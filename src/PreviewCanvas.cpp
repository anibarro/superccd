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

void PreviewCanvas::setScene(superccd::SceneImagePtr scene)
{
    m_scene = std::move(scene);
    setMinimumSize(1, 1);
    setText(QString());
    m_whiteBalancePickerVisible = false;
    updateCanvasSize();
    update();
}

void PreviewCanvas::clearScene()
{
    m_scene.reset();
    m_whiteBalancePickerVisible = false;
    setMinimumSize(480, 480);
    resize(480, 480);
    update();
}

void PreviewCanvas::setDisplayState(double zoom, const superccd::EditParams &params)
{
    m_zoom = std::max(zoom, 0.01);
    m_params = params;
    updateCanvasSize();
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
    if (!m_scene || !m_whiteBalancePickerEnabled
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
        .intersected(QRect(0, 0, m_scene->width, m_scene->height));
}

void PreviewCanvas::paintEvent(QPaintEvent *event)
{
    if (!m_scene) {
        QLabel::paintEvent(event);
        return;
    }

    const QRect targetRect = event->rect().intersected(rect());
    if (targetRect.isEmpty()) {
        return;
    }

    // Only the exposed part of the view is developed, so slider feedback stays
    // fast at any zoom.
    const QRectF sourceRect(
        static_cast<double>(targetRect.x()) / m_zoom,
        static_cast<double>(targetRect.y()) / m_zoom,
        static_cast<double>(targetRect.width()) / m_zoom,
        static_cast<double>(targetRect.height()) / m_zoom);
    const QImage tile = PreviewImageProcessing::renderRegion8(
        *m_scene, m_params, sourceRect, targetRect.size());

    QPainter painter(this);
    painter.drawImage(targetRect.topLeft(), tile);

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
    if (!m_scene) {
        return;
    }

    const QSize scaledSize(
        std::max(1, static_cast<int>(std::lround(m_scene->width * m_zoom))),
        std::max(1, static_cast<int>(std::lround(m_scene->height * m_zoom))));
    if (size() != scaledSize) {
        resize(scaledSize);
    }
}
