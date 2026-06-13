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
class PreviewCanvas;

#include "SuperCCDProcessor.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
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
    void onPreviewGammaChanged(int value);
    void onPreviewContrastChanged(int value);
    void onPreviewSaturationChanged(int value);
    void onPreviewSharpeningChanged(int value);
    void onPreviewHighlightCompressionChanged(int value);
    void onSaveDefaults();
    void onResetDefaults();
    void onAutoPreviewTimer();

private:
    void updateControls(bool busy);
    QImage buildAdjustedPreviewImage16() const;
    void updatePreviewDisplay();
    void updateSharpenedPreviewDisplay();
    void showStatus(const QString &message);
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
    QSlider *m_rTransitionDelaySlider;
    QLabel *m_rTransitionDelayValueLabel;
    QSlider *m_rTransitionSmoothnessSlider;
    QLabel *m_rTransitionSmoothnessValueLabel;
    QSlider *m_previewZoomSlider;
    QLabel *m_previewZoomValueLabel;
    QSlider *m_previewExposureSlider;
    QLabel *m_previewExposureValueLabel;
    QSlider *m_previewWhiteBalanceSlider;
    QLabel *m_previewWhiteBalanceValueLabel;
    QSlider *m_previewTintSlider;
    QLabel *m_previewTintValueLabel;
    QPushButton *m_whiteBalancePickerButton;
    QSlider *m_previewGammaSlider;
    QLabel *m_previewGammaValueLabel;
    QSlider *m_previewContrastSlider;
    QLabel *m_previewContrastValueLabel;
    QSlider *m_previewSaturationSlider;
    QLabel *m_previewSaturationValueLabel;
    QSlider *m_previewSharpeningSlider;
    QLabel *m_previewSharpeningValueLabel;
    QSlider *m_previewHighlightCompressionSlider;
    QLabel *m_previewHighlightCompressionValueLabel;
    QComboBox *m_previewRotationCombo;
    QCheckBox *m_correctPreviewOutliersCheckBox;
    QCheckBox *m_autoPreviewCheckBox;
    QScrollArea *m_previewScrollArea;
    PreviewCanvas *m_previewLabel;
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
    QImage m_currentPreviewImage;
    bool m_previewDragging = false;
    QPoint m_lastPreviewDragPos;
    QString m_lastPreviewedInputPath;
    bool m_busy = false;
    SuperCCDProcessor m_processor;
};

#endif // MAINWINDOW_H
