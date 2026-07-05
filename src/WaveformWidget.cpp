#include "WaveformWidget.h"

#include <QPaintEvent>
#include <QPainter>
#include <QFontMetrics>
#include <cmath>
#include <algorithm>

WaveformWidget::WaveformWidget(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(220, 180);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
}

void WaveformWidget::setMode(Mode mode)
{
    if (m_mode == mode) {
        return;
    }
    m_mode = mode;
    update();
}

void WaveformWidget::setTransparency(double transparency)
{
    const double clamped = std::clamp(transparency, 0.0, 1.0);
    if (qFuzzyCompare(clamped + 1.0, m_transparency + 1.0)) {
        return;
    }
    m_transparency = clamped;
    update();
}

QSize WaveformWidget::sizeHint() const
{
    return QSize(360, 240);
}

QSize WaveformWidget::minimumSizeHint() const
{
    return QSize(180, 140);
}

void WaveformWidget::setSourceImage(const QImage &image, const QRect &visibleRect)
{
    m_sourceImage = image;
    m_visibleRect = visibleRect;
    recompute();
    update();
}

void WaveformWidget::clearSourceImage()
{
    m_sourceImage = QImage();
    m_visibleRect = QRect();
    m_columns = 0;
    m_counts.clear();
    m_columnPeak.clear();
    m_globalPeak = 1;
    update();
}

void WaveformWidget::recompute()
{
    m_counts.clear();
    m_columnPeak.clear();
    m_columns = 0;
    m_globalPeak = 1;
    if (m_sourceImage.isNull()) {
        return;
    }

    QImage image = m_sourceImage;
    if (image.format() != QImage::Format_RGB32
        && image.format() != QImage::Format_ARGB32) {
        image = image.convertToFormat(QImage::Format_RGB32);
    }

    QRect rect = m_visibleRect.isNull() ? image.rect() : m_visibleRect.intersected(image.rect());
    if (rect.isEmpty()) {
        return;
    }

    m_columns = rect.width();
    m_counts.resize(static_cast<size_t>(4) * m_columns * 256);
    std::fill(m_counts.begin(), m_counts.end(), 0);
    m_columnPeak.resize(m_columns);
    std::fill(m_columnPeak.begin(), m_columnPeak.end(), 0);

    // Vertical stride sampling to keep the cost reasonable on large previews.
    const int height = rect.height();
    const int targetSamples = 2'000'000;
    int stride = 1;
    while ((static_cast<long long>(m_columns) * height)
               / (static_cast<long long>(stride) * stride)
           > targetSamples) {
        ++stride;
    }
    if (stride < 1) {
        stride = 1;
    }

    quint32 globalPeak = 0;
    for (int y = rect.top(); y <= rect.bottom(); y += stride) {
        const QRgb *scan = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = rect.left(); x <= rect.right(); x += stride) {
            const QRgb px = scan[x];
            const int r = qRed(px);
            const int g = qGreen(px);
            const int b = qBlue(px);
            const int colIndex = x - rect.left();
            const size_t baseR = static_cast<size_t>(colIndex) * 256 + r;
            const size_t baseG = (static_cast<size_t>(m_columns) + colIndex) * 256 + g;
            const size_t baseB = (static_cast<size_t>(2 * m_columns) + colIndex) * 256 + b;
            ++m_counts[baseR];
            ++m_counts[baseG];
            ++m_counts[baseB];
            if (m_counts[baseR] > m_columnPeak[colIndex]) {
                m_columnPeak[colIndex] = m_counts[baseR];
            }
            if (m_counts[baseG] > m_columnPeak[colIndex]) {
                m_columnPeak[colIndex] = m_counts[baseG];
            }
            if (m_counts[baseB] > m_columnPeak[colIndex]) {
                m_columnPeak[colIndex] = m_counts[baseB];
            }
            if (m_counts[baseR] > globalPeak) globalPeak = m_counts[baseR];
            if (m_counts[baseG] > globalPeak) globalPeak = m_counts[baseG];
            if (m_counts[baseB] > globalPeak) globalPeak = m_counts[baseB];

            const int y709 = static_cast<int>(std::lround(0.2126 * r + 0.7152 * g + 0.0722 * b));
            const int yc = std::clamp(y709, 0, 255);
            // 3 * m_columns must be computed as size_t to avoid int
            // overflow on absurdly wide source images (> ~715M pixels).
            const size_t baseL = (static_cast<size_t>(3) * static_cast<size_t>(m_columns)
                                  + colIndex) * 256 + yc;
            ++m_counts[baseL];
            if (m_counts[baseL] > m_columnPeak[colIndex]) {
                m_columnPeak[colIndex] = m_counts[baseL];
            }
            if (m_counts[baseL] > globalPeak) globalPeak = m_counts[baseL];
        }
    }
    m_globalPeak = std::max<quint32>(1, globalPeak);
}

