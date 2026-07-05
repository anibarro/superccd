#include "VectorscopeWidget.h"

#include <QPaintEvent>
#include <QPainter>
#include <QFontMetrics>
#include <QRadialGradient>
#include <cmath>
#include <algorithm>

namespace {

// ── YIQ colour space (NTSC / SMPTE 170M) ──────────────────────────
// I = orange/cyan axis (horizontal on screen)
// Q = purple/yellow-green axis (vertical on screen)
// Q is negated so the standard broadcast layout (R→Mg→B→Cy→G→Yl
// clockwise) is produced with the rotation below.

inline void rgbToIQ(double r, double g, double b, double &i, double &q)
{
    const double y = 0.299 * r + 0.587 * g + 0.114 * b;
    i = 0.595716 * r - 0.274453 * g - 0.321263 * b;
    q = 0.211456 * r - 0.522591 * g + 0.311135 * b;
    (void)y;
}

// Inverse: convert (I, Q) back to linear RGB (may produce out-of-gamut
// values; the caller clips). Used to build the colour-wheel cache.
inline void iqToRgb(double i, double q, double &r, double &g, double &b)
{
    const double qn = -q;
    const double y = 0.0;
    r = y + 0.95629572 * i + 0.62102413 * qn;
    g = y - 0.27212211 * i - 0.64738068 * qn;
    b = y - 1.10698902 * i + 1.70461445 * qn;
}

// Rotation: ~290° yields the darktable/DaVinci standard vectorscope
// layout (R top-right, Y upper-left, G left, Cy lower-left, B lower-right,
// M right). Computed by matching the rotated IQ angles of the R/Y/G/Cy/B/M
// cube vertices to the canonical positions used by those tools.
constexpr double kRotationDeg = 290.0;
constexpr double kRotationRad = kRotationDeg * M_PI / 180.0;

inline void rotateIQ(double i, double q, double &iOut, double &qOut)
{
    const double c = std::cos(kRotationRad);
    const double s = std::sin(kRotationRad);
    iOut = i * c - q * s;
    qOut = i * s + q * c;
}

inline void rotateIQInv(double i, double q, double &iOut, double &qOut)
{
    const double c = std::cos(kRotationRad);
    const double s = std::sin(kRotationRad);
    iOut =  i * c + q * s;
    qOut = -i * s + q * c;
}

// Maximum chroma (I/Q magnitude) for a fully-saturated RGB triple.
constexpr double kMaxChroma = 0.6;

} // namespace

// ═══════════════════════════════════════════════════════════════════
//  Construction
// ═══════════════════════════════════════════════════════════════════

VectorscopeWidget::VectorscopeWidget(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(220, 220);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
}

QSize VectorscopeWidget::sizeHint() const
{
    return QSize(360, 360);
}

QSize VectorscopeWidget::minimumSizeHint() const
{
    return QSize(180, 180);
}

void VectorscopeWidget::setTransparency(double transparency)
{
    const double clamped = std::clamp(transparency, 0.0, 1.0);
    if (qFuzzyCompare(clamped + 1.0, m_transparency + 1.0)) return;
    m_transparency = clamped;
    update();
}

// ═══════════════════════════════════════════════════════════════════
//  Data binning – density (hit count) only
// ═══════════════════════════════════════════════════════════════════

void VectorscopeWidget::setSourceImage(const QImage &image, const QRect &visibleRect)
{
    m_sourceImage = image;
    m_visibleRect = visibleRect;
    recompute();
    update();
}

void VectorscopeWidget::clearSourceImage()
{
    m_sourceImage = QImage();
    m_visibleRect = QRect();
    m_gridCount.clear();
    m_gridSize = 0;
    m_peakCount = 1;
    m_colorWheelCache = QImage();
    m_cacheDiameter = 0;
    update();
}

int VectorscopeWidget::indexFor(int binX, int binY) const
{
    return binY * m_gridSize + binX;
}

