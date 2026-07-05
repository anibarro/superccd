#ifndef VECTORSCOPEWIDGET_H
#define VECTORSCOPEWIDGET_H

#include <QImage>
#include <QRect>
#include <QVector>
#include <QWidget>

// Displays a vectorscope inspired by darktable's implementation.
// Pixels are converted to (I, Q) chromaticity and binned into a density
// grid. A full color wheel background is pre-rendered and cached, and
// data dots take on the hue of their position with brightness driven by
// pixel density (hard-light-style blending).
class VectorscopeWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VectorscopeWidget(QWidget *parent = nullptr);

    void setSourceImage(const QImage &image, const QRect &visibleRect = QRect());
    void clearSourceImage();

    void setTransparency(double transparency);
    double transparency() const { return m_transparency; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void recompute();
    void ensureColorWheelCache(int diameter);
    int indexFor(int binX, int binY) const;

    QImage m_sourceImage;
    QRect m_visibleRect;

    // Cached color wheel background at internal resolution.
    QImage m_colorWheelCache;
    int m_cacheDiameter = 0;

    int m_gridSize = 0;
    // Per-bin pixel hit count (density).
    QVector<quint32> m_gridCount;
    quint32 m_peakCount = 1;
    double m_transparency = 0.0;
};

#endif // VECTORSCOPEWIDGET_H
