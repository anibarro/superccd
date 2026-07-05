#ifndef HISTOGRAMWIDGET_H
#define HISTOGRAMWIDGET_H

#include <QImage>
#include <QWidget>

// Displays a standard RGB + luma histogram for the current preview image.
// The image is sampled in 8-bit-per-channel form (matching what the user
// actually sees on screen) and binned into 256 buckets per channel. The
// luma curve uses Rec. 709 weights.
class HistogramWidget : public QWidget
{
    Q_OBJECT

public:
    explicit HistogramWidget(QWidget *parent = nullptr);

    // Provide a new source image. A null/empty image clears the histogram.
    // The widget also receives an optional "visible rect" in image-pixel
    // coordinates; when non-empty, only that sub-rect is sampled.
    void setSourceImage(const QImage &image, const QRect &visibleRect = QRect());
    void clearSourceImage();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void recompute();
    double peak() const;
    double bucket(int channel, int bin) const;

    QImage m_sourceImage;
    QRect m_visibleRect;
    // Cumulative distribution (256 entries) used to compute the peak in
    // log space; reset on every recompute.
    double m_peak = 1.0;
    // Per-channel histogram with logarithmic compression. Channel order:
    // 0 = red, 1 = green, 2 = blue, 3 = luma.
    double m_hist[4][256];
};

#endif // HISTOGRAMWIDGET_H
