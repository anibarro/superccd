#include "TransitionCurveWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPalette>
#include <QFontMetrics>
#include <algorithm>
#include <cmath>

namespace {
// Mirrors the math from SuperCCDProcessor.cpp for the S->R merge (the
// !sDrivenHighlightsOnly branch, which is what the Transition Settings
// sliders control in the GUI).
double blendTAt(double normalizedS, double start, double delay, double smoothness)
{
    const double startClamped = std::clamp(start, 0.0, 1.0);
    const double delayClamped = std::clamp(delay, 0.0, 1.0);
    const double smoothnessClamped = std::clamp(smoothness, 0.0, 1.0);

    // blendStart / blendEnd derived from the slider parameters.
    // Mirrors the logic in SuperCCDProcessor.cpp so the graph matches
    // the actual conversion behavior. The ceiling matches the
    // slider's fine-control zone in MainWindow.cpp.
    constexpr double kMergeStartFullWidthCeiling = 0.9985;
    constexpr double kMaxWidth = 0.40;
    double widthDecay = 1.0;
    if (startClamped > kMergeStartFullWidthCeiling) {
        const double u = (startClamped - kMergeStartFullWidthCeiling)
                         / (1.0 - kMergeStartFullWidthCeiling);
        const double s = u * u * (3.0 - 2.0 * u);
        widthDecay = 1.0 - s;
    }
    const double width = kMaxWidth * delayClamped * widthDecay;
    double blendStart;
    double blendEnd;
    if (width < 1e-6) {
        // Defensive: a 0 width here means "no merge at all" -> curve
        // stays at 0 across the whole S range.
        blendStart = 1.0;
        blendEnd = 1.0;
    } else {
        blendStart = std::clamp(startClamped, 0.0, 0.999);
        blendEnd = std::clamp(blendStart + width, blendStart + 0.002, 1.0);
    }

    if (normalizedS <= blendStart) {
        return 0.0;
    }
    if (normalizedS >= blendEnd) {
        return 1.0;
    }

    const double t = (normalizedS - blendStart) / (blendEnd - blendStart);
    // Ease-in / exponential shape: blendT = t ^ k, with
    // k = exp(GAMMA * smoothness). smoothness=0 -> k=1 (straight line),
    // smoothness=1 -> k~exp(2.0)~7.4 (sharp ~90deg).
    constexpr double kEaseInGamma = 2.0;
    return std::pow(std::max(t, 0.0), std::exp(kEaseInGamma * smoothnessClamped));
}
} // namespace

TransitionCurveWidget::TransitionCurveWidget(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumHeight(140);
    // Reasonable default that fits in the right-hand panel.
    setMinimumWidth(220);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
}

QSize TransitionCurveWidget::sizeHint() const
{
    return QSize(260, 160);
}

QSize TransitionCurveWidget::minimumSizeHint() const
{
    return QSize(180, 140);
}

void TransitionCurveWidget::setParameters(double start, double delay, double smoothness)
{
    if (m_start == start && m_delay == delay && m_smoothness == smoothness) {
        return;
    }
    m_start = start;
    m_delay = delay;
    m_smoothness = smoothness;
    update();
}

