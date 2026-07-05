#ifndef WAVEFORMWIDGET_H
#define WAVEFORMWIDGET_H

#include <QImage>
#include <QRect>
#include <QVector>
#include <QWidget>

// Displays a classic waveform monitor. For each column of the source
// image (the X axis of the waveform) we count, for each bin in
// [0, 255], how many pixels in that column landed in that bin. The
// result is drawn with brightness proportional to the count.
//
// Internal channel layout in m_counts:
//   0 = red, 1 = green, 2 = blue, 3 = luma (Rec. 709).
class WaveformWidget : public QWidget
{
    Q_OBJECT

public:
    // Visualization layout for the waveform tab.
    enum Mode {
        AllChannels,   // single plot with R+G+B+luma overlaid
        RgbSplit,      // three side-by-side plots, one per RGB channel
        LumaOnly,      // single plot with the luma channel only
    };
    Q_ENUM(Mode)

    explicit WaveformWidget(QWidget *parent = nullptr);

    void setSourceImage(const QImage &image, const QRect &visibleRect = QRect());
    void clearSourceImage();

    void setMode(Mode mode);
    Mode mode() const { return m_mode; }

    // Per-pixel alpha is multiplied by this value before being written to
    // the overlay, letting the user dial in how transparent the waveform
    // looks. 0.0 = fully transparent, 1.0 = fully opaque (default). The
    // slider in the Waveform tab drives this value.
    void setTransparency(double transparency);
    double transparency() const { return m_transparency; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void recompute();
    // Renders the given channel indices into a sub-rectangle of the widget.
    // channelColors[i] is the drawing color for channelIndices[i].
    void renderChannels(QPainter &painter,
                        const QRect &subRect,
                        const int *channelIndices,
                        const QColor *channelColors,
                        int channelCount);
    void drawAxisDecorations(QPainter &painter,
                             const QRect &subRect,
                             const QString &xTitle,
                             const QString &yTitle);
    void drawNoPreviewMessage(QPainter &painter, const QRect &rect);

    QImage m_sourceImage;
    QRect m_visibleRect;
    // Per-column, per-bucket counts: 4 channels, N columns (width of source
    // region), 256 buckets. Stored as a single contiguous buffer because
    // QVector<...> of QVector would be wasteful for a 12 MP image.
    QVector<quint32> m_counts; // size = 4 * columns * 256
    // Per-column peak (the max of any (col, bin) cell in that column
    // across all 4 channels). Using a per-column reference instead of a
    // single global peak means a single bright column doesn't dim
    // every other column in the waveform — each column self-normalizes.
    QVector<quint32> m_columnPeak; // size = columns
    int m_columns = 0;
    // Global peak across all (col, bin) cells and all channels, kept
    // around for the clipping/crush indicator so it can show a
    // meaningful red/blue bar even when the per-column reference is
    // small.
    quint32 m_globalPeak = 1;
    Mode m_mode = RgbSplit;
    double m_transparency = 0.0; // 0 = opaque (no transparency), 1 = invisible
};

#endif // WAVEFORMWIDGET_H
