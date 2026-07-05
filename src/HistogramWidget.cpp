#include "HistogramWidget.h"

#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <cmath>
#include <algorithm>

HistogramWidget::HistogramWidget(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(220, 160);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    for (int c = 0; c < 4; ++c) {
        for (int i = 0; i < 256; ++i) {
            m_hist[c][i] = 0.0;
        }
    }
}

QSize HistogramWidget::sizeHint() const
{
    return QSize(360, 220);
}

QSize HistogramWidget::minimumSizeHint() const
{
    return QSize(180, 120);
}

void HistogramWidget::setSourceImage(const QImage &image, const QRect &visibleRect)
{
    m_sourceImage = image;
    m_visibleRect = visibleRect;
    recompute();
    update();
}

void HistogramWidget::clearSourceImage()
{
    m_sourceImage = QImage();
    m_visibleRect = QRect();
    for (int c = 0; c < 4; ++c) {
        for (int i = 0; i < 256; ++i) {
            m_hist[c][i] = 0.0;
        }
    }
    m_peak = 1.0;
    update();
}

double HistogramWidget::bucket(int channel, int bin) const
{
    return m_hist[channel][std::clamp(bin, 0, 255)];
}

double HistogramWidget::peak() const
{
    return std::max(1.0, m_peak);
}

void HistogramWidget::recompute()
{
    for (int c = 0; c < 4; ++c) {
        for (int i = 0; i < 256; ++i) {
            m_hist[c][i] = 0.0;
        }
    }
    if (m_sourceImage.isNull()) {
        m_peak = 1.0;
        return;
    }

    QImage image = m_sourceImage;
    if (image.format() != QImage::Format_RGB32
        && image.format() != QImage::Format_ARGB32) {
        image = image.convertToFormat(QImage::Format_RGB32);
    }

    QRect rect = m_visibleRect.isNull() ? image.rect() : m_visibleRect.intersected(image.rect());
    if (rect.isEmpty()) {
        m_peak = 1.0;
        return;
    }

    // Sample on a stride so very large previews stay snappy. 8 MP cap is
    // well above what the user can visually distinguish.
    const int width = rect.width();
    const int height = rect.height();
    const int totalPixels = width * height;
    const int targetSamples = 2'000'000;
    int stride = 1;
    while ((totalPixels / (stride * stride)) > targetSamples) {
        ++stride;
    }
    if (stride < 1) {
        stride = 1;
    }

    for (int y = rect.top(); y <= rect.bottom(); y += stride) {
        const QRgb *scan = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = rect.left(); x <= rect.right(); x += stride) {
            const QRgb px = scan[x];
            const int r = qRed(px);
            const int g = qGreen(px);
            const int b = qBlue(px);
            m_hist[0][r] += 1.0;
            m_hist[1][g] += 1.0;
            m_hist[2][b] += 1.0;
            // Rec. 709 luma, rounded to the nearest bin.
            const int y709 = static_cast<int>(std::lround(0.2126 * r + 0.7152 * g + 0.0722 * b));
            m_hist[3][std::clamp(y709, 0, 255)] += 1.0;
        }
    }

    // Choose a reference value that represents the "typical loud" bin,
    // NOT the absolute max. If a single bin (e.g. the clipped highlights
    // at 255) dominates the count, an absolute-max reference would
    // crush every other bin to invisible, so a perfectly normal image
    // with a tiny over-exposed spot would look like a flat floor.
    //
    // We use the 99.5th percentile across all (channel, bin) pairs,
    // which is what Resolve / Premiere / Lightroom all do. The clipping
    // indicator at the right edge is computed separately from the raw
    // bin count and is not affected by this choice.
    constexpr double kReferencePercentile = 0.995;
    double values[4 * 256];
    int n = 0;
    for (int c = 0; c < 4; ++c) {
        for (int i = 0; i < 256; ++i) {
            values[n++] = m_hist[c][i];
        }
    }
    std::sort(values, values + n);
    const int idx = std::clamp(
        static_cast<int>(std::lround(kReferencePercentile * (n - 1))),
        0, n - 1);
    m_peak = std::max(1.0, values[idx]);
}

