#ifndef TRANSITIONCURVEWIDGET_H
#define TRANSITIONCURVEWIDGET_H

#include <QWidget>

// Visualizes the S->R merge curve used by SuperCCDProcessor when highlights
// are blended between the small-pixel (S) and large-pixel (R) planes.
//
// The widget shows the weight applied to the R image (blendT) as a function of
// the normalized S brightness. It mirrors the exact math from
// SuperCCDProcessor.cpp::mergePixels (rTransitionDelay / rTransitionSmoothness
// branch, with sDrivenHighlightsOnly == false) so the visual matches what
// the actual pipeline does.
class TransitionCurveWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TransitionCurveWidget(QWidget *parent = nullptr);

    // start in [0, 1]: S-brightness where the S->R merge starts.
    // delay in [0, 1]: width of the S->R transition in normalized S units
    //                  (0 = zero-width, 1 = up to 0.4 wide).
    // smoothness in [0, 1]: ease-in exponent multiplier. 0 = straight line,
    //                       1 = sharp ~90deg.
    void setParameters(double start, double delay, double smoothness);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    double m_start = 0.75;
    double m_delay = 0.20;
    double m_smoothness = 0.5;
};

#endif // TRANSITIONCURVEWIDGET_H
