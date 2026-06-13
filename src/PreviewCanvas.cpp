#include "PreviewCanvas.h"

#include <QPaintEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

PreviewCanvas::PreviewCanvas(QWidget *parent)
    : QLabel(parent)
{
    setAlignment(Qt::AlignCenter);
    setMinimumSize(480, 480);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void PreviewCanvas::setSourceImage(const QImage &image)
{
    m_sourceImage = image;
    setMinimumSize(1, 1);
    setText(QString());
    updateCanvasSize();
    update();
}

void PreviewCanvas::clearSourceImage()
{
    m_sourceImage = QImage();
    m_sharpening = 0;
    setMinimumSize(480, 480);
    resize(480, 480);
    update();
}

void PreviewCanvas::setDisplayState(double zoom,
                                    const PreviewAdjustmentValues &adjustments,
                                    int sharpening)
{
    m_zoom = std::max(zoom, 0.01);
    m_adjustments = adjustments;
    m_sharpening = std::clamp(sharpening, 0, 100);
    updateCanvasSize();
    update();
}

void PreviewCanvas::setSharpening(int sharpening)
{
    m_sharpening = std::clamp(sharpening, 0, 100);
    update();
}

void PreviewCanvas::paintEvent(QPaintEvent *event)
{
    if (m_sourceImage.isNull()) {
        QLabel::paintEvent(event);
        return;
    }

    const QRect targetRect = event->rect().intersected(rect());
    if (targetRect.isEmpty()) {
        return;
    }

    const int margin = m_sharpening > 0 ? 2 : 1;
    const QRect renderRect =
        targetRect.adjusted(-margin, -margin, margin, margin).intersected(rect());
    const QRectF sourceRect(
        static_cast<double>(renderRect.x()) / m_zoom,
        static_cast<double>(renderRect.y()) / m_zoom,
        static_cast<double>(renderRect.width()) / m_zoom,
        static_cast<double>(renderRect.height()) / m_zoom);

    QImage tile(renderRect.size(), QImage::Format_RGB32);
    if (tile.isNull()) {
        return;
    }
    tile.fill(Qt::black);

    {
        QPainter tilePainter(&tile);
        tilePainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        tilePainter.drawImage(QRectF(tile.rect()), m_sourceImage, sourceRect);
    }

    tile = PreviewImageProcessing::applyDisplayAdjustments(tile, m_adjustments);
    if (m_sharpening > 0) {
        PreviewImageProcessing::applyLumaSharpening8(tile, m_sharpening);
    }

    const QRect tileSource(targetRect.topLeft() - renderRect.topLeft(),
                           targetRect.size());
    QPainter painter(this);
    painter.drawImage(targetRect.topLeft(), tile, tileSource);
}

void PreviewCanvas::updateCanvasSize()
{
    if (m_sourceImage.isNull()) {
        return;
    }

    const QSize scaledSize(
        std::max(1, static_cast<int>(std::lround(m_sourceImage.width() * m_zoom))),
        std::max(1, static_cast<int>(std::lround(m_sourceImage.height() * m_zoom))));
    if (size() != scaledSize) {
        resize(scaledSize);
    }
}
