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

void HistogramWidget::setMode(Mode mode)
{
    if (m_mode == mode) return;
    m_mode = mode;
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

void HistogramWidget::renderChannels(QPainter &painter,
                                     const QRect &subRect,
                                     const int *channelIndices,
                                     const QColor *channelColors,
                                     int channelCount,
                                     const QColor &gridColor,
                                     const QColor &axisColor,
                                     const QColor &textColor)
{
    if (subRect.width() <= 2 || subRect.height() <= 2) {
        return;
    }

    // Plot border.
    painter.setPen(QPen(gridColor, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(subRect);

    // Vertical quarter lines for visual reference.
    painter.setPen(QPen(QColor(50, 50, 50), 1, Qt::DotLine));
    for (int i = 1; i < 4; ++i) {
        const int x = subRect.left()
            + static_cast<int>(i * subRect.width() / 4.0);
        painter.drawLine(x, subRect.top(), x, subRect.bottom());
    }

    const double scale = peak();

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
    QImage overlay(subRect.size(), QImage::Format_ARGB32);
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
        const int plotHeight = subRect.height();
        const int plotWidth = subRect.width();
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
                double perPixelAlpha = static_cast<double>(alpha);
                if (dy + 1 > columnHeightF) {
                    const double partial = columnHeightF - dy;
                    perPixelAlpha = perPixelAlpha * std::max(0.0, partial);
                } else {
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

    // Draw all the requested channels.
    for (int c = 0; c < channelCount; ++c) {
        drawBinColumn(channelIndices[c], channelColors[c]);
    }

    painter.drawImage(subRect.topLeft(), overlay);

    // Tick labels (only when the sub-plot is wide enough to fit them
    // without crowding — i.e. the main AllChannels/LumaOnly plot).
    if (subRect.width() >= 80) {
        QFont smallFont = painter.font();
        smallFont.setPointSizeF(std::max(7.0, smallFont.pointSizeF() - 1.0));
        painter.setFont(smallFont);
        const QFontMetrics fm(smallFont);
        painter.setPen(QPen(axisColor, 1));
        const QString xLabels[] = {QStringLiteral("0"), QStringLiteral("64"),
                                   QStringLiteral("128"),
                                   QStringLiteral("192"),
                                   QStringLiteral("255")};
        const double xFracs[] = {0.0, 0.25, 0.5, 0.75, 1.0};
        for (int i = 0; i < 5; ++i) {
            const int x = subRect.left()
                + static_cast<int>(xFracs[i] * subRect.width());
            const QRect textRect(x - 16, subRect.bottom() + 2, 32, fm.height());
            painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop,
                             xLabels[i]);
        }
        painter.setPen(QPen(textColor, 1));
        const QRect xTitleRect(subRect.left(),
                               subRect.bottom() + fm.height() + 1,
                               subRect.width(), fm.height());
        painter.drawText(xTitleRect, Qt::AlignHCenter | Qt::AlignTop,
                         tr("Brightness"));
    }
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

    const QColor channelColors[3] = {
        QColor(220, 80, 80),
        QColor(80, 200, 100),
        QColor(80, 130, 230),
    };
    const QColor lumaColor(220, 220, 220);

    if (m_mode == RgbSplit) {
        // Three side-by-side plots, one per RGB channel.
        const int marginLeft = 18;
        const int marginRight = 8;
        const int marginTop = 22; // room for the per-plot label
        const int marginBottom = 18;
        const int gap = 6;

        const int totalPlotWidth = rect.width() - marginLeft - marginRight
            - 2 * gap;
        if (totalPlotWidth <= 30) {
            return;
        }
        const int plotW = totalPlotWidth / 3;
        const int plotH = rect.height() - marginTop - marginBottom;
        if (plotH <= 30) {
            return;
        }

        const QString titles[3] = {tr("Red"), tr("Green"), tr("Blue")};
        QFont titleFont = painter.font();
        titleFont.setPointSizeF(std::max(7.0, titleFont.pointSizeF() - 1.0));
        painter.setFont(titleFont);
        const QFontMetrics titleFm(titleFont);
        const int titleBaselineY = marginTop - 4;

        for (int i = 0; i < 3; ++i) {
            const QRect subRect(marginLeft + i * (plotW + gap),
                                marginTop, plotW, plotH);
            const int channelIndices[1] = {i};
            const QColor colors[1] = {channelColors[i]};
            renderChannels(painter, subRect, channelIndices, colors, 1,
                           gridColor, axisColor, textColor);

            // Channel label, centered above the sub-plot, in the channel color.
            painter.setPen(QPen(channelColors[i], 1));
            const QRect labelRect(subRect.left(), 0, subRect.width(),
                                  titleBaselineY + 4);
            painter.drawText(labelRect, Qt::AlignHCenter | Qt::AlignBottom,
                             titles[i]);
        }
        return;
    }

    // Single plot (AllChannels or LumaOnly).
    const int marginLeft = 26;
    const int marginRight = 8;
    const int marginTop = 8;
    const int marginBottom = 18;
    const QRect plotRect = rect.adjusted(marginLeft, marginTop, -marginRight, -marginBottom);
    if (plotRect.width() <= 2 || plotRect.height() <= 2) {
        return;
    }

    if (m_mode == AllChannels) {
        const int channelIndices[4] = {0, 1, 2, 3};
        const QColor colors[4] = {
            channelColors[0], channelColors[1], channelColors[2], lumaColor,
        };
        renderChannels(painter, plotRect, channelIndices, colors, 4,
                       gridColor, axisColor, textColor);
    } else {
        // LumaOnly
        const int channelIndices[1] = {3};
        const QColor colors[1] = {lumaColor};
        renderChannels(painter, plotRect, channelIndices, colors, 1,
                       gridColor, axisColor, textColor);
    }

    if (m_sourceImage.isNull()) {
        painter.setPen(QPen(textColor, 1));
        painter.drawText(rect, Qt::AlignCenter, tr("No preview"));
    }
}