void WaveformWidget::resizeEvent(QResizeEvent * /*event*/)
{
    update();
}

void WaveformWidget::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    const QRect rect = this->rect();
    const QColor background(32, 32, 32);
    painter.fillRect(rect, background);

    // Axis-style colors used by the per-subplot rendering helpers below.
    const QFont smallFont = []() {
        QFont f;
        f.setPointSizeF(std::max(7.0, f.pointSizeF() - 1.0));
        return f;
    }();
    (void)smallFont;

    const QColor channelColors[3] = {
        QColor(220, 80, 80),
        QColor(80, 200, 100),
        QColor(80, 130, 230),
    };
    const QColor lumaColor(220, 220, 220);

    if (m_mode == RgbSplit) {
        // Three side-by-side plots, one per RGB channel. The mode label
        // sits above each plot; axis decorations (X ticks, Y title) only
        // appear under the middle plot to reduce visual noise.
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

        // Plot labels above each sub-plot.
        QFont titleFont = painter.font();
        titleFont.setPointSizeF(std::max(7.0, titleFont.pointSizeF() - 1.0));
        painter.setFont(titleFont);
        const QFontMetrics titleFm(titleFont);
        const QString titles[3] = {tr("Red"), tr("Green"), tr("Blue")};
        const int titleBaselineY = marginTop - 4;

        for (int i = 0; i < 3; ++i) {
            const QRect subRect(marginLeft + i * (plotW + gap),
                                marginTop, plotW, plotH);
            const int channelIndices[1] = {i};
            const QColor colors[1] = {channelColors[i]};
            renderChannels(painter, subRect, channelIndices, colors, 1);

            // Channel label, centered above the sub-plot, in the channel color.
            painter.setPen(QPen(channelColors[i], 1));
            const QRect labelRect(subRect.left(), 0, subRect.width(), titleBaselineY + 4);
            painter.drawText(labelRect, Qt::AlignHCenter | Qt::AlignBottom, titles[i]);

            // No axis decorations in RGB-split mode: the three sub-plots
            // share the same X axis so the per-plot 0-255 ticks and the
            // "Source X" title would just add visual noise.
            drawAxisDecorations(painter, subRect, QString(), QString());
        }
    } else {
        // Single plot: either All (RGB + luma) or Luma only.
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
            renderChannels(painter, plotRect, channelIndices, colors, 4);
        } else {
            // LumaOnly
            const int channelIndices[1] = {3};
            const QColor colors[1] = {lumaColor};
            renderChannels(painter, plotRect, channelIndices, colors, 1);
        }

        drawAxisDecorations(painter, plotRect, tr("Source X"), tr("Luminance"));
    }

    if (m_sourceImage.isNull()) {
        drawNoPreviewMessage(painter, rect);
    }
}