void TransitionCurveWidget::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRect rect = this->rect();

    // Background — match the rest of the dark UI used by the app.
    const QColor background(32, 32, 32);
    const QColor gridColor(80, 80, 80);
    const QColor axisColor(160, 160, 160);
    const QColor curveColor(120, 200, 255);
    const QColor sLineColor(120, 220, 140);
    const QColor rLineColor(255, 140, 140);
    const QColor textColor(210, 210, 210);

    painter.fillRect(rect, background);

    // Reserve a margin for axis labels. We need enough room for the X tick
    // labels and the X title on a separate line, and enough left room for the
    // Y tick labels plus the rotated "R weight" title.
    const int marginLeft = 44;
    const int marginRight = 10;
    const int marginTop = 8;
    const int marginBottom = 36;

    const QRect plotRect = rect.adjusted(marginLeft, marginTop, -marginRight, -marginBottom);
    if (plotRect.width() <= 2 || plotRect.height() <= 2) {
        return;
    }

    // Plot border.
    painter.setPen(QPen(gridColor, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(plotRect);

    // Grid lines (every 0.25 on each axis).
    painter.setPen(QPen(QColor(60, 60, 60), 1, Qt::DotLine));
    for (int i = 1; i < 4; ++i) {
        const double frac = i / 4.0;
        const int x = plotRect.left() + static_cast<int>(frac * plotRect.width());
        painter.drawLine(x, plotRect.top(), x, plotRect.bottom());
        const int y = plotRect.bottom() - static_cast<int>(frac * plotRect.height());
        painter.drawLine(plotRect.left(), y, plotRect.right(), y);
    }

    // Axis labels.
    painter.setPen(QPen(textColor, 1));
    QFont smallFont = painter.font();
    smallFont.setPointSizeF(std::max(7.0, smallFont.pointSizeF() - 1.0));
    painter.setFont(smallFont);
    const QFontMetrics fm(smallFont);

    // X axis: 0, 0.5, 1.0.
    const QString xLabels[] = {QStringLiteral("0"), QStringLiteral("0.5"), QStringLiteral("1")};
    const double xFracs[] = {0.0, 0.5, 1.0};
    for (int i = 0; i < 3; ++i) {
        const int x = plotRect.left() + static_cast<int>(xFracs[i] * plotRect.width());
        const QRect textRect(x - 20, plotRect.bottom() + 2, 40, fm.height());
        painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop, xLabels[i]);
    }
    // Y axis: 0, 0.5, 1.0.
    const QString yLabels[] = {QStringLiteral("0"), QStringLiteral("0.5"), QStringLiteral("1")};
    for (int i = 0; i < 3; ++i) {
        const double frac = i / 2.0;
        const int y = plotRect.bottom() - static_cast<int>(frac * plotRect.height());
        const QRect textRect(2, y - fm.height() / 2, marginLeft - 4, fm.height());
        painter.drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, yLabels[i]);
    }

    // Axis titles.
    const QString xTitle = tr("S brightness");
    const QString yTitle = tr("R weight");
    const QRect xTitleRect(plotRect.left(), plotRect.bottom() + fm.height() + 3,
                           plotRect.width(), fm.height());
    painter.drawText(xTitleRect, Qt::AlignHCenter | Qt::AlignTop, xTitle);

    // Y title drawn vertically. Centered on the plot rectangle, pushed in
    // from the left edge so the rotated text doesn't sit on top of the Y
    // tick labels.
    painter.save();
    painter.translate(fm.height() / 2 + 2, plotRect.top() + plotRect.height() / 2);
    painter.rotate(-90);
    const int yTitleWidth = fm.horizontalAdvance(yTitle);
    const QRect yTitleRect(-plotRect.height() / 2, -yTitleWidth / 2,
                           plotRect.height(), yTitleWidth);
    painter.drawText(yTitleRect, Qt::AlignHCenter | Qt::AlignVCenter, yTitle);
    painter.restore();

    auto xToScreen = [&](double x) {
        return plotRect.left()
               + static_cast<int>(std::clamp(x, 0.0, 1.0) * plotRect.width());
    };
    auto yToScreen = [&](double y) {
        return plotRect.bottom()
               - static_cast<int>(std::clamp(y, 0.0, 1.0) * plotRect.height());
    };

    // Reference "S only" and "R only" horizontal lines for context.
    painter.setPen(QPen(sLineColor, 1, Qt::DashLine));
    painter.drawLine(plotRect.left(), yToScreen(0.0), plotRect.right(), yToScreen(0.0));
    painter.setPen(QPen(rLineColor, 1, Qt::DashLine));
    painter.drawLine(plotRect.left(), yToScreen(1.0), plotRect.right(), yToScreen(1.0));

    // Build the curve as a smooth path.
    QPainterPath curvePath;
    constexpr int kSamples = 121; // 0.0 .. 1.0 step 1/120
    bool started = false;
    for (int i = 0; i < kSamples; ++i) {
        const double s = i / static_cast<double>(kSamples - 1);
        const double w = blendTAt(s, m_start, m_delay, m_smoothness);
        const int sx = xToScreen(s);
        const int sy = yToScreen(w);
        if (!started) {
            curvePath.moveTo(sx, sy);
            started = true;
        } else {
            curvePath.lineTo(sx, sy);
        }
    }

    // Filled area under the curve (subtle highlight).
    QPainterPath area = curvePath;
    area.lineTo(xToScreen(1.0), yToScreen(0.0));
    area.lineTo(xToScreen(0.0), yToScreen(0.0));
    area.closeSubpath();
    QColor fillColor = curveColor;
    fillColor.setAlpha(45);
    painter.fillPath(area, fillColor);

    // Curve outline.
    painter.setPen(QPen(curveColor, 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(curvePath);

    // Axis ticks cosmetic.
    painter.setPen(QPen(axisColor, 1));
    painter.drawLine(plotRect.left(), plotRect.top(),
                     plotRect.left(), plotRect.bottom());
    painter.drawLine(plotRect.left(), plotRect.bottom(),
                     plotRect.right(), plotRect.bottom());
}
