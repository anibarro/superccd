#include "WaveformWidget.h"

#include <QPaintEvent>
#include <QPainter>
#include <QFontMetrics>
#include <QVarLengthArray>
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
    // Cheap fast path: if the same (image, rect) is being pushed again
    // we already have a valid waveform in m_counts, so just repaint.
    // On the Raspberry Pi this is a big win because the preview
    // pipeline re-pushes the cached image on every visible-rect
    // refresh (scroll, zoom, "meter visible area only" toggles) and
    // a full re-sample would be a few megabytes of memory traffic.
    //
    // The identity check works because QImage uses copy-on-write: a
    // genuine pixel update produces a different backing buffer, so
    // constBits() / size / format differ; merely copying or
    // re-assigning the same QImage keeps the same buffer.
    if (m_dataValid
        && m_dataFingerprint == image.constBits()
        && m_dataSize == image.size()
        && m_dataFormat == image.format()
        && m_visibleRect == visibleRect) {
        update();
        return;
    }
    m_sourceImage = image;
    m_visibleRect = visibleRect;
    recompute();
    m_dataFingerprint = image.constBits();
    m_dataSize = image.size();
    m_dataFormat = image.format();
    m_dataValid = true;
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
    m_dataFingerprint = nullptr;
    m_dataSize = QSize();
    m_dataFormat = QImage::Format_Invalid;
    m_dataValid = true; // empty waveform is still "valid"
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

    // Always sample all 4 channels (R, G, B and luma) on every
    // recompute, regardless of the current visualization mode. The
    // per-mode channel skipping was removed because it left the
    // un-sampled channels in m_counts at all-zero, so a mode change
    // (e.g. LumaOnly -> RgbSplit) would render a blank graph until
    // the next preview-control change triggered a fresh recompute.
    // The cost increase is one extra bump() per pixel (3 RGB bumps
    // per pixel are always done; the luma is one Q8-fixed-point
    // add-shift which is essentially free on the Pi).

    // ---- Data grid sizing + 2x2 source sampling -------------------------
    // The on-screen waveform plots one data column per X pixel of the
    // widget overlay (the painter computes colStartX = (col *
    // overlayWidth) / m_columns, so a data column index beyond the
    // overlay width maps to the same screen pixel as the previous
    // column and is wasted work). It is therefore both correct and a
    // big performance win to size the internal data grid to the
    // widget's overlay width, clamped to the number of source columns
    // we have.
    //
    // For a 960-wide preview image displayed in a 220-px widget, this
    // shrinks the per-channel data buffer from 960*256 = 245,760 cells
    // down to ~220*256 = 56,320 cells, a 4.3x reduction. The painter
    // then maps each data column to exactly one on-screen X (since
    // m_columns == overlayWidth), which is gap-free by construction.
    //
    // On the source-pixel side we sample at a fixed "every other row,
    // every other column" stride (= 2 in both X and Y) to keep the
    // per-slider-tick cost reasonable on the Raspberry Pi. The
    // sampled source X is mapped to its data column by the same
    // integer divide the painter uses to map data column -> screen X,
    // so every data column gets roughly the same number of source X's
    // (no per-data-column under-sampling that would leave empty
    // columns painting as visible gaps). The stride grows in small
    // equal steps in X and Y only for very large sources.
    int overlayWidth = width();
    if (overlayWidth <= 2) {
        // Widget hasn't been laid out yet; fall back to the source
        // width so the next paint still has data.
        overlayWidth = rect.width();
    }
    const int sourceWidth = rect.width();
    m_columns = std::min(sourceWidth, overlayWidth);
    if (m_columns < 1) {
        m_columns = 1;
    }
    m_counts.resize(static_cast<size_t>(4) * m_columns * 256);
    std::fill(m_counts.begin(), m_counts.end(), 0);
    m_columnPeak.resize(m_columns);
    std::fill(m_columnPeak.begin(), m_columnPeak.end(), 0);

    int xStride = 2;
    int yStride = 2;
    const int height = rect.height();
    // 1 M-sample cap: a 1920x1280 preview at 2x2 is 614k samples
    // and stays snappy, a 4000x3000 preview jumps to 6 M samples and
    // is throttled back by growing the stride in equal X/Y steps so
    // the per-column peak normalization stays stable.
    constexpr int kMaxSamples = 1'000'000;
    while ((static_cast<long long>(sourceWidth / xStride)
            * (height / yStride))
           > kMaxSamples) {
        if (xStride <= yStride) {
            ++xStride;
        } else {
            ++yStride;
        }
        if (xStride > 8 && yStride > 8) {
            break;
        }
    }
    if (xStride < 1) {
        xStride = 1;
    }
    if (yStride < 1) {
        yStride = 1;
    }

    // Precompute which destination column each sampled source X maps
    // to. The mapping is the same integer divide the painter uses in
    // reverse (painter: col * overlayWidth / m_columns -> screen X;
    // here: sourceX * m_columns / sourceWidth -> col). With the data
    // grid sized to the overlay width, every data column receives
    // roughly the same number of source X's (no missing or
    // under-sampled columns that would show up as visible vertical
    // gaps in the rendered waveform).
    const int sampledSourceWidth = (sourceWidth + xStride - 1) / xStride;
    QVarLengthArray<int, 4096> colIndexForX;
    colIndexForX.resize(sampledSourceWidth);
    for (int i = 0; i < sampledSourceWidth; ++i) {
        const int sourceX = i * xStride;
        colIndexForX[i] = (sourceX * m_columns) / std::max(1, sourceWidth);
    }

    // Inline bump that avoids the function-call indirection from the
    // previous lambda, and uses quint32 indices rather than
    // size_t-multiplied offsets. On ARM this matters because the
    // compiler can keep the per-channel base pointers in registers.
    auto bump = [this](quint32 *counts, int colIndex, int bin) {
        const quint32 v = ++counts[colIndex * 256 + bin];
        if (v > m_columnPeak[colIndex]) {
            m_columnPeak[colIndex] = v;
        }
    };

    // Cache the per-channel pointers (one per channel) for the inner loop.
    quint32 *counts[4] = {
        m_counts.data() + 0 * static_cast<size_t>(m_columns) * 256,
        m_counts.data() + 1 * static_cast<size_t>(m_columns) * 256,
        m_counts.data() + 2 * static_cast<size_t>(m_columns) * 256,
        m_counts.data() + 3 * static_cast<size_t>(m_columns) * 256,
    };

    // For integer luma: 0.2126*R + 0.7152*G + 0.0722*B in Q8 fixed point
    // gives a value in [0, 255*256] for R/G/B in [0, 255]. We then
    // round-to-nearest by adding 128 before shifting right by 8. This
    // is a single multiply-add pair per channel with no libm call,
    // dramatically faster than std::lround() on the Raspberry Pi.
    constexpr int kLR = 54;  // 0.2126 * 256 ≈ 54
    constexpr int kLG = 183; // 0.7152 * 256 ≈ 183
    constexpr int kLB = 18;  // 0.0722 * 256 ≈ 18

    quint32 localGlobalPeak = 1;
    for (int y = rect.top(); y <= rect.bottom(); y += yStride) {
        const QRgb *scan = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int i = 0, x = rect.left(); x <= rect.right(); x += xStride, ++i) {
            const QRgb px = scan[x];
            const int r = qRed(px);
            const int g = qGreen(px);
            const int b = qBlue(px);
            const int colIndex = colIndexForX[i];
            // Always sample all 4 channels so a mode change shows
            // data immediately, without waiting for the next
            // preview-control event to trigger a fresh recompute.
            bump(counts[0], colIndex, r);
            bump(counts[1], colIndex, g);
            bump(counts[2], colIndex, b);
            // Rec. 709 luma in Q8 fixed point, rounded to the
            // nearest bin. The result fits comfortably in a 16-bit
            // int before the right-shift, so we can skip the
            // expensive std::lround path.
            const int yQ8 = (kLR * r + kLG * g + kLB * b + 128) >> 8;
            bump(counts[3], colIndex, yQ8 < 0 ? 0 : (yQ8 > 255 ? 255 : yQ8));
        }
    }
    // Final global peak: walk each column's recorded peak and take the
    // max. This is 1 read per column and avoids the O(W * 256) re-scan
    // that a naive "max of m_counts" would require. m_columnPeak was
    // updated inline as the histogram was filled, so this is the
    // global max across all (col, bin, channel) cells.
    for (int c = 0; c < m_columns; ++c) {
        if (m_columnPeak[c] > localGlobalPeak) {
            localGlobalPeak = m_columnPeak[c];
        }
    }
    m_globalPeak = std::max<quint32>(1, localGlobalPeak);
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