void WaveformWidget::renderChannels(QPainter &painter,
                                    const QRect &subRect,
                                    const int *channelIndices,
                                    const QColor *channelColors,
                                    int channelCount)
{
    if (subRect.width() <= 2 || subRect.height() <= 2) {
        return;
    }

    const QColor gridColor(60, 60, 60);

    // Plot border.
    painter.setPen(QPen(gridColor, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(subRect);

    // Vertical and horizontal quarter reference lines.
    painter.setPen(QPen(QColor(50, 50, 50), 1, Qt::DotLine));
    for (int i = 1; i < 4; ++i) {
        const int x = subRect.left() + static_cast<int>(i * subRect.width() / 4.0);
        painter.drawLine(x, subRect.top(), x, subRect.bottom());
    }
    for (int i = 1; i < 4; ++i) {
        const int y = subRect.top() + static_cast<int>(i * subRect.height() / 4.0);
        painter.drawLine(subRect.left(), y, subRect.right(), y);
    }

    if (m_columns <= 0 || m_globalPeak <= 0 || m_counts.isEmpty()) {
        return;
    }

    // Per-pixel alpha is computed against a per-column reference, not a
    // single global peak. A single hot column (a bright lamp, a sun
    // reflection) would otherwise set the global peak so high that every
    // other column dimmed to nearly invisible. With per-column reference
    // each column self-normalizes: the densest cell in that column
    // reaches full opacity and the rest of the column fades with a
    // square-root curve so even very sparse bins stay faintly visible
    // (the standard "pro" waveform monitor look).
    //
    // IMPORTANT: the overlay is non-premultiplied ARGB32. Earlier this
    // code used Format_ARGB32_Premultiplied but wrote non-premultiplied
    // values with qRgba(c.r, c.g, c.b, alpha), which produced wrong
    // colors (e.g. the blue channel turning olive green) and made the
    // transparency slider do nothing visible. The src-over blend below
    // works in straight (non-premultiplied) alpha and gives correct
    // colors at every alpha level.
    QImage overlay(subRect.size(), QImage::Format_ARGB32);
    overlay.fill(Qt::transparent);

    for (int ci = 0; ci < channelCount; ++ci) {
        const int c = channelIndices[ci];
        const QColor color = channelColors[ci];
        // Per-data-cell rendering: iterate over the source columns and
        // brightness bins (the data grid) and, for each cell that has
        // pixels in it, fill the FULL on-screen rectangle that cell maps
        // to. This preserves the original per-cell representation (which
        // matched the image correctly): the Y axis follows the
        // oscilloscope convention bright-at-top/dark-at-bottom, and the
        // waveform shape corresponds exactly to the source image's
        // brightness distribution per column. Filling the whole cell
        // rectangle (instead of a single pixel at the cell center) is
        // what makes the waveform scale cleanly when the widget is
        // resized larger: there are no black gaps between cells.
        const int overlayWidth = overlay.width();
        const int overlayHeight = overlay.height();
        const size_t channelBase = static_cast<size_t>(c) * m_columns * 256;
        for (int col = 0; col < m_columns; ++col) {
            const double ref = static_cast<double>(
                std::max<quint32>(1, m_columnPeak[col]));
            // On-screen X extent of this source column. Enforce a
            // minimum width of 1 px so cells never collapse to zero
            // width when the image has more columns than the widget has
            // pixels (which is the normal case for a 4000-pixel-wide
            // source in a 180-pixel-wide widget). Without this clamp,
            // colEndX == colStartX for many consecutive columns and the
            // inner write loop is skipped entirely — that was the cause
            // of the gaps at every widget size.
            int colStartX = (col * overlayWidth) / m_columns;
            int colEndX = ((col + 1) * overlayWidth) / m_columns;
            if (colEndX <= colStartX) {
                colEndX = colStartX + 1;
            }
            // Clamp to the widget area so we don't write past the edge
            // on the last column.
            if (colEndX > overlayWidth) {
                colEndX = overlayWidth;
            }
            if (colStartX < 0) {
                colStartX = 0;
            }
            const size_t colBase = channelBase + static_cast<size_t>(col) * 256;
            for (int bin = 0; bin < 256; ++bin) {
                const quint32 count = m_counts[colBase + bin];
                if (count == 0) {
                    continue;
                }
                const double frac = std::min(1.0, static_cast<double>(count) / ref);
                // Square-root curve: alpha = 255 * sqrt(frac). A cell
                // with 1% the density of the column peak still gets
                // alpha ~= 25 (clearly visible), and 0.01% gets alpha
                // ~= 2.5 (faintly there). The per-cell value is then
                // scaled by the user-controlled transparency
                // (1.0 - m_transparency), so 0 = fully opaque, 1 = fully
                // transparent.
                const int alpha = std::clamp(
                    static_cast<int>(255.0 * std::sqrt(frac)
                                     * (1.0 - m_transparency)),
                    0, 255);
                if (alpha <= 0) {
                    continue;
                }
                // On-screen Y extent of this brightness bin. The Y axis
                // is inverted: bin 0 (dark) at the bottom, bin 255 (bright)
                // at the top — standard waveform convention. Enforce a
                // minimum height of 1 px for the same reason as the X
                // extent: when the widget is shorter than 256 px (which
                // is common), adjacent brightness bins would otherwise
                // collapse to the same row and be skipped.
                int binStartY = overlayHeight
                    - ((bin + 1) * overlayHeight) / 256;
                int binEndY = overlayHeight
                    - (bin * overlayHeight) / 256;
                if (binEndY <= binStartY) {
                    binEndY = binStartY + 1;
                }
                if (binEndY > overlayHeight) {
                    binEndY = overlayHeight;
                }
                if (binStartY < 0) {
                    binStartY = 0;
                }
                // Fill the whole cell rectangle with this alpha. The
                // src-over blend is the same for every pixel of the
                // cell, so we just repeat it.
                const int a = alpha;
                const int invA = 255 - a;
                for (int localY = binStartY; localY < binEndY; ++localY) {
                    if (localY < 0 || localY >= overlayHeight) {
                        continue;
                    }
                    QRgb *row = reinterpret_cast<QRgb *>(overlay.scanLine(localY));
                    for (int localX = colStartX; localX < colEndX; ++localX) {
                        if (localX < 0 || localX >= overlayWidth) {
                            continue;
                        }
                        QRgb *pixel = row + localX;
                        const QRgb prev = *pixel;
                        const int prevA = qAlpha(prev);
                        // Src-over alpha compositing in non-premultiplied
                        // space:
                        //   outA = sa + da * (1 - sa)
                        //   outC = (sc * sa + dc * da * (1 - sa)) / outA
                        const int newA = a + prevA - (a * prevA) / 255;
                        if (newA <= 0) {
                            continue;
                        }
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
        }
    }

    painter.drawImage(subRect.topLeft(), overlay);

    // Clipping / black-crush indicator: paint a thin red overlay at the
    // top edge of the plot for any column where a non-luma channel has
    // pixels pinned to 255, and at the bottom edge for any column where a
    // non-luma channel has pixels pinned to 0. The intensity scales with
    // the count of clipped pixels.
    if (channelCount > 0) {
        const QColor clipColor(255, 80, 80);
        const QColor crushColor(80, 130, 255);
        const int overlayWidth = overlay.width();
        for (int col = 0; col < m_columns; ++col) {
            double clipFrac = 0.0;
            double crushFrac = 0.0;
            for (int ci = 0; ci < channelCount; ++ci) {
                const int c = channelIndices[ci];
                if (c == 3) {
                    continue; // luma isn't a clipping channel
                }
                const quint32 clipCount = m_counts[
                    static_cast<size_t>(c) * m_columns * 256
                    + static_cast<size_t>(col) * 256 + 255];
                const quint32 crushCount = m_counts[
                    static_cast<size_t>(c) * m_columns * 256
                    + static_cast<size_t>(col) * 256 + 0];
                clipFrac = std::max(clipFrac,
                                    static_cast<double>(clipCount) / m_globalPeak);
                crushFrac = std::max(crushFrac,
                                     static_cast<double>(crushCount) / m_globalPeak);
            }
            const int screenX = subRect.left()
                + static_cast<int>((col + 0.5) * subRect.width() / m_columns);
            const int localX = screenX - subRect.left();
            if (localX < 0 || localX >= overlayWidth) {
                continue;
            }
            if (clipFrac > 0.0) {
                const int alpha = std::clamp(
                    static_cast<int>(255.0 * clipFrac * 2.0
                                     * (1.0 - m_transparency)),
                    0, 255);
                // Same src-over blend as the main per-channel loop,
                // working in non-premultiplied alpha so the colors stay
                // correct and the transparency slider has a real effect.
                if (alpha > 0) {
                    QRgb *top = reinterpret_cast<QRgb *>(overlay.scanLine(0)) + localX;
                    const QRgb prev = *top;
                    const int prevA = qAlpha(prev);
                    const int a = alpha;
                    const int invA = 255 - a;
                    const int newA = a + prevA - (a * prevA) / 255;
                    if (newA > 0) {
                        const int r = std::clamp(
                            (clipColor.red() * a
                                + qRed(prev) * prevA * invA / 255) / newA,
                            0, 255);
                        const int g = std::clamp(
                            (clipColor.green() * a
                                + qGreen(prev) * prevA * invA / 255) / newA,
                            0, 255);
                        const int b = std::clamp(
                            (clipColor.blue() * a
                                + qBlue(prev) * prevA * invA / 255) / newA,
                            0, 255);
                        *top = qRgba(r, g, b, newA);
                    }
                }
            }
            if (crushFrac > 0.0) {
                const int alpha = std::clamp(
                    static_cast<int>(255.0 * crushFrac * 2.0
                                     * (1.0 - m_transparency)),
                    0, 255);
                if (alpha > 0) {
                    QRgb *bot = reinterpret_cast<QRgb *>(
                        overlay.scanLine(overlay.height() - 1)) + localX;
                    const QRgb prev = *bot;
                    const int prevA = qAlpha(prev);
                    const int a = alpha;
                    const int invA = 255 - a;
                    const int newA = a + prevA - (a * prevA) / 255;
                    if (newA > 0) {
                        const int r = std::clamp(
                            (crushColor.red() * a
                                + qRed(prev) * prevA * invA / 255) / newA,
                            0, 255);
                        const int g = std::clamp(
                            (crushColor.green() * a
                                + qGreen(prev) * prevA * invA / 255) / newA,
                            0, 255);
                        const int b = std::clamp(
                            (crushColor.blue() * a
                                + qBlue(prev) * prevA * invA / 255) / newA,
                            0, 255);
                        *bot = qRgba(r, g, b, newA);
                    }
                }
            }
        }
    }
}

void WaveformWidget::drawAxisDecorations(QPainter &painter,
                                         const QRect &subRect,
                                         const QString &xTitle,
                                         const QString &yTitle)
{
    if (subRect.width() <= 2 || subRect.height() <= 2) {
        return;
    }
    const QColor axisColor(140, 140, 140);
    const QColor textColor(200, 200, 200);

    QFont smallFont = painter.font();
    smallFont.setPointSizeF(std::max(7.0, smallFont.pointSizeF() - 1.0));
    painter.setFont(smallFont);
    const QFontMetrics fm(smallFont);

    if (!xTitle.isEmpty()) {
        painter.setPen(QPen(axisColor, 1));
        const QString xLabels[] = {QStringLiteral("0"), QStringLiteral("64"),
                                   QStringLiteral("128"), QStringLiteral("192"),
                                   QStringLiteral("255")};
        const double xFracs[] = {0.0, 0.25, 0.5, 0.75, 1.0};
        for (int i = 0; i < 5; ++i) {
            const int x = subRect.left() + static_cast<int>(xFracs[i] * subRect.width());
            const QRect textRect(x - 16, subRect.bottom() + 2, 32, fm.height());
            painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop, xLabels[i]);
        }
        painter.setPen(QPen(textColor, 1));
        const QRect xTitleRect(subRect.left(),
                               subRect.bottom() + fm.height() + 1,
                               subRect.width(), fm.height());
        painter.drawText(xTitleRect, Qt::AlignHCenter | Qt::AlignTop, xTitle);
    }

    if (!yTitle.isEmpty()) {
        painter.setPen(QPen(textColor, 1));
        painter.save();
        painter.translate(fm.height() / 2 + 2,
                          subRect.top() + subRect.height() / 2);
        painter.rotate(-90);
        const int yTitleWidth = fm.horizontalAdvance(yTitle);
        const QRect yTitleRect(-subRect.height() / 2, -yTitleWidth / 2,
                               subRect.height(), yTitleWidth);
        painter.drawText(yTitleRect, Qt::AlignHCenter | Qt::AlignVCenter, yTitle);
        painter.restore();
    }
}

void WaveformWidget::drawNoPreviewMessage(QPainter &painter, const QRect &rect)
{
    const QColor textColor(200, 200, 200);
    QFont smallFont = painter.font();
    smallFont.setPointSizeF(std::max(7.0, smallFont.pointSizeF() - 1.0));
    painter.setFont(smallFont);
    painter.setPen(QPen(textColor, 1));
    painter.drawText(rect, Qt::AlignCenter, tr("No preview"));
}
