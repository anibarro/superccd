#include "PreviewCanvas.h"

#include <QPaintEvent>
#include <QPainter>
#include <QPen>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kMinimumWhiteBalancePickerSize = 16;
constexpr int kMaximumWhiteBalancePickerSize = 256;
constexpr int kWhiteBalancePickerSizeStep = 8;
}

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
    m_whiteBalancePickerVisible = false;
    updateCanvasSize();
    update();
}

void PreviewCanvas::clearSourceImage()
{
    m_sourceImage = QImage();
    m_sharpening = 0;
    m_whiteBalancePickerVisible = false;
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

void PreviewCanvas::setWhiteBalancePickerEnabled(bool enabled)
{
    const QRect oldRect = whiteBalancePickerCanvasRect();
    m_whiteBalancePickerEnabled = enabled;
    if (!enabled) {
        m_whiteBalancePickerVisible = false;
    }
    updateWhiteBalancePickerArea(oldRect);
}

void PreviewCanvas::setWhiteBalancePickerPosition(const QPointF &position)
{
    const QRect oldRect = whiteBalancePickerCanvasRect();
    m_whiteBalancePickerPosition = position;
    m_whiteBalancePickerVisible =
        m_whiteBalancePickerEnabled && rect().contains(position.toPoint());
    updateWhiteBalancePickerArea(oldRect);
}

void PreviewCanvas::hideWhiteBalancePicker()
{
    const QRect oldRect = whiteBalancePickerCanvasRect();
    m_whiteBalancePickerVisible = false;
    updateWhiteBalancePickerArea(oldRect);
}

void PreviewCanvas::resizeWhiteBalancePicker(int wheelDelta)
{
    if (!m_whiteBalancePickerEnabled || wheelDelta == 0) {
        return;
    }

    const QRect oldRect = whiteBalancePickerCanvasRect();
    const int direction = wheelDelta > 0 ? 1 : -1;
    m_whiteBalancePickerSize = std::clamp(
        m_whiteBalancePickerSize + direction * kWhiteBalancePickerSizeStep,
        kMinimumWhiteBalancePickerSize,
        kMaximumWhiteBalancePickerSize);
    updateWhiteBalancePickerArea(oldRect);
}

QRect PreviewCanvas::whiteBalancePickerSourceRect() const
{
    if (m_sourceImage.isNull() || !m_whiteBalancePickerEnabled
        || !m_whiteBalancePickerVisible || m_zoom <= 0.0) {
        return QRect();
    }

    const QRect canvasRect = whiteBalancePickerCanvasRect().intersected(rect());
    if (canvasRect.isEmpty()) {
        return QRect();
    }

    const int left = static_cast<int>(std::floor(canvasRect.left() / m_zoom));
    const int top = static_cast<int>(std::floor(canvasRect.top() / m_zoom));
    const int right = static_cast<int>(
        std::ceil((canvasRect.right() + 1.0) / m_zoom));
    const int bottom = static_cast<int>(
        std::ceil((canvasRect.bottom() + 1.0) / m_zoom));
    return QRect(QPoint(left, top), QPoint(right - 1, bottom - 1))
        .intersected(m_sourceImage.rect());
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

    if (m_whiteBalancePickerEnabled && m_whiteBalancePickerVisible) {
        const QRect pickerRect = whiteBalancePickerCanvasRect();
        painter.setBrush(QColor(255, 255, 255, 20));
        painter.setPen(QPen(Qt::black, 3));
        painter.drawRect(pickerRect);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(Qt::white, 1, Qt::DashLine));
        painter.drawRect(pickerRect);
    }
}

QRect PreviewCanvas::whiteBalancePickerCanvasRect() const
{
    if (!m_whiteBalancePickerEnabled || !m_whiteBalancePickerVisible) {
        return QRect();
    }

    const int left = static_cast<int>(
        std::lround(m_whiteBalancePickerPosition.x() - m_whiteBalancePickerSize * 0.5));
    const int top = static_cast<int>(
        std::lround(m_whiteBalancePickerPosition.y() - m_whiteBalancePickerSize * 0.5));
    return QRect(left, top, m_whiteBalancePickerSize, m_whiteBalancePickerSize);
}

void PreviewCanvas::updateWhiteBalancePickerArea(const QRect &oldRect)
{
    const QRect newRect = whiteBalancePickerCanvasRect();
    if (!oldRect.isEmpty()) {
        update(oldRect.adjusted(-3, -3, 3, 3));
    }
    if (!newRect.isEmpty()) {
        update(newRect.adjusted(-3, -3, 3, 3));
    }
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
