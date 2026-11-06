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

    void setScene(superccd::SceneImagePtr scene);
    void clearScene();
    void setDisplayState(double zoom, const superccd::EditParams &params);
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

    superccd::SceneImagePtr m_scene;
    superccd::EditParams m_params;
    double m_zoom = 1.0;
    bool m_whiteBalancePickerEnabled = false;
    bool m_whiteBalancePickerVisible = false;
    QPointF m_whiteBalancePickerPosition;
    int m_whiteBalancePickerSize = 64;
};

#endif
