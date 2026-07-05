#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QString>

class QListWidget;
class QLineEdit;
class QRadioButton;
class QPushButton;
class QLabel;
class QTimer;
class QSlider;
class QScrollArea;
class QCheckBox;
class QComboBox;
class QButtonGroup;
class QCloseEvent;
class QWidget;
class QSpinBox;
class QDoubleSpinBox;
class PreviewCanvas;
class TransitionCurveWidget;
class ExposureToolsWindow;

#include "SuperCCDProcessor.h"
#include "PreviewImageProcessing.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onAddFiles();
    void onRemoveSelectedFiles();
    void onSelectOutputFolder();
    void onConvertCurrent();
    void onConvertAll();
    void onUpdatePreview();
    void onExportPreview();
    void onPreviewZoomChanged(int value);
    void onPreviewExposureChanged(int value);
    void onPreviewWhiteBalanceChanged(int value);
    void onPreviewTintChanged(int value);
    void onWhiteBalancePickerToggled(bool enabled);
    void onPreviewMethodChanged();
    PreviewMethod currentPreviewMethod() const;
    void onPreviewGammaChanged(int value);
    void onPreviewContrastChanged(int value);
    void onPreviewShadowsChanged(int value);
    void onPreviewShadowRangeChanged(int value);
    void onPreviewSaturationChanged(int value);
    void onPreviewSharpeningChanged(int value);
    void onPreviewHighlightCompressionChanged(int value);
    void onSaveDefaults();
    void onResetDefaults();
    void onAutoPreviewTimer();
    void showPreviewWindow();
    void onShowExposureToolsToggled(bool enabled);

private:
    void updateControls(bool busy);
    QImage buildAdjustedPreviewImage16() const;
    QImage buildAdjustedDisplayImage8() const;
    void updatePreviewDisplay(bool preserveViewport = true);
    void updateSharpenedPreviewDisplay();
    void showStatus(const QString &message);
    QRect currentPreviewVisibleRect() const;
    void pushExposureToolsFromCache();
    ConversionSettings currentSettings() const;
    void applyParameterSettings(const ConversionSettings &settings);
    void loadSavedDefaults();
    void saveCurrentDefaults() const;
    void queueAutoPreview();
    void applyWhiteBalancePickerSample();
    QPointF previewCanvasPosition(QObject *watched, const QPointF &position) const;
    bool hasCurrentPreview() const;
    bool convertOneFile(const QString &inputPath,
                        const QString &outputFolder,
                        const ConversionSettings &settings,
                        QString &error);

    QListWidget *m_fileList;
    QLineEdit *m_outputFolder;
    QSlider *m_rTransitionStartSlider;
    QSlider *m_rTransitionDelaySlider;
    QSlider *m_rTransitionSmoothnessSlider;
    QSlider *m_previewZoomSlider;
    QSlider *m_previewExposureSlider;
    QSlider *m_previewWhiteBalanceSlider;
    QSlider *m_previewTintSlider;
    QPushButton *m_whiteBalancePickerButton;
    QSlider *m_previewGammaSlider;
    QSlider *m_previewContrastSlider;
    QSlider *m_previewShadowsSlider;
    QSlider *m_previewShadowRangeSlider;
    QSlider *m_previewSaturationSlider;
    QSlider *m_previewSharpeningSlider;
    QSlider *m_previewHighlightCompressionSlider;
    QComboBox *m_previewRotationCombo;
    QWidget *m_previewMethodRow;
    QRadioButton *m_previewMethodReconstructionButton;
    QRadioButton *m_previewMethodAmazeButton;
    QButtonGroup *m_previewMethodGroup;
    QCheckBox *m_correctPreviewOutliersCheckBox;
    QCheckBox *m_autoPreviewCheckBox;
    QCheckBox *m_showExposureToolsCheckBox;
    ExposureToolsWindow *m_exposureToolsWindow;
    QWidget *m_previewWindow;
    QScrollArea *m_previewScrollArea;
    PreviewCanvas *m_previewLabel;
    QPushButton *m_showPreviewButton;
    QPushButton *m_previewButton;
    QPushButton *m_exportPreviewButton;
    QPushButton *m_convertCurrentButton;
    QPushButton *m_convertAllButton;
    QCheckBox *m_exportPlaneImagesCheckBox;
    QPushButton *m_resetDefaultsButton;
    QPushButton *m_saveDefaultsButton;
    QLabel *m_statusLabel;
    QTimer *m_statusClearTimer;
    QTimer *m_autoPreviewTimer;
    QTimer *m_previewSharpeningTimer;
    // Coalesces the (relatively expensive) "build the adjusted 8-bit
    // image and push it to the exposure tools" path so it runs at most
    // once per ~75 ms while the user is dragging a preview slider.
    // Without this, every valueChanged tick runs the full
    // applyDisplayAdjustments() pipeline over the whole preview, which
    // is the dominant cost on the Raspberry Pi.
    QTimer *m_exposureScopeTimer;
    // The cached set of adjustment values used to compute
    // m_adjustedDisplayImage. Used to short-circuit a re-push when
    // nothing has actually changed since the last push (e.g. when the
    // debounce timer fires after only scrollbar / zoom events).
    QImage m_cachedScopeAdjustmentsImage;
    PreviewAdjustmentValues m_lastPushedAdjustments;
    QSpinBox *m_rTransitionStartSpinBox;
    QSpinBox *m_rTransitionDelaySpinBox;
    QSpinBox *m_rTransitionSmoothnessSpinBox;
    TransitionCurveWidget *m_transitionCurveWidget;
    QSpinBox *m_previewZoomSpinBox;
    QDoubleSpinBox *m_previewExposureSpinBox;
    QSpinBox *m_previewWhiteBalanceSpinBox;
    QSpinBox *m_previewTintSpinBox;
    QDoubleSpinBox *m_previewGammaSpinBox;
    QSpinBox *m_previewContrastSpinBox;
    QSpinBox *m_previewShadowsSpinBox;
    QSpinBox *m_previewShadowRangeSpinBox;
    QSpinBox *m_previewSaturationSpinBox;
    QSpinBox *m_previewSharpeningSpinBox;
    QSpinBox *m_previewHighlightCompressionSpinBox;
    QImage m_currentPreviewImage;
    QImage m_adjustedDisplayImage;
    bool m_previewDragging = false;
    QPoint m_lastPreviewDragPos;
    QString m_lastPreviewedInputPath;
    bool m_busy = false;
    SuperCCDProcessor m_processor;
};

#endif // MAINWINDOW_H