void VectorscopeWidget::recompute()
{
    m_gridCount.clear();
    m_gridSize = 0;
    m_peakCount = 1;

    if (m_sourceImage.isNull()) return;

    QImage image = m_sourceImage;
    if (image.format() != QImage::Format_RGB32
        && image.format() != QImage::Format_ARGB32) {
        image = image.convertToFormat(QImage::Format_RGB32);
    }

    QRect rect = m_visibleRect.isNull()
        ? image.rect()
        : m_visibleRect.intersected(image.rect());
    if (rect.isEmpty()) return;

    m_gridSize = 256;
    const size_t gridCells = static_cast<size_t>(m_gridSize) * m_gridSize;
    m_gridCount.resize(gridCells);
    std::fill(m_gridCount.begin(), m_gridCount.end(), 0u);

    const int totalPixels = rect.width() * rect.height();
    int stride = 1;
    while ((totalPixels / (stride * stride)) > 1'500'000) ++stride;
    if (stride < 1) stride = 1;

    quint32 localPeak = 0;
    for (int y = rect.top(); y <= rect.bottom(); y += stride) {
        const QRgb *scan = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = rect.left(); x <= rect.right(); x += stride) {
            const QRgb px = scan[x];
            const double rN = qRed(px)   / 255.0;
            const double gN = qGreen(px) / 255.0;
            const double bN = qBlue(px)  / 255.0;

            double iVal, qVal;
            rgbToIQ(rN, gN, bN, iVal, qVal);

            double iRot, qRot;
            rotateIQ(iVal, qVal, iRot, qRot);

            const double iS = iRot / kMaxChroma;
            const double qS = qRot / kMaxChroma;

            int binX = static_cast<int>(std::floor((iS + 1.0) * 0.5 * m_gridSize));
            int binY = static_cast<int>(std::floor((1.0 - qS) * 0.5 * m_gridSize));
            binX = std::clamp(binX, 0, m_gridSize - 1);
            binY = std::clamp(binY, 0, m_gridSize - 1);

            const int idx = indexFor(binX, binY);
            ++m_gridCount[idx];
            if (m_gridCount[idx] > localPeak)
                localPeak = m_gridCount[idx];
        }
    }
    m_peakCount = std::max<quint32>(1, localPeak);
}

// ═══════════════════════════════════════════════════════════════════
//  Cached colour-wheel background
// ═══════════════════════════════════════════════════════════════════

