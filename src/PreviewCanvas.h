#ifndef PREVIEWCANVAS_H
#define PREVIEWCANVAS_H

#include "PreviewImageProcessing.h"

#include <QImage>
#include <QLabel>

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

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void updateCanvasSize();

    QImage m_sourceImage;
    PreviewAdjustmentValues m_adjustments;
    double m_zoom = 1.0;
    int m_sharpening = 0;
};

#endif
