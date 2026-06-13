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
class QCloseEvent;
class QWidget;
class QSpinBox;
class QDoubleSpinBox;
class PreviewCanvas;

#include "SuperCCDProcessor.h"

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
    void onPreviewGammaChanged(int value);
    void onPreviewContrastChanged(int value);
    void onPreviewSaturationChanged(int value);
    void onPreviewSharpeningChanged(int value);
    void onPreviewHighlightCompressionChanged(int value);
    void onSaveDefaults();
    void onResetDefaults();
    void onAutoPreviewTimer();
    void showPreviewWindow();

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
    QSlider *m_rTransitionSmoothnessSlider;
    QSlider *m_previewZoomSlider;
    QSlider *m_previewExposureSlider;
    QSlider *m_previewWhiteBalanceSlider;
    QSlider *m_previewTintSlider;
    QPushButton *m_whiteBalancePickerButton;
    QSlider *m_previewGammaSlider;
    QSlider *m_previewContrastSlider;
    QSlider *m_previewSaturationSlider;
    QSlider *m_previewSharpeningSlider;
    QSlider *m_previewHighlightCompressionSlider;
    QComboBox *m_previewRotationCombo;
    QCheckBox *m_correctPreviewOutliersCheckBox;
    QCheckBox *m_autoPreviewCheckBox;
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
    QSpinBox *m_rTransitionDelaySpinBox;
    QSpinBox *m_rTransitionSmoothnessSpinBox;
    QSpinBox *m_previewZoomSpinBox;
    QDoubleSpinBox *m_previewExposureSpinBox;
    QSpinBox *m_previewWhiteBalanceSpinBox;
    QSpinBox *m_previewTintSpinBox;
    QDoubleSpinBox *m_previewGammaSpinBox;
    QSpinBox *m_previewContrastSpinBox;
    QSpinBox *m_previewSaturationSpinBox;
    QSpinBox *m_previewSharpeningSpinBox;
    QSpinBox *m_previewHighlightCompressionSpinBox;
    QImage m_currentPreviewImage;
    bool m_previewDragging = false;
    QPoint m_lastPreviewDragPos;
    QString m_lastPreviewedInputPath;
    bool m_busy = false;
    SuperCCDProcessor m_processor;
};

#endif // MAINWINDOW_H
