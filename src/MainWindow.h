#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QString>
#include <QJsonObject>

#include <array>

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
class QVBoxLayout;
class QWidget;
class QSpinBox;
class QDoubleSpinBox;
class PreviewCanvas;
class TransitionCurveWidget;
class ExposureToolsWindow;
class PresetManager;
class ImageSettingsManager;

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
    void onWhiteBalancePickerToggled(bool enabled);
    void onPreviewMethodChanged();
    PreviewMethod currentPreviewMethod() const;
    void onDevelopChanged();
    void onPresetSelected(int index);
    void onSavePresetClicked();
    void onResetPresetClicked();
    void onAutoPreviewTimer();
    void showPreviewWindow();
    void onShowExposureToolsToggled(bool enabled);

private:
    enum class Develop {
        Exposure,
        Contrast,
        Highlights,
        Shadows,
        Whites,
        Blacks,
        Gamma,
        Tint,
        Vibrance,
        Saturation,
        SharpenAmount,
        ReconstructThreshold,
        ReconstructNeutralize,
        Count
    };

    // One develop slider with its spin box. The slider holds an integer, and
    // scale converts it to the value shown and used.
    struct DevelopControl {
        Develop id = Develop::Exposure;
        const char *key = nullptr; // JSON key in presets and per-image settings
        int defaultValue = 0;
        double scale = 1.0;
        QSlider *slider = nullptr;
        QDoubleSpinBox *spinBox = nullptr;
    };

    DevelopControl &developControl(Develop id);
    const DevelopControl &developControl(Develop id) const;
    void addDevelopControl(Develop id,
                           const char *key,
                           const QString &label,
                           const QString &toolTip,
                           int minimum,
                           int maximum,
                           int defaultValue,
                           double scale,
                           int decimals,
                           QVBoxLayout *layout);
    void setDevelopValue(Develop id, int value);
    double developValue(Develop id) const;
    superccd::EditParams currentEditParams() const;
    void setTemperatureKelvin(int kelvin);
    void setCurrentScene(superccd::SceneImagePtr scene, const QString &inputPath);

    void updateControls(bool busy);
    void updatePreviewDisplay(bool preserveViewport = true);
    void showStatus(const QString &message);
    QRect currentPreviewVisibleRect() const;
    void pushExposureToolsFromCache();
    ConversionSettings currentSettings() const;
    void applyParameterSettings(const ConversionSettings &settings);
    void loadPreset(const QString &name);
    QJsonObject collectCurrentStateAsJson() const;
    void applyJsonPreset(const QJsonObject &data);
    void refreshPresetCombo();
    void markPresetModified();
    void queueAutoPreview();
    void applyWhiteBalancePickerSample();
    QPointF previewCanvasPosition(QObject *watched, const QPointF &position) const;
    bool hasCurrentPreview() const;
    bool convertOneFile(const QString &inputPath,
                        const QString &outputFolder,
                        const ConversionSettings &settings,
                        QString &error);
    void loadImageSettingsForCurrentFile();
    void saveImageSettingsForCurrentFile();

    QListWidget *m_fileList;
    QLineEdit *m_outputFolder;
    QSlider *m_rTransitionStartSlider;
    QSlider *m_rTransitionDelaySlider;
    QSlider *m_rTransitionSmoothnessSlider;
    QSlider *m_rLumaNoiseReductionSlider;
    QSlider *m_chromaNoiseReductionSlider;
    QSlider *m_previewZoomSlider;
    QPushButton *m_whiteBalancePickerButton;
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
    QComboBox *m_presetCombo;
    QPushButton *m_savePresetButton;
    QPushButton *m_resetPresetButton;
    PresetManager *m_presetManager;
    ImageSettingsManager *m_imageSettingsManager;
    bool m_presetComboGuard = false;
    bool m_presetModified = false;
    QJsonObject m_loadedPresetData;
    QLabel *m_statusLabel;
    QTimer *m_statusClearTimer;
    QTimer *m_autoPreviewTimer;
    // Coalesces the (relatively expensive) "build the adjusted 8-bit
    // image and push it to the exposure tools" path so it runs at most
    // once per ~75 ms while the user is dragging a preview slider.
    // Without this, every valueChanged tick runs the full
    // applyDisplayAdjustments() pipeline over the whole preview, which
    // is the dominant cost on the Raspberry Pi.
    QTimer *m_exposureScopeTimer;
    // What m_adjustedDisplayImage was rendered from, so the scopes can skip a
    // rebuild when only the scroll position or zoom changed.
    superccd::SceneImagePtr m_scopeScene;
    superccd::EditParams m_scopeParams;
    QSpinBox *m_rTransitionStartSpinBox;
    QSpinBox *m_rTransitionDelaySpinBox;
    QSpinBox *m_rTransitionSmoothnessSpinBox;
    QSpinBox *m_rLumaNoiseReductionSpinBox;
    QSpinBox *m_chromaNoiseReductionSpinBox;
    TransitionCurveWidget *m_transitionCurveWidget;
    QSpinBox *m_previewZoomSpinBox;
    std::array<DevelopControl, static_cast<size_t>(Develop::Count)> m_developControls;
    QSlider *m_temperatureSlider = nullptr;
    QSpinBox *m_temperatureSpinBox = nullptr;
    QPushButton *m_temperatureAsShotButton = nullptr;
    int m_temperatureKelvin = 0; // 0 keeps the white balance as shot
    QCheckBox *m_reconstructCheckBox = nullptr;
    superccd::SceneImagePtr m_scene;
    QImage m_adjustedDisplayImage;
    bool m_previewDragging = false;
    QPoint m_lastPreviewDragPos;
    QString m_lastPreviewedInputPath;
    bool m_busy = false;
    SuperCCDProcessor m_processor;
};

#endif // MAINWINDOW_H
