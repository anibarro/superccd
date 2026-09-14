#ifndef PREVIEWCANVAS_H
#define PREVIEWCANVAS_H

#include "PreviewImageProcessing.h"

#include <QImage>
#include <QLabel>
#include <QPointF>

class PreviewCanvas : public QLabel
{
public:
    explicit PreviewCanvas(QWidget *parent = nullptr);

    void setSourceImage(const QImage &image);
    void clearSourceImage();
    void setDisplayState(double zoom,
                         const PreviewAdjustmentValues &adjustments,
                         int sharpening);
    void setSharpening(int sharpening);
    void setWhiteBalancePickerEnabled(bool enabled);
    void setWhiteBalancePickerPosition(const QPointF &position);
    void hideWhiteBalancePicker();
    void resizeWhiteBalancePicker(int wheelDelta);
    QRect whiteBalancePickerSourceRect() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QRect whiteBalancePickerCanvasRect() const;
    void updateWhiteBalancePickerArea(const QRect &oldRect);
    void updateCanvasSize();

    QImage m_sourceImage;
    PreviewAdjustmentValues m_adjustments;
    double m_zoom = 1.0;
    int m_sharpening = 0;
    bool m_whiteBalancePickerEnabled = false;
    bool m_whiteBalancePickerVisible = false;
    QPointF m_whiteBalancePickerPosition;
    int m_whiteBalancePickerSize = 64;
};

#endif