void VectorscopeWidget::ensureColorWheelCache(int diameter)
{
    if (m_cacheDiameter == diameter && !m_colorWheelCache.isNull()) return;

    m_colorWheelCache = QImage(diameter, diameter, QImage::Format_ARGB32);
    m_colorWheelCache.fill(Qt::transparent);
    m_cacheDiameter = diameter;

    for (int py = 0; py < diameter; ++py) {
        QRgb *row = reinterpret_cast<QRgb *>(m_colorWheelCache.scanLine(py));
        for (int px = 0; px < diameter; ++px) {
            const double iS = (px + 0.5) / diameter * 2.0 - 1.0;
            const double qS = 1.0 - (py + 0.5) / diameter * 2.0;

            const double dist = std::hypot(iS, qS);
            if (dist > 1.0) continue;

            double iOrig, qOrig;
            rotateIQInv(iS * kMaxChroma, qS * kMaxChroma, iOrig, qOrig);

            double rLN, gLN, bLN;
            iqToRgb(iOrig, qOrig, rLN, gLN, bLN);

            rLN = std::clamp(rLN, 0.0, 1.0);
            gLN = std::clamp(gLN, 0.0, 1.0);
            bLN = std::clamp(bLN, 0.0, 1.0);

            const int r = static_cast<int>(std::sqrt(rLN) * 255.0);
            const int g = static_cast<int>(std::sqrt(gLN) * 255.0);
            const int b = static_cast<int>(std::sqrt(bLN) * 255.0);

            row[px] = qRgba(r, g, b, 255);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════
//  Painting
// ═══════════════════════════════════════════════════════════════════

void VectorscopeWidget::resizeEvent(QResizeEvent * /*event*/)
{
    update();
}

void VectorscopeWidget::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRect rect = this->rect();
    const QColor graphBg(42, 42, 42);
    const QColor graphExt(22, 22, 22);
    const QColor gridColor(70, 70, 70);
    const QColor textColor(180, 180, 180);

    // ── Radial gradient background ────────────────────────────────
    QRadialGradient bgGrad(rect.center(),
                           std::min(rect.width(), rect.height()) * 0.7);
    bgGrad.setColorAt(0.0, graphBg);
    bgGrad.setColorAt(1.0, graphExt);
    painter.fillRect(rect, bgGrad);

    const int margin = 16;
    int side = std::min(rect.width(), rect.height()) - 2 * margin;
    if (side < 40) return;

    const QPointF center(rect.center());
    const double radius = side * 0.5;

    // ── Colour wheel background (cached at 384 px, like darktable) ─
    const int cacheDiam = 384;
    ensureColorWheelCache(cacheDiam);
    const QRectF wheelDest(center.x() - radius, center.y() - radius,
                           side, side);
    painter.setOpacity(0.50);
    painter.drawImage(wheelDest, m_colorWheelCache);
    painter.setOpacity(1.0);

    // ── Grid: concentric circles ──────────────────────────────────
    painter.setPen(QPen(QColor(55, 55, 55), 1, Qt::DotLine));
    painter.setBrush(Qt::NoBrush);
    for (double frac : {0.25, 0.5, 0.75}) {
        painter.drawEllipse(center, radius * frac, radius * frac);
    }

    // Outer circle.
    painter.setPen(QPen(gridColor, 1));
    painter.drawEllipse(center, radius, radius);

    // Cross-hair.
    painter.setPen(QPen(QColor(50, 50, 50), 1, Qt::DotLine));
    painter.drawLine(QPointF(center.x() - radius, center.y()),
                     QPointF(center.x() + radius, center.y()));
    painter.drawLine(QPointF(center.x(), center.y() - radius),
                     QPointF(center.x(), center.y() + radius));

    // ── Hue target nodes and labels ───────────────────────────────
    const double vertexRgb[6][3] = {
        {1,0,0}, {1,1,0}, {0,1,0}, {0,1,1}, {0,0,1}, {1,0,1}
    };
    const char *vertexLabel[6] = {"R", "Yl", "G", "Cy", "B", "Mg"};
    const QColor vertexColor[6] = {
        QColor(220,60,60), QColor(220,200,60), QColor(80,200,80),
        QColor(60,180,180), QColor(80,130,230), QColor(220,80,220)
    };

    QFont smallFont = painter.font();
    smallFont.setPointSizeF(std::max(7.0, smallFont.pointSizeF() - 1.0));
    painter.setFont(smallFont);
    const QFontMetrics fm(smallFont);

    for (int k = 0; k < 6; ++k) {
        double iVal, qVal;
        rgbToIQ(vertexRgb[k][0], vertexRgb[k][1], vertexRgb[k][2], iVal, qVal);
        double iRot, qRot;
        rotateIQ(iVal, qVal, iRot, qRot);
        const double iS = iRot / kMaxChroma;
        const double qS = qRot / kMaxChroma;
        const double angle = std::atan2(-qS, iS);
        const double px = center.x() + radius * std::cos(angle);
        const double py = center.y() - radius * std::sin(angle);

        painter.setPen(QPen(vertexColor[k], 1));
        painter.setBrush(vertexColor[k]);
        painter.drawEllipse(QPointF(px, py), 3.5, 3.5);

        const double lblDist = radius + 6.0;
        const double lx = center.x() + lblDist * std::cos(angle);
        const double ly = center.y() - lblDist * std::sin(angle);
        painter.setPen(textColor);
        painter.setBrush(Qt::NoBrush);
        QRectF tr(lx - 14, ly - fm.height() / 2.0, 28, fm.height());
        painter.drawText(tr, Qt::AlignHCenter | Qt::AlignVCenter,
                         QString::fromLatin1(vertexLabel[k]));
    }

    // ── Data dots ─────────────────────────────────────────────────
    if (m_gridSize > 0 && m_peakCount > 0) {
        QImage overlay(side, side, QImage::Format_ARGB32);
        overlay.fill(Qt::transparent);

        const int ow = overlay.width();
        const int oh = overlay.height();
        const double peak = static_cast<double>(m_peakCount);
        const double tMul = 1.0 - m_transparency;

        for (int by = 0; by < m_gridSize; ++by) {
            for (int bx = 0; bx < m_gridSize; ++bx) {
                const int idx = indexFor(bx, by);
                const quint32 count = m_gridCount[idx];
                if (count == 0) continue;

                // Alpha from density.
                const double frac = std::min(1.0, count / peak);
                const int alpha = std::clamp(
                    static_cast<int>(255.0 * std::sqrt(frac) * tMul), 0, 255);
                if (alpha <= 0) continue;

                // White dots (darktable style) — they glow over the
                // colour wheel via normal alpha blending.
                const int dr = 255, dg = 255, db = 255;

                // Integer cell boundaries (no gaps on resize).
                int startX = (bx * ow) / m_gridSize;
                int endX   = ((bx + 1) * ow) / m_gridSize;
                int startY = (by * oh) / m_gridSize;
                int endY   = ((by + 1) * oh) / m_gridSize;
                if (endX <= startX) endX = startX + 1;
                if (endY <= startY) endY = startY + 1;
                startX = std::clamp(startX, 0, ow);
                endX   = std::clamp(endX,   0, ow);
                startY = std::clamp(startY, 0, oh);
                endY   = std::clamp(endY,   0, oh);

                for (int oy = startY; oy < endY; ++oy) {
                    QRgb *row = reinterpret_cast<QRgb *>(overlay.scanLine(oy));
                    for (int ox = startX; ox < endX; ++ox) {
                        const QRgb prev = row[ox];
                        const int prevA = qAlpha(prev);
                        const int newA = std::min(255, alpha + prevA);
                        if (newA <= 0) continue;
                        const int nr = std::min(255, (qRed(prev)   * prevA + dr * alpha) / 255);
                        const int ng = std::min(255, (qGreen(prev) * prevA + dg * alpha) / 255);
                        const int nb = std::min(255, (qBlue(prev)  * prevA + db * alpha) / 255);
                        row[ox] = qRgba(nr, ng, nb, newA);
                    }
                }
            }
        }

        painter.drawImage(static_cast<int>(center.x() - radius),
                          static_cast<int>(center.y() - radius),
                          overlay);
    }

    // ── Title ─────────────────────────────────────────────────────
    painter.setPen(textColor);
    painter.setFont(smallFont);
    painter.drawText(rect.adjusted(6, 4, -6, -4),
                     Qt::AlignLeft | Qt::AlignTop,
                     QStringLiteral("Vectorscope"));
}
