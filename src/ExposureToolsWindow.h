#ifndef EXPOSURETOOLSWINDOW_H
#define EXPOSURETOOLSWINDOW_H

#include <QImage>
#include <QRect>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QSlider;
class QSpinBox;
class QTabWidget;
class HistogramWidget;
class WaveformWidget;
class VectorscopeWidget;

// A detached top-level window that hosts the exposure analysis tools
// (Histogram, RGB + Luma Waveform, Vectorscope with skin-tone guide).
// Each tool lives in its own tab. A checkbox at the bottom of the
// window toggles between metering the full preview image and metering
// only the sub-rect currently visible in the preview window.
class ExposureToolsWindow : public QWidget
{
    Q_OBJECT

public:
    explicit ExposureToolsWindow(QWidget *parent = nullptr);

    // Push a new image into every tool. visibleRect is the sub-rect of
    // the image currently visible in the preview window, in image
    // coordinates; it is only used when the user has enabled the
    // "meter visible area only" checkbox.
    void setSourceImage(const QImage &image, const QRect &visibleRect);

    // True when only the visible (in-preview-window) rect should be
    // metered.
    bool metersVisibleAreaOnly() const;

    QCheckBox *meterVisibleAreaCheckBox() const { return m_meterVisibleCheckBox; }

private:
    QTabWidget *m_tabWidget;
    HistogramWidget *m_histogram;
    WaveformWidget *m_waveform;
    VectorscopeWidget *m_vectorscope;
    QCheckBox *m_meterVisibleCheckBox;
    QComboBox *m_waveformModeCombo;
    QSlider *m_waveformTransparencySlider;
    QSpinBox *m_waveformTransparencySpinBox;
    QSlider *m_vectorscopeTransparencySlider;
    QSpinBox *m_vectorscopeTransparencySpinBox;
};

#endif // EXPOSURETOOLSWINDOW_H