void HistogramWidget::resizeEvent(QResizeEvent * /*event*/)
{
    // Repaint the same data; nothing structural to recompute.
    update();
}

void HistogramWidget::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    const QRect rect = this->rect();
    const QColor background(32, 32, 32);
    const QColor gridColor(60, 60, 60);
    const QColor axisColor(140, 140, 140);
    const QColor textColor(200, 200, 200);

    painter.fillRect(rect, background);

    // Leave a tiny margin for tick labels.
    const int marginLeft = 26;
    const int marginRight = 8;
    const int marginTop = 8;
    const int marginBottom = 18;
    const QRect plotRect = rect.adjusted(marginLeft, marginTop, -marginRight, -marginBottom);
    if (plotRect.width() <= 2 || plotRect.height() <= 2) {
        return;
    }

    // Plot border.
    painter.setPen(QPen(gridColor, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(plotRect);

    // Vertical quarter lines for visual reference.
    painter.setPen(QPen(QColor(50, 50, 50), 1, Qt::DotLine));
    for (int i = 1; i < 4; ++i) {
        const int x = plotRect.left() + static_cast<int>(i * plotRect.width() / 4.0);
        painter.drawLine(x, plotRect.top(), x, plotRect.bottom());
    }

    const double scale = peak();
    const QColor channelColors[3] = {
        QColor(220, 80, 80),
        QColor(80, 200, 100),
        QColor(80, 130, 230),
    };
    const QColor lumaColor(220, 220, 220);

    // Per-pixel alpha uses a square-root curve over (count / reference)
    // so the dynamic range of bin counts is compressed gracefully: even a
    // bin with 1/1000th the reference count stays faintly visible, while
    // the densest bin still reaches full opacity. This is the standard
    // "pro" histogram transparency behavior (Resolve, Premiere,
    // Lightroom) — a linear curve would crush faint bins to invisible.
    //
    // The overlay is non-premultiplied ARGB32 and we use the standard
    // src-over alpha-compositing formula (in straight alpha space). The
    // earlier premultiplied format + additive math produced wrong
    // colors when alpha was anything but 255.
    QImage overlay(plotRect.size(), QImage::Format_ARGB32);
    overlay.fill(Qt::transparent);

    auto drawBinColumn = [&](int channel, const QColor &color) {
        // Per-pixel iteration: for each (localY, localX) in the overlay,
        // compute the floating-point bin position from localX, linearly
        // interpolate the bin count between adjacent bins, derive the
        // column height for that continuous count, and write the column
        // alpha at this pixel. This eliminates the visible horizontal
        // steps between bins that the old per-bin code produced (each
        // bin's column height was a different integer, so adjacent bins
        // had different top edges and the difference was visible as a
        // hard horizontal stripe).
        const int plotHeight = plotRect.height();
        const int plotWidth = plotRect.width();
        const int columnBottom = plotHeight - 1;
        for (int localX = 0; localX < plotWidth; ++localX) {
            const double binF = (static_cast<double>(localX) * 255.0)
                                / std::max(1, plotWidth - 1);
            const int bin0 = std::clamp(
                static_cast<int>(std::floor(binF)), 0, 255);
            const int bin1 = std::min(255, bin0 + 1);
            const double binT = binF - std::floor(binF);
            const double count = bucket(channel, bin0)
                + (bucket(channel, bin1) - bucket(channel, bin0)) * binT;
            if (count <= 0.0) {
                continue;
            }
            const double frac = std::min(1.0, count / scale);
            // Square-root curve: alpha = 255 * sqrt(frac).
            const int alpha = std::clamp(
                static_cast<int>(255.0 * std::sqrt(frac)), 0, 255);
            if (alpha <= 0) {
                continue;
            }
            // Column height is the fractional part of (frac * plotHeight).
            // Two adjacent pixels with slightly different binF (and thus
            // slightly different counts) will have slightly different
            // column heights, so the top edge of the histogram varies
            // smoothly from pixel to pixel.
            const double columnHeightF = frac * plotHeight;
            const int columnHeightCeil = static_cast<int>(
                std::ceil(columnHeightF));
            if (columnHeightCeil <= 0) {
                continue;
            }
            for (int dy = 0; dy < columnHeightCeil; ++dy) {
                const int localY = columnBottom - dy;
                if (localY < 0 || localY >= plotHeight) {
                    continue;
                }
                // Alpha at this pixel: full strength up to the
                // floor(columnHeightF) mark, then linearly fades to 0
                // across the partial top row. This is what makes the
                // top edge of the histogram smooth instead of stepped.
                double perPixelAlpha = static_cast<double>(alpha);
                if (dy + 1 > columnHeightF) {
                    // We're in the partial top row.
                    const double partial = columnHeightF - dy;
                    perPixelAlpha = perPixelAlpha * std::max(0.0, partial);
                } else {
                    // Standard column-body fade (the original "denser
                    // bins are more opaque at the bottom" effect).
                    perPixelAlpha = perPixelAlpha
                        * (1.0 - static_cast<double>(dy)
                                / columnHeightCeil * 0.6);
                }
                const int heightAlpha = std::clamp(
                    static_cast<int>(perPixelAlpha), 0, 255);
                if (heightAlpha <= 0) {
                    continue;
                }
                QRgb *pixel = reinterpret_cast<QRgb *>(
                    overlay.scanLine(localY)) + localX;
                const QRgb prev = *pixel;
                const int prevA = qAlpha(prev);
                const int a = heightAlpha;
                const int invA = 255 - a;
                const int newA = a + prevA - (a * prevA) / 255;
                if (newA > 0) {
                    const int r = std::clamp(
                        (color.red() * a
                            + qRed(prev) * prevA * invA / 255) / newA,
                        0, 255);
                    const int g = std::clamp(
                        (color.green() * a
                            + qGreen(prev) * prevA * invA / 255) / newA,
                        0, 255);
                    const int b = std::clamp(
                        (color.blue() * a
                            + qBlue(prev) * prevA * invA / 255) / newA,
                        0, 255);
                    *pixel = qRgba(r, g, b, newA);
                }
            }
        }
    };

    // RGB channels (additive blending so overlap stays distinct).
    for (int c = 0; c < 3; ++c) {
        drawBinColumn(c, channelColors[c]);
    }
    // Luma on top in white.
    drawBinColumn(3, lumaColor);

    painter.drawImage(plotRect.topLeft(), overlay);

    // Overexposure / underexposure indicators: a vertical bar at the right
    // (or left) edge whose height is proportional to the *percentage of
    // the image* that's clipped to 255 (or crushed to 0). The reference
    // here is the total number of sampled pixels, not the bin peak, so
    // the bar reads as "X% of the image is clipped" regardless of how
    // the rest of the histogram is distributed. The bar saturates at
    // ~5% (a common "you've lost a lot of detail" threshold).
    auto maxBinCount = [&](int channel, int bin) {
        double v = 0.0;
        for (int c = 0; c < (channel < 0 ? 3 : channel + 1); ++c) {
            if (channel >= 0 && c != channel) {
                continue;
            }
            v = std::max(v, bucket(c, bin));
        }
        return v;
    };
    double totalSamples = 0.0;
    for (int c = 0; c < 4; ++c) {
        for (int i = 0; i < 256; ++i) {
            totalSamples += m_hist[c][i];
        }
    }
    totalSamples = std::max(1.0, totalSamples / 4.0); // average over channels
    // 5% of the image clipped => bar reaches the top of the plot.
    constexpr double kClipSaturationFraction = 0.05;
    const double clipFrac = maxBinCount(2, 255) / totalSamples
                            / kClipSaturationFraction;
    const double crushFrac = maxBinCount(0, 0) / totalSamples
                             / kClipSaturationFraction;
    constexpr int kIndicatorWidth = 6;
    constexpr int kIndicatorMargin = 1;
    if (clipFrac > 0.0) {
        const int barH = std::clamp(
            static_cast<int>(std::min(1.0, clipFrac) * plotRect.height()),
            kIndicatorWidth, plotRect.height() - 2 * kIndicatorMargin);
        const QRect clipBar(plotRect.right() - kIndicatorWidth - kIndicatorMargin,
                            plotRect.bottom() - barH - kIndicatorMargin,
                            kIndicatorWidth, barH);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 70, 70, 230));
        painter.drawRect(clipBar);
    }
    if (crushFrac > 0.0) {
        const int barH = std::clamp(
            static_cast<int>(std::min(1.0, crushFrac) * plotRect.height()),
            kIndicatorWidth, plotRect.height() - 2 * kIndicatorMargin);
        const QRect crushBar(plotRect.left() + kIndicatorMargin,
                             plotRect.bottom() - barH - kIndicatorMargin,
                             kIndicatorWidth, barH);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(80, 130, 255, 230));
        painter.drawRect(crushBar);
    }
    // Tick the right edge of the plot at 255 so the indicator is
    // associated with the correct brightness value.
    painter.setPen(QPen(QColor(255, 70, 70, 180), 1, Qt::DotLine));
    painter.drawLine(plotRect.right() - kIndicatorWidth - kIndicatorMargin * 2 - 1,
                     plotRect.top(),
                     plotRect.right() - kIndicatorWidth - kIndicatorMargin * 2 - 1,
                     plotRect.bottom());
    painter.setPen(QPen(QColor(80, 130, 255, 180), 1, Qt::DotLine));
    painter.drawLine(plotRect.left() + kIndicatorWidth + kIndicatorMargin * 2 + 1,
                     plotRect.top(),
                     plotRect.left() + kIndicatorWidth + kIndicatorMargin * 2 + 1,
                     plotRect.bottom());


    // Axis tick labels.
    QFont smallFont = painter.font();
    smallFont.setPointSizeF(std::max(7.0, smallFont.pointSizeF() - 1.0));
    painter.setFont(smallFont);
    const QFontMetrics fm(smallFont);
    painter.setPen(QPen(axisColor, 1));
    const QString xLabels[] = {QStringLiteral("0"), QStringLiteral("64"),
                               QStringLiteral("128"), QStringLiteral("192"),
                               QStringLiteral("255")};
    const double xFracs[] = {0.0, 0.25, 0.5, 0.75, 1.0};
    for (int i = 0; i < 5; ++i) {
        const int x = plotRect.left() + static_cast<int>(xFracs[i] * plotRect.width());
        const QRect textRect(x - 16, plotRect.bottom() + 2, 32, fm.height());
        painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop, xLabels[i]);
    }
    painter.setPen(QPen(textColor, 1));
    const QRect xTitleRect(plotRect.left(), plotRect.bottom() + fm.height() + 1,
                           plotRect.width(), fm.height());
    painter.drawText(xTitleRect, Qt::AlignHCenter | Qt::AlignTop, tr("Brightness"));

    // Y label, rotated.
    painter.save();
    painter.translate(fm.height() / 2 + 2, plotRect.top() + plotRect.height() / 2);
    painter.rotate(-90);
    const QString yTitle = tr("Pixels");
    const int yTitleWidth = fm.horizontalAdvance(yTitle);
    const QRect yTitleRect(-plotRect.height() / 2, -yTitleWidth / 2,
                           plotRect.height(), yTitleWidth);
    painter.drawText(yTitleRect, Qt::AlignHCenter | Qt::AlignVCenter, yTitle);
    painter.restore();

    if (m_sourceImage.isNull()) {
        painter.setPen(QPen(textColor, 1));
        painter.drawText(rect, Qt::AlignCenter, tr("No preview"));
    }
}
