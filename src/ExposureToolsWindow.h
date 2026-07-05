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

// A detached top-level window that hosts the exposure analysis tools
// (Histogram, RGB + Luma Waveform). Each tool lives in its own tab. A
// checkbox at the bottom of the window toggles between metering the
// full preview image and metering only the sub-rect currently visible
// in the preview window.
class ExposureToolsWindow : public QWidget
{
    Q_OBJECT

public:
    explicit ExposureToolsWindow(QWidget *parent = nullptr);

    // Push a new image into the exposure tools. visibleRect is the
    // sub-rect of the image currently visible in the preview window, in
    // image coordinates; it is only used when the user has enabled the
    // "meter visible area only" checkbox.
    //
    // Performance contract: if the window is not visible (e.g. the user
    // has closed the floating window and is still tweaking preview
    // controls) the call is a cheap no-op and the widgets do not
    // recompute. While the window is visible, only the currently
    // selected tab is updated — the other tab is updated lazily the
    // next time the user switches to it.
    void setSourceImage(const QImage &image, const QRect &visibleRect);

    // True when only the visible (in-preview-window) rect should be
    // metered.
    bool metersVisibleAreaOnly() const;

    QCheckBox *meterVisibleAreaCheckBox() const { return m_meterVisibleCheckBox; }

protected:
    // Re-push the cached image to the tab that just became active. The
    // user may have switched to a tab we skipped during a previous
    // setSourceImage() because it was inactive; we want that tab to
    // show up-to-date data immediately.
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    // Pushes the cached source image to a single tab.
    void pushToActiveTab();
    // Pushes the cached source image to a specific tab by widget pointer.
    void pushToWidget(QWidget *widget, const QRect &visibleRect);

    QTabWidget *m_tabWidget;
    HistogramWidget *m_histogram;
    WaveformWidget *m_waveform;
    QCheckBox *m_meterVisibleCheckBox;
    QComboBox *m_waveformModeCombo;
    QSlider *m_waveformTransparencySlider;
    QSpinBox *m_waveformTransparencySpinBox;
    QComboBox *m_histogramModeCombo;

    // Cache of the most recent source image / visible rect so we can
    // re-push them when the window becomes visible again or when the
    // user switches to a tab that was skipped because it was inactive.
    QImage m_cachedImage;
    QRect m_cachedVisibleRect;
};

#endif // EXPOSURETOOLSWINDOW_H
