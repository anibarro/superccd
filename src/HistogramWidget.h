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
    // Visualization layout, mirroring WaveformWidget.
    enum Mode {
        AllChannels,   // single plot with R+G+B+luma overlaid
        RgbSplit,      // three side-by-side plots, one per RGB channel
        LumaOnly,      // single plot with the luma channel only
    };
    Q_ENUM(Mode)

    explicit HistogramWidget(QWidget *parent = nullptr);

    // Provide a new source image. A null/empty image clears the histogram.
    // The widget also receives an optional "visible rect" in image-pixel
    // coordinates; when non-empty, only that sub-rect is sampled.
    void setSourceImage(const QImage &image, const QRect &visibleRect = QRect());
    void clearSourceImage();

    void setMode(Mode mode);
    Mode mode() const { return m_mode; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void recompute();
    double peak() const;
    double bucket(int channel, int bin) const;
    // Renders the given channel indices into a sub-rectangle of the widget.
    // channelColors[i] is the drawing color for channelIndices[i].
    void renderChannels(QPainter &painter,
                        const QRect &subRect,
                        const int *channelIndices,
                        const QColor *channelColors,
                        int channelCount,
                        const QColor &gridColor,
                        const QColor &axisColor,
                        const QColor &textColor);

    QImage m_sourceImage;
    QRect m_visibleRect;
    // Cumulative distribution (256 entries) used to compute the peak in
    // log space; reset on every recompute.
    double m_peak = 1.0;
    // Per-channel histogram with logarithmic compression. Channel order:
    // 0 = red, 1 = green, 2 = blue, 3 = luma.
    double m_hist[4][256];
    Mode m_mode = AllChannels;
    // True when m_hist / m_peak correspond to the data currently in
    // m_sourceImage. Lets setSourceImage() / setMode() skip the
    // (relatively expensive) per-pixel sampling pass when nothing has
    // changed — this happens a lot on the Raspberry Pi when the preview
    // pipeline re-pushes the cached image on every visible-rect refresh
    // (scroll, zoom, "meter visible area only" toggles).
    bool m_histogramValid = false;
};

#endif // HISTOGRAMWIDGET_H
