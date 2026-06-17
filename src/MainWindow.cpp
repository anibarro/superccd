#include "MainWindow.h"
#include "DngWriter.h"
#include "PreviewCanvas.h"
#include "PreviewFalseColorSuppression.h"
#include "PreviewImageProcessing.h"
#include "PreviewPixelCorrection.h"

#include <QAction>
#include <QBoxLayout>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMimeData>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSettings>
#include <QSplitter>
#include <QStringList>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>
#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <algorithm>
#include <QMessageBox>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {
constexpr int kDefaultDelaySliderValue = 50;
constexpr int kDefaultSmoothnessSliderValue = 50;
constexpr int kDefaultPreviewExposureSliderValue = 0;
constexpr int kDefaultPreviewWhiteBalanceSliderValue = 0;
constexpr int kDefaultPreviewTintSliderValue = 0;
constexpr int kDefaultPreviewGammaSliderValue = 40;
constexpr int kDefaultPreviewContrastSliderValue = 38;
constexpr int kDefaultPreviewShadowsSliderValue = 0;
constexpr int kDefaultPreviewShadowRangeSliderValue = 100;
constexpr int kDefaultPreviewSaturationSliderValue = 64;
constexpr int kDefaultPreviewSharpeningSliderValue = 0;
constexpr int kDefaultPreviewHighlightCompressionSliderValue = 30;
constexpr int kDefaultPreviewZoomSliderValue = 35;
constexpr bool kDefaultAutoPreview = true;
constexpr int kPreviewExportSixMpShortSide = 2016;
constexpr int kItemPreviewRotationRole = Qt::UserRole + 1;

enum class PreviewExportSize {
    FullSize12Mp = 0,
    SixMp = 1
};

enum class PreviewExportFormat {
    Jpeg = 0,
    Tiff16 = 1
};

double shadowRangeMask(double linear, double shadowRange)
{
    constexpr double kMinimumPivot = 0.08;
    constexpr double kMaximumPivot = 0.80;
    const double pivot =
        kMinimumPivot + (kMaximumPivot - kMinimumPivot) * shadowRange;
    const double normalized =
        std::clamp(std::clamp(linear, 0.0, 1.0) / std::max(pivot, 0.001), 0.0, 1.0);
    const double baseMask =
        1.0 - normalized * normalized * (3.0 - 2.0 * normalized);
    const double maskStrength = std::pow(1.0 - shadowRange, 2.5);
    return 1.0 - (1.0 - baseMask) * maskStrength;
}

double applyShadowRecoveryCurve(double linear, double shadowRecovery, double shadowRange)
{
    if (shadowRecovery <= 0.0) {
        return linear;
    }

    const double clamped = std::clamp(linear, 0.0, 1.0);
    const double exponent = 1.0 + shadowRecovery * 3.0;
    const double lifted = 1.0 - std::pow(1.0 - clamped, exponent);
    return clamped + (lifted - clamped) * shadowRangeMask(clamped, shadowRange);
}

QSettings appSettings()
{
    return QSettings(QStringLiteral("superccd"), QStringLiteral("superccd2dng"));
}

QString listItemPath(const QListWidgetItem *item)
{
    if (!item) {
        return QString();
    }
    const QString storedPath = item->data(Qt::UserRole).toString();
    return storedPath.isEmpty() ? item->text() : storedPath;
}

int previewRotationFromMetadata(const SuperCCDMetadata &metadata)
{
    const int flip = metadata.flip < 0 ? -metadata.flip : metadata.flip;
    switch (flip) {
    case 3:
        return 180;
    case 5:
        return 270;
    case 6:
        return 90;
    case 2:
        return 180;
    case 1:
        return 270;
    default:
        return 0;
    }
}

QString displayCameraModel(const SuperCCDMetadata &metadata)
{
    const QString make = metadata.make.trimmed();
    const QString model = metadata.model.trimmed();
    if (model.isEmpty()) {
        return make;
    }
    if (!make.isEmpty() && !model.startsWith(make, Qt::CaseInsensitive)) {
        return make + QLatin1Char(' ') + model;
    }
    return model;
}

QString formatShutterSpeed(double shutterSeconds)
{
    if (!(shutterSeconds > 0.0)) {
        return QString();
    }
    if (shutterSeconds >= 1.0) {
        if (std::fabs(shutterSeconds - std::round(shutterSeconds)) < 0.01) {
            return QStringLiteral("%1 s").arg(static_cast<int>(std::round(shutterSeconds)));
        }
        return QStringLiteral("%1 s").arg(QString::number(shutterSeconds, 'f', shutterSeconds >= 10.0 ? 1 : 2));
    }

    const double denominator = 1.0 / shutterSeconds;
    return QStringLiteral("1/%1 s").arg(QString::number(std::round(denominator)));
}

QString formatExposureSummary(const SuperCCDMetadata &metadata)
{
    QStringList parts;
    if (metadata.iso > 0) {
        parts.append(QStringLiteral("ISO %1").arg(metadata.iso));
    }
    const QString shutter = formatShutterSpeed(metadata.shutter);
    if (!shutter.isEmpty()) {
        parts.append(shutter);
    }
    if (metadata.aperture > 0.0) {
        parts.append(QStringLiteral("f/%1").arg(QString::number(metadata.aperture, 'f', 1)));
    }
    return parts.join(QStringLiteral("  "));
}

QString formatLensSummary(const SuperCCDMetadata &metadata)
{
    QStringList parts;
    if (metadata.focalLength > 0.0) {
        const int precision = std::fabs(metadata.focalLength - std::round(metadata.focalLength)) < 0.05 ? 0 : 1;
        parts.append(QStringLiteral("%1 mm").arg(QString::number(metadata.focalLength, 'f', precision)));
    }
    if (!metadata.lensModel.trimmed().isEmpty()) {
        parts.append(metadata.lensModel.trimmed());
    }
    return parts.join(QStringLiteral("  "));
}

QWidget *createFileListRow(const QString &displayName,
                           const QString &fullPath,
                           const QImage &thumbnail,
                           const SuperCCDMetadata &metadata,
                           QWidget *parent)
{
    QWidget *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(8);

    QLabel *thumbLabel = new QLabel(row);
    thumbLabel->setFixedSize(96, 64);
    thumbLabel->setAlignment(Qt::AlignCenter);
    thumbLabel->setStyleSheet(QStringLiteral("background:#2a2a2a; border:1px solid #505050;"));
    if (!thumbnail.isNull()) {
        thumbLabel->setPixmap(QPixmap::fromImage(thumbnail.scaled(96, 64,
                                                                  Qt::KeepAspectRatio,
                                                                  Qt::SmoothTransformation)));
    } else {
        thumbLabel->setText(QStringLiteral("No\nthumb"));
    }

    QWidget *textContainer = new QWidget(row);
    auto *textLayout = new QVBoxLayout(textContainer);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(0);

    QLabel *nameLabel = new QLabel(displayName, textContainer);
    QFont nameFont = nameLabel->font();
    nameFont.setBold(true);
    nameLabel->setFont(nameFont);
    nameLabel->setToolTip(fullPath);
    nameLabel->setWordWrap(true);
    nameLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    textLayout->addWidget(nameLabel);

    const auto addMetaLabel = [textContainer, textLayout, &fullPath](const QString &text) {
        if (text.isEmpty()) {
            return;
        }
        QLabel *label = new QLabel(text, textContainer);
        label->setToolTip(fullPath);
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::NoTextInteraction);
        QFont metaFont = label->font();
        metaFont.setPointSizeF(std::max(8.0, metaFont.pointSizeF() - 1.0));
        label->setFont(metaFont);
        label->setStyleSheet(QStringLiteral("color:#707070;"));
        textLayout->addWidget(label);
    };

    addMetaLabel(displayCameraModel(metadata));
    addMetaLabel(formatExposureSummary(metadata));
    addMetaLabel(formatLensSummary(metadata));

    layout->addWidget(thumbLabel, 0);
    layout->addWidget(textContainer, 1);
    return row;
}

QImage resizeForPreviewExport(const QImage &image, PreviewExportSize exportSize)
{
    if (image.isNull() || exportSize == PreviewExportSize::FullSize12Mp) {
        return image;
    }

    const int targetShortSide = kPreviewExportSixMpShortSide;
    const int shortSide = std::min(image.width(), image.height());
    if (shortSide <= targetShortSide) {
        return image;
    }

    const double scale = static_cast<double>(targetShortSide) / static_cast<double>(shortSide);
    const QSize targetSize(std::max(1, static_cast<int>(std::round(image.width() * scale))),
                           std::max(1, static_cast<int>(std::round(image.height() * scale))));

    QImage scaled = image;
    while (scaled.width() / 2 >= targetSize.width() && scaled.height() / 2 >= targetSize.height()) {
        scaled = scaled.scaled(QSize(std::max(targetSize.width(), scaled.width() / 2),
                                     std::max(targetSize.height(), scaled.height() / 2)),
                               Qt::IgnoreAspectRatio,
                               Qt::SmoothTransformation);
    }

    if (scaled.size() != targetSize) {
        scaled = scaled.scaled(targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    return scaled;
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_fileList(new QListWidget(this))
    , m_outputFolder(new QLineEdit(this))
    , m_rTransitionDelaySlider(new QSlider(Qt::Horizontal, this))
    , m_rTransitionSmoothnessSlider(new QSlider(Qt::Horizontal, this))
    , m_previewZoomSlider(new QSlider(Qt::Horizontal, this))
    , m_previewExposureSlider(new QSlider(Qt::Horizontal, this))
    , m_previewWhiteBalanceSlider(new QSlider(Qt::Horizontal, this))
    , m_previewTintSlider(new QSlider(Qt::Horizontal, this))
    , m_whiteBalancePickerButton(new QPushButton(tr("White Balance Picker: Off"), this))
    , m_previewGammaSlider(new QSlider(Qt::Horizontal, this))
    , m_previewContrastSlider(new QSlider(Qt::Horizontal, this))
    , m_previewShadowsSlider(new QSlider(Qt::Horizontal, this))
    , m_previewShadowRangeSlider(new QSlider(Qt::Horizontal, this))
    , m_previewSaturationSlider(new QSlider(Qt::Horizontal, this))
    , m_previewSharpeningSlider(new QSlider(Qt::Horizontal, this))
    , m_previewHighlightCompressionSlider(new QSlider(Qt::Horizontal, this))
    , m_previewRotationCombo(new QComboBox(this))
    , m_correctPreviewOutliersCheckBox(
          new QCheckBox(tr("Correct isolated light/dark pixels"), this))
    , m_autoPreviewCheckBox(new QCheckBox(tr("Update preview automatically"), this))
    , m_previewWindow(new QWidget(this, Qt::Window))
    , m_previewScrollArea(new QScrollArea(m_previewWindow))
    , m_previewLabel(new PreviewCanvas(m_previewScrollArea))
    , m_showPreviewButton(new QPushButton(tr("Show Preview"), this))
    , m_previewButton(new QPushButton(tr("Update Preview"), this))
    , m_exportPreviewButton(new QPushButton(tr("Export Preview"), this))
    , m_convertCurrentButton(new QPushButton(tr("Convert"), this))
    , m_convertAllButton(new QPushButton(tr("Convert All"), this))
    , m_exportPlaneImagesCheckBox(new QCheckBox(tr("Export S/R Planes"), this))
    , m_resetDefaultsButton(new QPushButton(tr("Reset Defaults"), this))
    , m_saveDefaultsButton(new QPushButton(tr("Save Current As Default"), this))
    , m_statusLabel(new QLabel(tr("Ready."), this))
    , m_statusClearTimer(new QTimer(this))
    , m_autoPreviewTimer(new QTimer(this))
    , m_previewSharpeningTimer(new QTimer(this))
    , m_rTransitionDelaySpinBox(new QSpinBox(this))
    , m_rTransitionSmoothnessSpinBox(new QSpinBox(this))
    , m_previewZoomSpinBox(new QSpinBox(this))
    , m_previewExposureSpinBox(new QDoubleSpinBox(this))
    , m_previewWhiteBalanceSpinBox(new QSpinBox(this))
    , m_previewTintSpinBox(new QSpinBox(this))
    , m_previewGammaSpinBox(new QDoubleSpinBox(this))
    , m_previewContrastSpinBox(new QSpinBox(this))
    , m_previewShadowsSpinBox(new QSpinBox(this))
    , m_previewShadowRangeSpinBox(new QSpinBox(this))
    , m_previewSaturationSpinBox(new QSpinBox(this))
    , m_previewSharpeningSpinBox(new QSpinBox(this))
    , m_previewHighlightCompressionSpinBox(new QSpinBox(this))
{
    setWindowTitle(tr("SuperCCD RAF to DNG Converter v%1").arg(QString::fromLatin1(APP_VERSION_STRING)));
    resize(900, 760);
    setAcceptDrops(true);

    QWidget *central = new QWidget(this);
    setCentralWidget(central);

    m_previewWindow->setWindowTitle(tr("Preview"));
    m_previewWindow->setMinimumSize(520, 360);
    m_previewWindow->resize(1000, 760);
    QVBoxLayout *previewWindowLayout = new QVBoxLayout(m_previewWindow);
    previewWindowLayout->setContentsMargins(0, 0, 0, 0);
    previewWindowLayout->addWidget(m_previewScrollArea);

    QPushButton *addFilesButton = new QPushButton(tr("Add RAF Files..."), this);
    QPushButton *removeFilesButton = new QPushButton(tr("Remove Selected"), this);
    QPushButton *selectFolderButton = new QPushButton(tr("Select Output Folder..."), this);
    QLabel *warmupNoteLabel = new QLabel(
        tr("Note: the first preview or conversion for a RAF file is slower because the app has to decode and cache the raw data."),
        this);

    m_rTransitionDelaySlider->setRange(0, 100);
    m_rTransitionDelaySlider->setValue(kDefaultDelaySliderValue);
    m_rTransitionSmoothnessSlider->setRange(0, 100);
    m_rTransitionSmoothnessSlider->setValue(kDefaultSmoothnessSliderValue);
    m_previewZoomSlider->setRange(5, 400);
    m_previewZoomSlider->setValue(kDefaultPreviewZoomSliderValue);
    m_previewExposureSlider->setRange(-30, 40);
    m_previewExposureSlider->setValue(kDefaultPreviewExposureSliderValue);
    m_previewWhiteBalanceSlider->setRange(-100, 100);
    m_previewWhiteBalanceSlider->setValue(kDefaultPreviewWhiteBalanceSliderValue);
    m_previewTintSlider->setRange(-100, 100);
    m_previewTintSlider->setValue(kDefaultPreviewTintSliderValue);
    m_whiteBalancePickerButton->setCheckable(true);
    m_whiteBalancePickerButton->setToolTip(
        tr("Turn on the picker, move the box over a neutral gray area, use the "
           "mouse wheel to resize it, and left-click to set white balance and tint."));
    // Gamma slider: range 0-300 (0 to 3.0), default 220 (gamma 2.2)
    m_previewGammaSlider->setRange(0, 300);
    m_previewGammaSlider->setValue(kDefaultPreviewGammaSliderValue);
    // Contrast slider: range -200 to +200, default 0
    m_previewContrastSlider->setRange(-200, 200);
    m_previewContrastSlider->setValue(kDefaultPreviewContrastSliderValue);
    // Shadows slider: range 0 to 100, default 0
    m_previewShadowsSlider->setRange(0, 100);
    m_previewShadowsSlider->setValue(kDefaultPreviewShadowsSliderValue);
    // Shadow range slider: range 0 to 100, default 100
    m_previewShadowRangeSlider->setRange(0, 100);
    m_previewShadowRangeSlider->setValue(kDefaultPreviewShadowRangeSliderValue);
    // Saturation slider: range -200 to +200, default 0
    m_previewSaturationSlider->setRange(-200, 200);
    m_previewSaturationSlider->setValue(kDefaultPreviewSaturationSliderValue);
    // Sharpening is deliberately limited to a luma-only unsharp pass.
    m_previewSharpeningSlider->setRange(0, 100);
    m_previewSharpeningSlider->setValue(kDefaultPreviewSharpeningSliderValue);
    // Highlight compression slider: range 0 to 100, default 0
    m_previewHighlightCompressionSlider->setRange(0, 100);
    m_previewHighlightCompressionSlider->setValue(kDefaultPreviewHighlightCompressionSliderValue);
    m_previewRotationCombo->addItem(tr("Normal"), 0);
    m_previewRotationCombo->addItem(tr("Rotate 90 CW"), 90);
    m_previewRotationCombo->addItem(tr("Rotate 180"), 180);
    m_previewRotationCombo->addItem(tr("Rotate 90 CCW"), 270);
    m_correctPreviewOutliersCheckBox->setChecked(false);
    m_correctPreviewOutliersCheckBox->setToolTip(
        tr("Correct only strongly isolated light or dark pixels in the finished "
           "16-bit preview. Other pixels remain unchanged. Affects the live "
           "preview and JPEG/TIFF preview exports, not DNG."));
    m_autoPreviewCheckBox->setChecked(kDefaultAutoPreview);
    m_exportPlaneImagesCheckBox->setChecked(false);
    m_fileList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_fileList->setIconSize(QSize(96, 96));
    m_fileList->setUniformItemSizes(false);
    m_fileList->setWordWrap(true);
    m_fileList->setAcceptDrops(true);
    m_fileList->setDropIndicatorShown(true);
    m_previewLabel->setMinimumSize(480, 480);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setStyleSheet(QStringLiteral("background:#202020; color:#d0d0d0; border:1px solid #505050;"));
    m_previewLabel->setText(tr("Preview not generated."));
    m_previewLabel->setMouseTracking(true);
    m_previewScrollArea->setBackgroundRole(QPalette::Dark);
    m_previewScrollArea->setWidget(m_previewLabel);
    m_previewScrollArea->setWidgetResizable(false);
    m_previewScrollArea->viewport()->installEventFilter(this);
    m_previewLabel->installEventFilter(this);
    m_previewWindow->installEventFilter(this);
    m_statusClearTimer->setSingleShot(true);
    m_autoPreviewTimer->setSingleShot(true);
    m_autoPreviewTimer->setInterval(250);
    m_previewSharpeningTimer->setSingleShot(true);
    m_previewSharpeningTimer->setInterval(100);
    warmupNoteLabel->setWordWrap(true);
    warmupNoteLabel->setStyleSheet(QStringLiteral("color:#808080;"));

    // Configure spinboxes to work with sliders
    // Delay spinbox: 0 to 100 (represents 0.00 to 1.00)
    m_rTransitionDelaySpinBox->setRange(0, 100);
    m_rTransitionDelaySpinBox->setValue(kDefaultDelaySliderValue);
    m_rTransitionDelaySpinBox->setSingleStep(1);
    
    // Smoothness spinbox: 0 to 100 (represents 0.00 to 1.00)
    m_rTransitionSmoothnessSpinBox->setRange(0, 100);
    m_rTransitionSmoothnessSpinBox->setValue(kDefaultSmoothnessSliderValue);
    m_rTransitionSmoothnessSpinBox->setSingleStep(1);
    
    // Zoom spinbox: 5% to 400%
    m_previewZoomSpinBox->setRange(5, 400);
    m_previewZoomSpinBox->setValue(kDefaultPreviewZoomSliderValue);
    m_previewZoomSpinBox->setSuffix(QStringLiteral("%"));
    m_previewZoomSpinBox->setSingleStep(5);
    
    // Exposure spinbox: -3.0 to +4.0 EV, step 0.1
    m_previewExposureSpinBox->setRange(-30, 40);
    m_previewExposureSpinBox->setValue(kDefaultPreviewExposureSliderValue);
    m_previewExposureSpinBox->setSuffix(QStringLiteral(" EV"));
    m_previewExposureSpinBox->setDecimals(1);
    m_previewExposureSpinBox->setSingleStep(1);
    
    // White balance spinbox: -100 to +100
    m_previewWhiteBalanceSpinBox->setRange(-100, 100);
    m_previewWhiteBalanceSpinBox->setValue(kDefaultPreviewWhiteBalanceSliderValue);
    m_previewWhiteBalanceSpinBox->setSingleStep(1);
    
    // Tint spinbox: -100 to +100
    m_previewTintSpinBox->setRange(-100, 100);
    m_previewTintSpinBox->setValue(kDefaultPreviewTintSliderValue);
    m_previewTintSpinBox->setSingleStep(1);
    
    // Gamma spinbox: 0.00 to 3.00, step 0.01
    m_previewGammaSpinBox->setRange(0, 300);
    m_previewGammaSpinBox->setValue(kDefaultPreviewGammaSliderValue);
    m_previewGammaSpinBox->setDecimals(2);
    m_previewGammaSpinBox->setSingleStep(1);
    
    // Contrast spinbox: -200 to +200
    m_previewContrastSpinBox->setRange(-200, 200);
    m_previewContrastSpinBox->setValue(kDefaultPreviewContrastSliderValue);
    m_previewContrastSpinBox->setSingleStep(5);

    // Shadows spinbox: 0 to 100
    m_previewShadowsSpinBox->setRange(0, 100);
    m_previewShadowsSpinBox->setValue(kDefaultPreviewShadowsSliderValue);
    m_previewShadowsSpinBox->setSingleStep(5);

    // Shadow range spinbox: 0 to 100
    m_previewShadowRangeSpinBox->setRange(0, 100);
    m_previewShadowRangeSpinBox->setValue(kDefaultPreviewShadowRangeSliderValue);
    m_previewShadowRangeSpinBox->setSingleStep(5);
    
    // Saturation spinbox: -200 to +200
    m_previewSaturationSpinBox->setRange(-200, 200);
    m_previewSaturationSpinBox->setValue(kDefaultPreviewSaturationSliderValue);
    m_previewSaturationSpinBox->setSingleStep(5);
    
    // Sharpening spinbox: 0% to 100%
    m_previewSharpeningSpinBox->setRange(0, 100);
    m_previewSharpeningSpinBox->setValue(kDefaultPreviewSharpeningSliderValue);
    m_previewSharpeningSpinBox->setSuffix(QStringLiteral("%"));
    m_previewSharpeningSpinBox->setSingleStep(1);
    
    // Highlight compression spinbox: 0 to 100
    m_previewHighlightCompressionSpinBox->setRange(0, 100);
    m_previewHighlightCompressionSpinBox->setValue(kDefaultPreviewHighlightCompressionSliderValue);
    m_previewHighlightCompressionSpinBox->setSingleStep(5);

    QHBoxLayout *fileButtonsLayout = new QHBoxLayout;
    fileButtonsLayout->addWidget(addFilesButton);
    fileButtonsLayout->addWidget(removeFilesButton);
    fileButtonsLayout->addStretch();

    QHBoxLayout *convertButtonsLayout = new QHBoxLayout;
    convertButtonsLayout->addWidget(m_convertCurrentButton);
    convertButtonsLayout->addWidget(m_convertAllButton);
    convertButtonsLayout->addWidget(m_exportPlaneImagesCheckBox);
    convertButtonsLayout->addStretch();

    QVBoxLayout *leftLayout = new QVBoxLayout;
    leftLayout->addLayout(fileButtonsLayout);
    leftLayout->addWidget(m_fileList, 1);
    leftLayout->addLayout(convertButtonsLayout);

    // Transition controls group box
    QGroupBox *transitionGroup = new QGroupBox(tr("Transition Settings"), this);
    QFormLayout *transitionLayout = new QFormLayout(transitionGroup);
    
    QHBoxLayout *delayLayout = new QHBoxLayout;
    delayLayout->addWidget(m_rTransitionDelaySlider, 1);
    delayLayout->addWidget(m_rTransitionDelaySpinBox, 0);
    transitionLayout->addRow(tr("Handoff delay:"), delayLayout);
    
    QHBoxLayout *smoothnessLayout = new QHBoxLayout;
    smoothnessLayout->addWidget(m_rTransitionSmoothnessSlider, 1);
    smoothnessLayout->addWidget(m_rTransitionSmoothnessSpinBox, 0);
    transitionLayout->addRow(tr("Smoothness:"), smoothnessLayout);

    // Preview controls group box (wrapped in scrollable area)
    QGroupBox *previewGroup = new QGroupBox(tr("Preview Controls"), this);
    QFormLayout *previewControlsLayout = new QFormLayout(previewGroup);
    
    QHBoxLayout *exposureLayout = new QHBoxLayout;
    exposureLayout->addWidget(m_previewExposureSlider, 1);
    exposureLayout->addWidget(m_previewExposureSpinBox, 0);
    previewControlsLayout->addRow(tr("Exposure:"), exposureLayout);
    
    QHBoxLayout *gammaLayout = new QHBoxLayout;
    gammaLayout->addWidget(m_previewGammaSlider, 1);
    gammaLayout->addWidget(m_previewGammaSpinBox, 0);
    previewControlsLayout->addRow(tr("Gamma:"), gammaLayout);
    
    QHBoxLayout *contrastLayout = new QHBoxLayout;
    contrastLayout->addWidget(m_previewContrastSlider, 1);
    contrastLayout->addWidget(m_previewContrastSpinBox, 0);
    previewControlsLayout->addRow(tr("Contrast:"), contrastLayout);
    
    QHBoxLayout *saturationLayout = new QHBoxLayout;
    saturationLayout->addWidget(m_previewSaturationSlider, 1);
    saturationLayout->addWidget(m_previewSaturationSpinBox, 0);
    previewControlsLayout->addRow(tr("Saturation:"), saturationLayout);
    
    QHBoxLayout *sharpeningLayout = new QHBoxLayout;
    sharpeningLayout->addWidget(m_previewSharpeningSlider, 1);
    sharpeningLayout->addWidget(m_previewSharpeningSpinBox, 0);
    previewControlsLayout->addRow(tr("Sharpening:"), sharpeningLayout);

    QHBoxLayout *shadowsLayout = new QHBoxLayout;
    shadowsLayout->addWidget(m_previewShadowsSlider, 1);
    shadowsLayout->addWidget(m_previewShadowsSpinBox, 0);
    previewControlsLayout->addRow(tr("Shadows:"), shadowsLayout);

    QHBoxLayout *shadowRangeLayout = new QHBoxLayout;
    shadowRangeLayout->addWidget(m_previewShadowRangeSlider, 1);
    shadowRangeLayout->addWidget(m_previewShadowRangeSpinBox, 0);
    previewControlsLayout->addRow(tr("Shadow range:"), shadowRangeLayout);
    
    QHBoxLayout *highlightCompressionLayout = new QHBoxLayout;
    highlightCompressionLayout->addWidget(m_previewHighlightCompressionSlider, 1);
    highlightCompressionLayout->addWidget(m_previewHighlightCompressionSpinBox, 0);
    previewControlsLayout->addRow(tr("Highlight compression:"), highlightCompressionLayout);
    
    QHBoxLayout *whiteBalanceLayout = new QHBoxLayout;
    whiteBalanceLayout->addWidget(m_previewWhiteBalanceSlider, 1);
    whiteBalanceLayout->addWidget(m_previewWhiteBalanceSpinBox, 0);
    previewControlsLayout->addRow(tr("White balance:"), whiteBalanceLayout);
    
    QHBoxLayout *tintLayout = new QHBoxLayout;
    tintLayout->addWidget(m_previewTintSlider, 1);
    tintLayout->addWidget(m_previewTintSpinBox, 0);
    previewControlsLayout->addRow(tr("Tint:"), tintLayout);
    
    previewControlsLayout->addRow(tr("White balance picker:"), m_whiteBalancePickerButton);
    previewControlsLayout->addRow(tr("Rotation:"), m_previewRotationCombo);
    
    QHBoxLayout *zoomLayout = new QHBoxLayout;
    zoomLayout->addWidget(m_previewZoomSlider, 1);
    zoomLayout->addWidget(m_previewZoomSpinBox, 0);
    previewControlsLayout->addRow(tr("Zoom:"), zoomLayout);
    
    previewControlsLayout->addRow(m_correctPreviewOutliersCheckBox);
    previewControlsLayout->addRow(m_autoPreviewCheckBox);

    // Wrap preview group in scrollable area - constrain height to ~2 sliders tall
    QScrollArea *previewScrollArea = new QScrollArea(this);
    previewScrollArea->setWidget(previewGroup);
    previewScrollArea->setWidgetResizable(true);
    previewScrollArea->setFrameShape(QFrame::NoFrame);
    previewScrollArea->setMinimumHeight(60);  // About 2 slider rows tall
    previewScrollArea->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    // Make transition group fixed size - it should never grow vertically
    transitionGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    QFormLayout *optionsLayout = new QFormLayout;
    optionsLayout->addRow(tr("Output folder:"), m_outputFolder);
    optionsLayout->addRow(tr(""), selectFolderButton);
    optionsLayout->addRow(transitionGroup);
    optionsLayout->addRow(previewScrollArea);

    QHBoxLayout *defaultsButtonsLayout = new QHBoxLayout;
    defaultsButtonsLayout->addWidget(m_resetDefaultsButton);
    defaultsButtonsLayout->addWidget(m_saveDefaultsButton);

    QVBoxLayout *rightLayout = new QVBoxLayout;
    rightLayout->addLayout(optionsLayout);
    rightLayout->addLayout(defaultsButtonsLayout);
    QHBoxLayout *previewButtonsLayout = new QHBoxLayout;
    previewButtonsLayout->addWidget(m_showPreviewButton);
    previewButtonsLayout->addWidget(m_previewButton);
    previewButtonsLayout->addWidget(m_exportPreviewButton);
    rightLayout->addLayout(previewButtonsLayout);
    rightLayout->addWidget(warmupNoteLabel);

    // Create container widgets for the splitter
    QWidget *leftContainer = new QWidget(this);
    leftContainer->setLayout(leftLayout);
    QWidget *rightContainer = new QWidget(this);
    rightContainer->setLayout(rightLayout);
    
    QSplitter *topSplitter = new QSplitter(Qt::Horizontal, this);
    topSplitter->addWidget(leftContainer);
    topSplitter->addWidget(rightContainer);
    
    // Set initial sizes with RAF list getting more space
    QList<int> splitterSizes;
    splitterSizes.append(500);
    splitterSizes.append(400);
    topSplitter->setSizes(splitterSizes);
    topSplitter->setStretchFactor(0, 2);  // RAF list gets more stretch
    topSplitter->setStretchFactor(1, 1);  // Controls gets less stretch
    topSplitter->setCollapsible(0, false);
    topSplitter->setCollapsible(1, false);

    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->addWidget(topSplitter, 1);
    mainLayout->addWidget(m_statusLabel);
    central->setLayout(mainLayout);

    connect(addFilesButton, &QPushButton::clicked, this, &MainWindow::onAddFiles);
    connect(removeFilesButton, &QPushButton::clicked, this, &MainWindow::onRemoveSelectedFiles);
    connect(selectFolderButton, &QPushButton::clicked, this, &MainWindow::onSelectOutputFolder);
    connect(m_convertCurrentButton, &QPushButton::clicked, this, &MainWindow::onConvertCurrent);
    connect(m_convertAllButton, &QPushButton::clicked, this, &MainWindow::onConvertAll);
    connect(m_showPreviewButton, &QPushButton::clicked, this, &MainWindow::showPreviewWindow);
    connect(m_previewButton, &QPushButton::clicked, this, &MainWindow::onUpdatePreview);
    connect(m_exportPreviewButton, &QPushButton::clicked, this, &MainWindow::onExportPreview);
    connect(m_fileList, &QListWidget::currentRowChanged, this, [this](int row) {
        m_whiteBalancePickerButton->setChecked(false);
        if (row >= 0) {
            const QListWidgetItem *item = m_fileList->item(row);
            const int metadataRotation = item ? item->data(kItemPreviewRotationRole).toInt() : 0;
            const int rotationIndex = m_previewRotationCombo->findData(metadataRotation);
            const QSignalBlocker blocker(m_previewRotationCombo);
            m_previewRotationCombo->setCurrentIndex(rotationIndex >= 0 ? rotationIndex : 0);
            queueAutoPreview();
            updateControls(false);
        }
    });
    connect(m_resetDefaultsButton, &QPushButton::clicked, this, &MainWindow::onResetDefaults);
    connect(m_saveDefaultsButton, &QPushButton::clicked, this, &MainWindow::onSaveDefaults);
    connect(m_statusClearTimer, &QTimer::timeout, m_statusLabel, &QLabel::clear);
    connect(m_autoPreviewTimer, &QTimer::timeout, this, &MainWindow::onAutoPreviewTimer);
    connect(m_previewSharpeningTimer,
            &QTimer::timeout,
            this,
            &MainWindow::updateSharpenedPreviewDisplay);
    connect(m_rTransitionDelaySlider, &QSlider::valueChanged, this, [this](int) {
        queueAutoPreview();
    });
    connect(m_rTransitionSmoothnessSlider, &QSlider::valueChanged, this, [this](int) {
        queueAutoPreview();
    });
    connect(m_previewZoomSlider, &QSlider::valueChanged, this, &MainWindow::onPreviewZoomChanged);
    connect(m_previewExposureSlider, &QSlider::valueChanged, this, &MainWindow::onPreviewExposureChanged);
    connect(m_previewWhiteBalanceSlider, &QSlider::valueChanged, this, &MainWindow::onPreviewWhiteBalanceChanged);
    connect(m_previewTintSlider, &QSlider::valueChanged, this, &MainWindow::onPreviewTintChanged);
    connect(m_whiteBalancePickerButton,
            &QPushButton::toggled,
            this,
            &MainWindow::onWhiteBalancePickerToggled);
    connect(m_previewGammaSlider, &QSlider::valueChanged, this, &MainWindow::onPreviewGammaChanged);
    connect(m_previewContrastSlider, &QSlider::valueChanged, this, &MainWindow::onPreviewContrastChanged);
    connect(m_previewShadowsSlider, &QSlider::valueChanged, this, &MainWindow::onPreviewShadowsChanged);
    connect(m_previewShadowRangeSlider, &QSlider::valueChanged, this, &MainWindow::onPreviewShadowRangeChanged);
    connect(m_previewSaturationSlider, &QSlider::valueChanged, this, &MainWindow::onPreviewSaturationChanged);
    connect(m_previewSharpeningSlider, &QSlider::valueChanged, this, &MainWindow::onPreviewSharpeningChanged);
    connect(m_previewHighlightCompressionSlider, &QSlider::valueChanged, this, &MainWindow::onPreviewHighlightCompressionChanged);
    connect(m_previewRotationCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        queueAutoPreview();
    });
    connect(m_correctPreviewOutliersCheckBox,
            &QCheckBox::toggled,
            this,
            [this](bool) {
                m_whiteBalancePickerButton->setChecked(false);
                m_lastPreviewedInputPath.clear();
                updateControls(false);
                queueAutoPreview();
            });
    connect(m_autoPreviewCheckBox, &QCheckBox::toggled, this, [this](bool enabled) {
        if (enabled) {
            queueAutoPreview();
        } else {
            m_autoPreviewTimer->stop();
        }
    });

    // Bidirectional connections between spinboxes and sliders
    // When slider changes, update spinbox
    connect(m_rTransitionDelaySlider, &QSlider::valueChanged, this, [this](int value) {
        m_rTransitionDelaySpinBox->setValue(value);
    });
    connect(m_rTransitionSmoothnessSlider, &QSlider::valueChanged, this, [this](int value) {
        m_rTransitionSmoothnessSpinBox->setValue(value);
    });
    connect(m_previewZoomSlider, &QSlider::valueChanged, this, [this](int value) {
        m_previewZoomSpinBox->setValue(value);
    });
    connect(m_previewExposureSlider, &QSlider::valueChanged, this, [this](int value) {
        m_previewExposureSpinBox->setValue(value);
    });
    connect(m_previewWhiteBalanceSlider, &QSlider::valueChanged, this, [this](int value) {
        m_previewWhiteBalanceSpinBox->setValue(value);
    });
    connect(m_previewTintSlider, &QSlider::valueChanged, this, [this](int value) {
        m_previewTintSpinBox->setValue(value);
    });
    connect(m_previewGammaSlider, &QSlider::valueChanged, this, [this](int value) {
        m_previewGammaSpinBox->setValue(value);
    });
    connect(m_previewContrastSlider, &QSlider::valueChanged, this, [this](int value) {
        m_previewContrastSpinBox->setValue(value);
    });
    connect(m_previewShadowsSlider, &QSlider::valueChanged, this, [this](int value) {
        m_previewShadowsSpinBox->setValue(value);
    });
    connect(m_previewShadowRangeSlider, &QSlider::valueChanged, this, [this](int value) {
        m_previewShadowRangeSpinBox->setValue(value);
    });
    connect(m_previewSaturationSlider, &QSlider::valueChanged, this, [this](int value) {
        m_previewSaturationSpinBox->setValue(value);
    });
    connect(m_previewSharpeningSlider, &QSlider::valueChanged, this, [this](int value) {
        m_previewSharpeningSpinBox->setValue(value);
    });
    connect(m_previewHighlightCompressionSlider, &QSlider::valueChanged, this, [this](int value) {
        m_previewHighlightCompressionSpinBox->setValue(value);
    });

    // When spinbox changes, update slider (no auto preview for preview adjustments)
    connect(m_rTransitionDelaySpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (m_rTransitionDelaySlider->value() != value) {
            m_rTransitionDelaySlider->setValue(value);
            queueAutoPreview();
        }
    });
    connect(m_rTransitionSmoothnessSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (m_rTransitionSmoothnessSlider->value() != value) {
            m_rTransitionSmoothnessSlider->setValue(value);
            queueAutoPreview();
        }
    });
    connect(m_previewZoomSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (m_previewZoomSlider->value() != value) {
            m_previewZoomSlider->setValue(value);
        }
    });
    connect(m_previewExposureSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_previewExposureSlider->value() != static_cast<int>(value)) {
            m_previewExposureSlider->setValue(static_cast<int>(value));
        }
    });
    connect(m_previewWhiteBalanceSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (m_previewWhiteBalanceSlider->value() != value) {
            m_previewWhiteBalanceSlider->setValue(value);
        }
    });
    connect(m_previewTintSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (m_previewTintSlider->value() != value) {
            m_previewTintSlider->setValue(value);
        }
    });
    connect(m_previewGammaSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_previewGammaSlider->value() != static_cast<int>(value)) {
            m_previewGammaSlider->setValue(static_cast<int>(value));
        }
    });
    connect(m_previewContrastSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (m_previewContrastSlider->value() != value) {
            m_previewContrastSlider->setValue(value);
        }
    });
    connect(m_previewShadowsSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (m_previewShadowsSlider->value() != value) {
            m_previewShadowsSlider->setValue(value);
        }
    });
    connect(m_previewShadowRangeSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (m_previewShadowRangeSlider->value() != value) {
            m_previewShadowRangeSlider->setValue(value);
        }
    });
    connect(m_previewSaturationSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (m_previewSaturationSlider->value() != value) {
            m_previewSaturationSlider->setValue(value);
        }
    });
    connect(m_previewSharpeningSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (m_previewSharpeningSlider->value() != value) {
            m_previewSharpeningSlider->setValue(value);
        }
    });
    connect(m_previewHighlightCompressionSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (m_previewHighlightCompressionSlider->value() != value) {
            m_previewHighlightCompressionSlider->setValue(value);
        }
    });

    loadSavedDefaults();
    updateControls(false);

    const QByteArray previewGeometry =
        appSettings().value(QStringLiteral("windows/previewGeometry")).toByteArray();
    if (!previewGeometry.isEmpty()) {
        m_previewWindow->restoreGeometry(previewGeometry);
    }
    m_previewWindow->show();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_previewWindow && event->type() == QEvent::Close) {
        appSettings().setValue(QStringLiteral("windows/previewGeometry"),
                               m_previewWindow->saveGeometry());
    }

    if ((watched == m_previewLabel || watched == m_previewScrollArea->viewport()) && !m_currentPreviewImage.isNull()) {
        const bool pickerActive =
            m_whiteBalancePickerButton->isChecked() && hasCurrentPreview();
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
            if (pickerActive && mouseEvent->button() == Qt::LeftButton) {
                const QPointF canvasPosition =
                    previewCanvasPosition(watched, mouseEvent->position());
                if (m_previewLabel->rect().contains(canvasPosition.toPoint())) {
                    m_previewLabel->setWhiteBalancePickerPosition(canvasPosition);
                    applyWhiteBalancePickerSample();
                }
                return true;
            }
            if (mouseEvent->button() == Qt::LeftButton) {
                m_previewDragging = true;
                m_lastPreviewDragPos = mouseEvent->globalPosition().toPoint();
                m_previewLabel->setCursor(Qt::ClosedHandCursor);
                return true;
            }
            break;
        }
        case QEvent::Wheel: {
            QWheelEvent *wheelEvent = static_cast<QWheelEvent *>(event);
            const int delta = wheelEvent->angleDelta().y();
            if (pickerActive) {
                const QPointF canvasPosition =
                    previewCanvasPosition(watched, wheelEvent->position());
                if (m_previewLabel->rect().contains(canvasPosition.toPoint())) {
                    m_previewLabel->setWhiteBalancePickerPosition(canvasPosition);
                    m_previewLabel->resizeWhiteBalancePicker(delta);
                }
                return true;
            }
            if (delta != 0) {
                const int step = delta > 0 ? 10 : -10;
                m_previewZoomSlider->setValue(std::clamp(m_previewZoomSlider->value() + step,
                                                         m_previewZoomSlider->minimum(),
                                                         m_previewZoomSlider->maximum()));
                return true;
            }
            break;
        }
        case QEvent::MouseMove: {
            if (pickerActive) {
                QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
                const QPointF canvasPosition =
                    previewCanvasPosition(watched, mouseEvent->position());
                if (m_previewLabel->rect().contains(canvasPosition.toPoint())) {
                    m_previewLabel->setWhiteBalancePickerPosition(canvasPosition);
                } else {
                    m_previewLabel->hideWhiteBalancePicker();
                }
                return true;
            }
            if (!m_previewDragging) {
                break;
            }
            QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
            const QPoint currentPos = mouseEvent->globalPosition().toPoint();
            const QPoint delta = currentPos - m_lastPreviewDragPos;
            m_lastPreviewDragPos = currentPos;
            m_previewScrollArea->horizontalScrollBar()->setValue(m_previewScrollArea->horizontalScrollBar()->value() - delta.x());
            m_previewScrollArea->verticalScrollBar()->setValue(m_previewScrollArea->verticalScrollBar()->value() - delta.y());
            return true;
        }
        case QEvent::MouseButtonRelease: {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton && m_previewDragging) {
                m_previewDragging = false;
                m_previewLabel->setCursor(Qt::OpenHandCursor);
                return true;
            }
            break;
        }
        case QEvent::Leave:
            if (pickerActive) {
                m_previewLabel->hideWhiteBalancePicker();
                break;
            }
            if (!m_previewDragging) {
                m_previewLabel->unsetCursor();
            }
            break;
        default:
            break;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    appSettings().setValue(QStringLiteral("windows/previewGeometry"),
                           m_previewWindow->saveGeometry());
    m_previewWindow->close();
    QMainWindow::closeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        // Accept if at least one URL is a RAF file
        for (const QUrl &url : event->mimeData()->urls()) {
            if (url.isLocalFile()) {
                const QString file = url.toLocalFile();
                if (file.endsWith(QLatin1String(".raf"), Qt::CaseInsensitive)) {
                    event->acceptProposedAction();
                    return;
                }
            }
        }
    }
    event->ignore();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }

    QStringList newFiles;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (!url.isLocalFile())
            continue;
        const QString file = url.toLocalFile();
        if (!file.endsWith(QLatin1String(".raf"), Qt::CaseInsensitive))
            continue;

        // Check if already in list
        bool exists = false;
        for (int i = 0; i < m_fileList->count(); ++i) {
            if (listItemPath(m_fileList->item(i)) == file) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            newFiles.append(file);
        }
    }

    if (!newFiles.isEmpty()) {
        for (const QString &file : newFiles) {
            const QFileInfo info(file);
            auto *item = new QListWidgetItem(QString(), m_fileList);
            item->setToolTip(file);
            item->setData(Qt::UserRole, file);
            item->setSizeHint(QSize(0, 72));
            QImage thumbnail;
            QString thumbErr;
            SuperCCDMetadata metadata;
            QString metadataErr;
            SuperCCDProcessor::extractEmbeddedThumbnail(file, thumbnail, &thumbErr);
            SuperCCDProcessor::readMetadata(file, metadata, &metadataErr);
            item->setData(kItemPreviewRotationRole, previewRotationFromMetadata(metadata));
            if (!thumbErr.isEmpty()) {
                item->setToolTip(item->toolTip() + QStringLiteral("\nThumbnail error: %1").arg(thumbErr));
            }
            if (!metadataErr.isEmpty()) {
                item->setToolTip(item->toolTip() + QStringLiteral("\nMetadata error: %1").arg(metadataErr));
            }
            m_fileList->setItemWidget(item,
                                      createFileListRow(info.fileName(),
                                                        file,
                                                        thumbnail,
                                                        metadata,
                                                        m_fileList));
        }

        if (!m_fileList->currentItem() && m_fileList->count() > 0) {
            m_fileList->setCurrentRow(m_fileList->count() - 1);
        }
        showStatus(tr("Added %1 file(s) by drag and drop.").arg(newFiles.size()));
    }

    event->acceptProposedAction();
}

void MainWindow::onAddFiles()
{
    QStringList files = QFileDialog::getOpenFileNames(this,
                                                      tr("Select Fujifilm RAF files"),
                                                      QString(),
                                                      tr("RAF files (*.raf);;All files (*)"));
    if (files.isEmpty())
        return;

    for (const QString &file : files) {
        bool exists = false;
        for (int i = 0; i < m_fileList->count(); ++i) {
            if (listItemPath(m_fileList->item(i)) == file) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            const QFileInfo info(file);
            auto *item = new QListWidgetItem(QString(), m_fileList);
            item->setToolTip(file);
            item->setData(Qt::UserRole, file);
            item->setSizeHint(QSize(0, 72));
            QImage thumbnail;
            QString thumbErr;
            SuperCCDMetadata metadata;
            QString metadataErr;
            SuperCCDProcessor::extractEmbeddedThumbnail(file, thumbnail, &thumbErr);
            SuperCCDProcessor::readMetadata(file, metadata, &metadataErr);
            item->setData(kItemPreviewRotationRole, previewRotationFromMetadata(metadata));
            if (!thumbErr.isEmpty()) {
                item->setToolTip(item->toolTip() + QStringLiteral("\nThumbnail error: %1").arg(thumbErr));
            }
            if (!metadataErr.isEmpty()) {
                item->setToolTip(item->toolTip() + QStringLiteral("\nMetadata error: %1").arg(metadataErr));
            }
            m_fileList->setItemWidget(item,
                                      createFileListRow(info.fileName(),
                                                        file,
                                                        thumbnail,
                                                        metadata,
                                                        m_fileList));
        }
    }

    if (!m_fileList->currentItem() && m_fileList->count() > 0) {
        m_fileList->setCurrentRow(m_fileList->count() - 1);
    }
}

void MainWindow::onRemoveSelectedFiles()
{
    const QList<QListWidgetItem *> selectedItems = m_fileList->selectedItems();
    if (selectedItems.isEmpty()) {
        showStatus(tr("Please select a RAF file to remove."));
        return;
    }

    for (QListWidgetItem *item : selectedItems) {
        delete m_fileList->takeItem(m_fileList->row(item));
    }

    if (m_fileList->count() > 0 && !m_fileList->currentItem()) {
        m_fileList->setCurrentRow(0);
    }

    if (m_fileList->count() == 0) {
        m_currentPreviewImage = QImage();
        m_previewSharpeningTimer->stop();
        m_previewLabel->clearSourceImage();
        m_previewLabel->clear();
        m_previewLabel->setText(tr("Preview not generated."));
        m_previewLabel->unsetCursor();
        m_previewWindow->setWindowTitle(tr("Preview"));
    }
}

void MainWindow::onSelectOutputFolder()
{
    QString folder = QFileDialog::getExistingDirectory(this,
                                                       tr("Select Output Folder"),
                                                       m_outputFolder->text());
    if (!folder.isEmpty()) {
        m_outputFolder->setText(folder);
    }
}

bool MainWindow::convertOneFile(const QString &inputPath,
                                const QString &outputFolder,
                                const ConversionSettings &settings,
                                QString &error)
{
    QString baseName = QFileInfo(inputPath).completeBaseName();
    QString suffix;
    suffix = QStringLiteral("_6MP_CFA");
    const QString outputPath = QDir(outputFolder).filePath(baseName + suffix + ".dng");
    showStatus(tr("Processing %1...").arg(QFileInfo(inputPath).fileName()));
    return m_processor.process(inputPath, outputPath, settings, error);
}

void MainWindow::onConvertCurrent()
{
    if (m_fileList->count() == 0) {
        showStatus(tr("Please add at least one RAF file."));
        return;
    }

    QListWidgetItem *currentItem = m_fileList->currentItem();
    if (!currentItem) {
        showStatus(tr("Please select a RAF file from the list."));
        return;
    }

    const QString inputPath = listItemPath(currentItem);
    const QString outputFolder = m_outputFolder->text().trimmed();
    if (outputFolder.isEmpty()) {
        showStatus(tr("Please select an output folder."));
        return;
    }

    // If the selected file hasn't been previewed yet, render preview first
    if (m_lastPreviewedInputPath != inputPath) {
        showStatus(tr("Rendering preview for %1...").arg(QFileInfo(inputPath).fileName()));
        m_busy = true;
        updateControls(true);
        QCoreApplication::processEvents();

        QImage preview;
        QString error;
        if (!m_processor.renderPreview(inputPath, currentSettings(), preview, error)) {
            showStatus(tr("Preview failed: %1").arg(error));
            m_busy = false;
            updateControls(false);
            return;
        }

        m_currentPreviewImage = preview;
        if (m_correctPreviewOutliersCheckBox->isChecked()) {
            PreviewPixelCorrection::suppressIsolatedLumaOutliers(
                m_currentPreviewImage);
        }
        m_previewLabel->setSourceImage(m_currentPreviewImage);
        m_lastPreviewedInputPath = inputPath;
        m_previewWindow->setWindowTitle(
            tr("Preview - %1").arg(QFileInfo(inputPath).fileName()));
        updatePreviewDisplay();
        showStatus(tr("Preview rendered. Proceeding with conversion..."));
    }

    m_busy = true;
    updateControls(true);
    QCoreApplication::processEvents();
    const ConversionSettings settings = currentSettings();
    bool success = true;

    try {
        QString error;
        if (!convertOneFile(m_lastPreviewedInputPath, outputFolder, settings, error)) {
            showStatus(tr("Failed: %1").arg(error));
            success = false;
        }
    } catch (const std::exception &ex) {
        QString logPath = QDir(QCoreApplication::applicationDirPath()).filePath("last_error.log");
        QFile f(logPath);
        if (f.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream ts(&f);
            ts << QDateTime::currentDateTime().toString(Qt::ISODate) << " - Exception: " << ex.what() << "\n";
            f.close();
        }
        showStatus(tr("An unexpected error occurred. See last_error.log."));
        success = false;
    } catch (...) {
        QString logPath = QDir(QCoreApplication::applicationDirPath()).filePath("last_error.log");
        QFile f(logPath);
        if (f.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream ts(&f);
            ts << QDateTime::currentDateTime().toString(Qt::ISODate) << " - Unknown exception\n";
            f.close();
        }
        showStatus(tr("An unexpected error occurred. See last_error.log."));
        success = false;
    }

    if (success) {
        showStatus(tr("Conversion completed."));
    }

    m_busy = false;
    updateControls(false);
}

void MainWindow::onConvertAll()
{
    if (m_fileList->count() == 0) {
        showStatus(tr("Please add at least one RAF file."));
        return;
    }

    const QString outputFolder = m_outputFolder->text().trimmed();
    if (outputFolder.isEmpty()) {
        showStatus(tr("Please select an output folder."));
        return;
    }

    m_busy = true;
    updateControls(true);
    QCoreApplication::processEvents();
    const ConversionSettings settings = currentSettings();
    bool success = true;

    try {
        for (int i = 0; i < m_fileList->count(); ++i) {
            QString error;
            const QString inputPath = listItemPath(m_fileList->item(i));
            if (!convertOneFile(inputPath, outputFolder, settings, error)) {
                showStatus(tr("Failed: %1").arg(error));
                success = false;
                break;
            }
        }
    } catch (const std::exception &ex) {
        QString logPath = QDir(QCoreApplication::applicationDirPath()).filePath("last_error.log");
        QFile f(logPath);
        if (f.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream ts(&f);
            ts << QDateTime::currentDateTime().toString(Qt::ISODate) << " - Exception: " << ex.what() << "\n";
            f.close();
        }
        showStatus(tr("An unexpected error occurred. See last_error.log."));
        success = false;
    } catch (...) {
        QString logPath = QDir(QCoreApplication::applicationDirPath()).filePath("last_error.log");
        QFile f(logPath);
        if (f.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream ts(&f);
            ts << QDateTime::currentDateTime().toString(Qt::ISODate) << " - Unknown exception\n";
            f.close();
        }
        showStatus(tr("An unexpected error occurred. See last_error.log."));
        success = false;
    }

    if (success) {
        showStatus(tr("Conversion completed."));
    }

    m_busy = false;
    updateControls(false);
}

void MainWindow::onUpdatePreview()
{
    QListWidgetItem *current = m_fileList->currentItem();
    if (!current) {
        showStatus(tr("Please select a RAF file from the list."));
        return;
    }
    const QString inputPath = listItemPath(current);

    m_busy = true;
    updateControls(true);
    showStatus(tr("Rendering preview for %1...").arg(QFileInfo(inputPath).fileName()));
    QCoreApplication::processEvents();

    QImage preview;
    QString error;
    if (!m_processor.renderPreview(inputPath, currentSettings(), preview, error)) {
        m_whiteBalancePickerButton->setChecked(false);
        m_previewLabel->clearSourceImage();
        m_previewLabel->setText(tr("Preview failed."));
        m_previewWindow->setWindowTitle(tr("Preview - Failed"));
        showStatus(tr("Preview failed: %1").arg(error));
        m_busy = false;
        updateControls(false);
        return;
    }

    const bool shouldFitPreview = m_currentPreviewImage.isNull()
        || m_currentPreviewImage.size() != preview.size()
        || m_lastPreviewedInputPath != inputPath;

    m_currentPreviewImage = preview;
    if (m_correctPreviewOutliersCheckBox->isChecked()) {
        PreviewPixelCorrection::suppressIsolatedLumaOutliers(
            m_currentPreviewImage);
    }
    m_previewLabel->setSourceImage(m_currentPreviewImage);
    m_lastPreviewedInputPath = inputPath;
    m_previewWindow->setWindowTitle(
        tr("Preview - %1").arg(QFileInfo(inputPath).fileName()));
    if (shouldFitPreview) {
        const QSize viewportSize = m_previewScrollArea->viewport()->size();
        if (viewportSize.width() > 0 && viewportSize.height() > 0) {
            const double fitScale = std::min(static_cast<double>(viewportSize.width()) / static_cast<double>(preview.width()),
                                             static_cast<double>(viewportSize.height()) / static_cast<double>(preview.height()));
            const int fitZoom = std::clamp(static_cast<int>(std::floor(fitScale * 100.0)),
                                           m_previewZoomSlider->minimum(),
                                           m_previewZoomSlider->maximum());
            const bool oldSignals = m_previewZoomSlider->blockSignals(true);
            m_previewZoomSlider->setValue(fitZoom);
            m_previewZoomSlider->blockSignals(oldSignals);
        }
    }
    updatePreviewDisplay();
    showStatus(tr("Preview updated."));
    m_busy = false;
    updateControls(false);
}

void MainWindow::onExportPreview()
{
    QListWidgetItem *current = m_fileList->currentItem();
    if (!current || m_currentPreviewImage.isNull()) {
        showStatus(tr("Please render a preview before exporting."));
        return;
    }

    const QString inputPath = listItemPath(current);
    if (inputPath != m_lastPreviewedInputPath) {
        showStatus(tr("Please update the preview for the selected RAF file before exporting."));
        return;
    }

    QSettings settingsStore = appSettings();
    const QString fallbackFolder = !m_outputFolder->text().trimmed().isEmpty()
        ? m_outputFolder->text().trimmed()
        : QFileInfo(inputPath).absolutePath();
    QString initialFolder = settingsStore.value(QStringLiteral("previewExport/folder"), fallbackFolder).toString();
    if (initialFolder.isEmpty()) {
        initialFolder = fallbackFolder;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Export Preview"));
    dialog.setModal(true);

    QFormLayout *layout = new QFormLayout(&dialog);
    QLineEdit *folderEdit = new QLineEdit(initialFolder, &dialog);
    QPushButton *browseButton = new QPushButton(tr("Browse..."), &dialog);
    QHBoxLayout *folderLayout = new QHBoxLayout;
    folderLayout->addWidget(folderEdit, 1);
    folderLayout->addWidget(browseButton);
    layout->addRow(tr("Folder:"), folderLayout);

    QComboBox *formatComboBox = new QComboBox(&dialog);
    formatComboBox->addItem(tr("JPEG"), static_cast<int>(PreviewExportFormat::Jpeg));
    formatComboBox->addItem(tr("16-bit TIFF"), static_cast<int>(PreviewExportFormat::Tiff16));
    const int exportFormatSetting = settingsStore.value(
        QStringLiteral("previewExport/format"),
        static_cast<int>(PreviewExportFormat::Jpeg)).toInt();
    const int exportFormatIndex = formatComboBox->findData(exportFormatSetting);
    formatComboBox->setCurrentIndex(exportFormatIndex >= 0 ? exportFormatIndex : 0);
    layout->addRow(tr("Format:"), formatComboBox);

    QSpinBox *qualitySpinBox = new QSpinBox(&dialog);
    qualitySpinBox->setRange(1, 100);
    qualitySpinBox->setValue(std::clamp(settingsStore.value(QStringLiteral("previewExport/quality"), 90).toInt(), 1, 100));
    qualitySpinBox->setSuffix(tr("%"));
    layout->addRow(tr("JPEG quality:"), qualitySpinBox);
    QWidget *qualityLabel = layout->labelForField(qualitySpinBox);
    const auto updateQualityAvailability = [formatComboBox, qualitySpinBox, qualityLabel]() {
        const bool jpegSelected =
            formatComboBox->currentData().toInt() == static_cast<int>(PreviewExportFormat::Jpeg);
        qualitySpinBox->setEnabled(jpegSelected);
        if (qualityLabel) {
            qualityLabel->setEnabled(jpegSelected);
        }
    };
    connect(formatComboBox,
            &QComboBox::currentIndexChanged,
            &dialog,
            [updateQualityAvailability](int) { updateQualityAvailability(); });
    updateQualityAvailability();

    QComboBox *sizeComboBox = new QComboBox(&dialog);
    sizeComboBox->addItem(tr("12 MP"), static_cast<int>(PreviewExportSize::FullSize12Mp));
    sizeComboBox->addItem(tr("6 MP"), static_cast<int>(PreviewExportSize::SixMp));
    const int exportSizeSetting = settingsStore.contains(QStringLiteral("previewExport/size"))
        ? settingsStore.value(QStringLiteral("previewExport/size"), static_cast<int>(PreviewExportSize::FullSize12Mp)).toInt()
        : (settingsStore.value(QStringLiteral("previewExport/export6Mp"), false).toBool()
               ? static_cast<int>(PreviewExportSize::SixMp)
               : static_cast<int>(PreviewExportSize::FullSize12Mp));
    const int exportSizeIndex = sizeComboBox->findData(exportSizeSetting);
    sizeComboBox->setCurrentIndex(exportSizeIndex >= 0 ? exportSizeIndex : 0);
    layout->addRow(tr("Export size:"), sizeComboBox);

    QCheckBox *includeExifCheckBox = new QCheckBox(tr("Include EXIF metadata"), &dialog);
    includeExifCheckBox->setChecked(
        settingsStore.value(QStringLiteral("previewExport/includeExif"), true).toBool());
    layout->addRow(QString(), includeExifCheckBox);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addRow(buttonBox);

    connect(browseButton, &QPushButton::clicked, &dialog, [&dialog, folderEdit]() {
        const QString folder = QFileDialog::getExistingDirectory(&dialog,
                                                                 QObject::tr("Select Export Folder"),
                                                                 folderEdit->text().trimmed());
        if (!folder.isEmpty()) {
            folderEdit->setText(folder);
        }
    });
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString targetFolder = folderEdit->text().trimmed();
    if (targetFolder.isEmpty()) {
        showStatus(tr("Please select an export folder."));
        return;
    }

    QDir dir;
    if (!dir.mkpath(targetFolder)) {
        showStatus(tr("Could not create export folder."));
        return;
    }

    const PreviewExportSize exportSize = static_cast<PreviewExportSize>(sizeComboBox->currentData().toInt());
    const PreviewExportFormat exportFormat =
        static_cast<PreviewExportFormat>(formatComboBox->currentData().toInt());
    const bool includeExif = includeExifCheckBox->isChecked();
    const QString exportSizeSuffix = exportSize == PreviewExportSize::SixMp
        ? QStringLiteral("_6MP")
        : QStringLiteral("_12MP");
    const QString extension = exportFormat == PreviewExportFormat::Tiff16
        ? QStringLiteral(".tif")
        : QStringLiteral(".jpg");
    const QString outputPath = QDir(targetFolder).filePath(QFileInfo(inputPath).completeBaseName()
                                                           + QStringLiteral("_preview")
                                                           + exportSizeSuffix
                                                           + extension);
    if (QFile::exists(outputPath)) {
        const auto overwrite = QMessageBox::question(this,
                                                     tr("Overwrite Preview"),
                                                     tr("The file already exists:\n%1\n\nOverwrite it?").arg(outputPath));
        if (overwrite != QMessageBox::Yes) {
            return;
        }
    }

    QImage exportImage = buildAdjustedPreviewImage16();
    if (exportImage.isNull()) {
        showStatus(tr("Preview export failed."));
        return;
    }

    exportImage = resizeForPreviewExport(exportImage, exportSize);
    if (exportImage.isNull()) {
        showStatus(tr("Preview export resize failed."));
        return;
    }
    PreviewImageProcessing::applyLumaSharpening16(
        exportImage,
        m_previewSharpeningSlider->value());

    const int quality = qualitySpinBox->value();
    if (exportFormat == PreviewExportFormat::Tiff16) {
        QString tiffError;
        if (!DngWriter::writeRgbTiff16(outputPath, exportImage, tiffError)) {
            QFile::remove(outputPath);
            showStatus(tr("Could not save 16-bit preview TIFF: %1").arg(tiffError));
            return;
        }
    } else {
        const QImage jpegImage = exportImage.convertToFormat(QImage::Format_RGB32);
        if (!jpegImage.save(outputPath, "JPG", quality)) {
            showStatus(tr("Could not save preview JPG."));
            return;
        }
    }

    if (includeExif) {
        QString metadataError;
        if (!SuperCCDProcessor::copyExifMetadata(inputPath, outputPath, &metadataError)) {
            QFile::remove(outputPath);
            showStatus(tr("Could not attach EXIF metadata: %1").arg(metadataError));
            return;
        }
    }

    settingsStore.setValue(QStringLiteral("previewExport/folder"), targetFolder);
    settingsStore.setValue(QStringLiteral("previewExport/quality"), quality);
    settingsStore.setValue(QStringLiteral("previewExport/size"), sizeComboBox->currentData().toInt());
    settingsStore.setValue(QStringLiteral("previewExport/format"), formatComboBox->currentData().toInt());
    settingsStore.setValue(QStringLiteral("previewExport/includeExif"), includeExif);
    showStatus(tr("Preview exported to %1").arg(outputPath));
}

void MainWindow::updateControls(bool busy)
{
    m_fileList->setEnabled(!busy);
    m_outputFolder->setEnabled(!busy);
    m_rTransitionDelaySlider->setEnabled(!busy);
    m_rTransitionDelaySpinBox->setEnabled(!busy);
    m_rTransitionSmoothnessSlider->setEnabled(!busy);
    m_rTransitionSmoothnessSpinBox->setEnabled(!busy);
    m_previewZoomSlider->setEnabled(!busy);
    m_previewZoomSpinBox->setEnabled(!busy);
    m_previewExposureSlider->setEnabled(!busy);
    m_previewExposureSpinBox->setEnabled(!busy);
    m_previewWhiteBalanceSlider->setEnabled(!busy);
    m_previewWhiteBalanceSpinBox->setEnabled(!busy);
    m_previewTintSlider->setEnabled(!busy);
    m_previewTintSpinBox->setEnabled(!busy);
    m_whiteBalancePickerButton->setEnabled(!busy && hasCurrentPreview());
    m_previewGammaSlider->setEnabled(!busy);
    m_previewGammaSpinBox->setEnabled(!busy);
    m_previewContrastSlider->setEnabled(!busy);
    m_previewContrastSpinBox->setEnabled(!busy);
    m_previewShadowsSlider->setEnabled(!busy);
    m_previewShadowsSpinBox->setEnabled(!busy);
    m_previewShadowRangeSlider->setEnabled(!busy);
    m_previewShadowRangeSpinBox->setEnabled(!busy);
    m_previewSaturationSlider->setEnabled(!busy);
    m_previewSaturationSpinBox->setEnabled(!busy);
    m_previewSharpeningSlider->setEnabled(!busy);
    m_previewSharpeningSpinBox->setEnabled(!busy);
    m_previewHighlightCompressionSlider->setEnabled(!busy);
    m_previewHighlightCompressionSpinBox->setEnabled(!busy);
    m_previewRotationCombo->setEnabled(!busy);
    m_correctPreviewOutliersCheckBox->setEnabled(!busy);
    m_showPreviewButton->setEnabled(true);
    m_previewButton->setEnabled(!busy);
    const bool hasCurrentPreview = m_fileList->currentItem() != nullptr
        && !m_currentPreviewImage.isNull()
        && listItemPath(m_fileList->currentItem()) == m_lastPreviewedInputPath;
    m_exportPreviewButton->setEnabled(!busy && hasCurrentPreview);
    m_autoPreviewCheckBox->setEnabled(!busy);
    m_convertCurrentButton->setEnabled(!busy && m_fileList->currentItem() != nullptr);
    m_convertAllButton->setEnabled(!busy);
    m_resetDefaultsButton->setEnabled(!busy);
    m_saveDefaultsButton->setEnabled(!busy);
    if (busy) {
        m_statusLabel->setText(tr("Processing..."));
    }
}

void MainWindow::showPreviewWindow()
{
    if (m_previewWindow->isMinimized()) {
        m_previewWindow->setWindowState(
            m_previewWindow->windowState() & ~Qt::WindowMinimized);
    }
    m_previewWindow->show();
    m_previewWindow->raise();
    m_previewWindow->activateWindow();
}

void MainWindow::onPreviewZoomChanged(int value)
{
    Q_UNUSED(value);
    updatePreviewDisplay();
}

void MainWindow::onPreviewExposureChanged(int value)
{
    Q_UNUSED(value);
    updatePreviewDisplay();
}

void MainWindow::onPreviewWhiteBalanceChanged(int value)
{
    Q_UNUSED(value);
    updatePreviewDisplay();
}

void MainWindow::onPreviewTintChanged(int value)
{
    Q_UNUSED(value);
    updatePreviewDisplay();
}

void MainWindow::onWhiteBalancePickerToggled(bool enabled)
{
    m_previewDragging = false;
    m_whiteBalancePickerButton->setText(
        enabled ? tr("White Balance Picker: On") : tr("White Balance Picker: Off"));
    m_previewLabel->setWhiteBalancePickerEnabled(enabled);

    if (enabled) {
        const QPoint viewportCenter = m_previewScrollArea->viewport()->rect().center();
        const QPoint canvasCenter =
            m_previewLabel->mapFrom(m_previewScrollArea->viewport(), viewportCenter);
        m_previewLabel->setWhiteBalancePickerPosition(canvasCenter);
        m_previewLabel->setCursor(Qt::CrossCursor);
        showStatus(tr("White balance picker active. Wheel resizes the box; left-click samples neutral gray."));
    } else {
        m_previewLabel->setCursor(
            m_currentPreviewImage.isNull() ? Qt::ArrowCursor : Qt::OpenHandCursor);
    }
}

void MainWindow::onPreviewGammaChanged(int value)
{
    Q_UNUSED(value);
    updatePreviewDisplay();
}

void MainWindow::onPreviewContrastChanged(int value)
{
    Q_UNUSED(value);
    updatePreviewDisplay();
}

void MainWindow::onPreviewShadowsChanged(int value)
{
    Q_UNUSED(value);
    updatePreviewDisplay();
}

void MainWindow::onPreviewShadowRangeChanged(int value)
{
    Q_UNUSED(value);
    updatePreviewDisplay();
}

void MainWindow::onPreviewSaturationChanged(int value)
{
    Q_UNUSED(value);
    updatePreviewDisplay();
}

void MainWindow::onPreviewSharpeningChanged(int value)
{
    Q_UNUSED(value);
    updatePreviewDisplay();
}

void MainWindow::onPreviewHighlightCompressionChanged(int value)
{
    Q_UNUSED(value);
    updatePreviewDisplay();
}

QImage MainWindow::buildAdjustedPreviewImage16() const
{
    if (m_currentPreviewImage.isNull()) {
        return QImage();
    }

    QImage adjustedImage = m_currentPreviewImage.convertToFormat(QImage::Format_RGBX64);

    const double exposureEv = static_cast<double>(m_previewExposureSlider->value()) / 10.0;
    const double exposureScale = std::pow(2.0, exposureEv);
    const double wbBias = static_cast<double>(m_previewWhiteBalanceSlider->value()) / 100.0;
    const double redScale = std::pow(2.0, wbBias);
    const double blueScale = std::pow(2.0, -wbBias);
    const double tintBias = static_cast<double>(m_previewTintSlider->value()) / 100.0;
    const double greenScale = std::pow(2.0, -tintBias);
    constexpr double kOriginalGamma = 2.2;
    const double newGamma =
        std::max(static_cast<double>(m_previewGammaSlider->value()) / 100.0, 0.01);
    const double contrastBias = static_cast<double>(m_previewContrastSlider->value()) / 100.0;
    const double contrastScale = 1.0 + contrastBias;
    const double shadowRecovery =
        static_cast<double>(m_previewShadowsSlider->value()) / 100.0;
    const double shadowRange =
        static_cast<double>(m_previewShadowRangeSlider->value()) / 100.0;
    const double invOriginalGamma = 1.0 / kOriginalGamma;
    const double invNewGamma = 1.0 / newGamma;
    const double highlightCompression = static_cast<double>(m_previewHighlightCompressionSlider->value()) / 100.0;
    const double compressionStart = 200.0 - highlightCompression * 152.0;
    const double compressionStrength = highlightCompression * 2.0
        + highlightCompression * highlightCompression * 14.0;
    const auto buildToneLut = [&](double channelScale) {
        std::vector<quint16> lut(65536);
        for (int i = 0; i <= 65535; ++i) {
            double compressed = (static_cast<double>(i) / 257.0) * exposureScale * channelScale;
            if (highlightCompression > 0.0 && compressed > compressionStart) {
                const double excess = compressed - compressionStart;
                compressed = compressionStart + excess / (1.0 + excess * compressionStrength / 255.0);
            }

            double linear = std::pow(std::clamp(compressed / 255.0, 0.0, 1.0), invOriginalGamma);
            if (contrastScale != 1.0) {
                linear = (linear - 0.5) * contrastScale + 0.5;
            }
            linear = applyShadowRecoveryCurve(linear, shadowRecovery, shadowRange);
            const double output =
                std::pow(std::clamp(linear, 0.0, 1.0), invNewGamma) * 65535.0;
            lut[static_cast<size_t>(i)] = static_cast<quint16>(
                std::clamp(static_cast<int>(std::lround(output)), 0, 65535));
        }
        return lut;
    };

    const std::vector<quint16> redLut = buildToneLut(redScale);
    const std::vector<quint16> greenLut = buildToneLut(greenScale);
    const std::vector<quint16> blueLut = buildToneLut(blueScale);

    const double saturationBias = static_cast<double>(m_previewSaturationSlider->value()) / 100.0;
    const double saturationScale = 1.0 + saturationBias;

    for (int y = 0; y < adjustedImage.height(); ++y) {
        QRgba64 *scanLine = reinterpret_cast<QRgba64 *>(adjustedImage.scanLine(y));
        for (int x = 0; x < adjustedImage.width(); ++x) {
            const QRgba64 source = scanLine[x];
            int r = redLut[static_cast<size_t>(source.red())];
            int g = greenLut[static_cast<size_t>(source.green())];
            int b = blueLut[static_cast<size_t>(source.blue())];

            if (saturationScale != 1.0) {
                const double gray = (r + g + b) / 3.0;
                r = std::clamp(static_cast<int>(
                    std::lround(gray + (r - gray) * saturationScale)), 0, 65535);
                g = std::clamp(static_cast<int>(
                    std::lround(gray + (g - gray) * saturationScale)), 0, 65535);
                b = std::clamp(static_cast<int>(
                    std::lround(gray + (b - gray) * saturationScale)), 0, 65535);
            }

            scanLine[x] = QRgba64::fromRgba64(
                static_cast<quint16>(r),
                static_cast<quint16>(g),
                static_cast<quint16>(b),
                65535);
        }
    }

    superccd::suppressPreviewFalseColor(adjustedImage);
    return adjustedImage;
}

void MainWindow::updatePreviewDisplay()
{
    if (m_currentPreviewImage.isNull()) {
        return;
    }

    const QSize oldContentSize = m_previewLabel->size();
    const QSize viewportSize = m_previewScrollArea->viewport()->size();
    const double oldCenterX = oldContentSize.width() > 0
        ? (static_cast<double>(m_previewScrollArea->horizontalScrollBar()->value()) + viewportSize.width() * 0.5)
            / static_cast<double>(oldContentSize.width())
        : 0.5;
    const double oldCenterY = oldContentSize.height() > 0
        ? (static_cast<double>(m_previewScrollArea->verticalScrollBar()->value()) + viewportSize.height() * 0.5)
            / static_cast<double>(oldContentSize.height())
        : 0.5;

    const double zoom = static_cast<double>(m_previewZoomSlider->value()) / 100.0;

    PreviewAdjustmentValues adjustments;
    adjustments.exposureTenthsEv = m_previewExposureSlider->value();
    adjustments.whiteBalance = m_previewWhiteBalanceSlider->value();
    adjustments.tint = m_previewTintSlider->value();
    adjustments.gammaHundredths = m_previewGammaSlider->value();
    adjustments.contrast = m_previewContrastSlider->value();
    adjustments.shadows = m_previewShadowsSlider->value();
    adjustments.shadowRange = m_previewShadowRangeSlider->value();
    adjustments.saturation = m_previewSaturationSlider->value();
    adjustments.highlightCompression = m_previewHighlightCompressionSlider->value();
    m_previewLabel->setDisplayState(zoom, adjustments, 0);
    m_previewLabel->setCursor(
        m_whiteBalancePickerButton->isChecked() ? Qt::CrossCursor : Qt::OpenHandCursor);

    const int newHValue = static_cast<int>(
        oldCenterX * m_previewLabel->width() - viewportSize.width() * 0.5 + 0.5);
    const int newVValue = static_cast<int>(
        oldCenterY * m_previewLabel->height() - viewportSize.height() * 0.5 + 0.5);
    m_previewScrollArea->horizontalScrollBar()->setValue(std::max(0, newHValue));
    m_previewScrollArea->verticalScrollBar()->setValue(std::max(0, newVValue));

    if (m_previewSharpeningSlider->value() > 0) {
        m_previewSharpeningTimer->start();
    } else {
        m_previewSharpeningTimer->stop();
    }
}

void MainWindow::updateSharpenedPreviewDisplay()
{
    if (m_currentPreviewImage.isNull()
        || m_previewSharpeningSlider->value() <= 0) {
        return;
    }

    m_previewLabel->setSharpening(m_previewSharpeningSlider->value());
}

void MainWindow::showStatus(const QString &message)
{
    m_statusLabel->setText(message);
    m_statusClearTimer->start(5000);
}

void MainWindow::applyWhiteBalancePickerSample()
{
    const QRect sampleRect = m_previewLabel->whiteBalancePickerSourceRect();
    const std::optional<PreviewWhiteBalanceEstimate> estimate =
        PreviewImageProcessing::estimateNeutralWhiteBalance(
            m_currentPreviewImage,
            sampleRect);
    if (!estimate) {
        showStatus(tr("The selected area could not be used for white balance."));
        return;
    }

    const int whiteBalance = std::clamp(
        static_cast<int>(std::lround(estimate->whiteBalance)),
        m_previewWhiteBalanceSlider->minimum(),
        m_previewWhiteBalanceSlider->maximum());
    const int tint = std::clamp(
        static_cast<int>(std::lround(estimate->tint)),
        m_previewTintSlider->minimum(),
        m_previewTintSlider->maximum());

    const bool oldWhiteBalanceSignals =
        m_previewWhiteBalanceSlider->blockSignals(true);
    const bool oldTintSignals = m_previewTintSlider->blockSignals(true);
    const bool oldWhiteBalanceSpinSignals =
        m_previewWhiteBalanceSpinBox->blockSignals(true);
    const bool oldTintSpinSignals = m_previewTintSpinBox->blockSignals(true);
    m_previewWhiteBalanceSlider->setValue(whiteBalance);
    m_previewTintSlider->setValue(tint);
    m_previewWhiteBalanceSpinBox->setValue(whiteBalance);
    m_previewTintSpinBox->setValue(tint);
    m_previewWhiteBalanceSlider->blockSignals(oldWhiteBalanceSignals);
    m_previewTintSlider->blockSignals(oldTintSignals);
    m_previewWhiteBalanceSpinBox->blockSignals(oldWhiteBalanceSpinSignals);
    m_previewTintSpinBox->blockSignals(oldTintSpinSignals);
    updatePreviewDisplay();

    showStatus(tr("White balance sampled: %1, tint: %2.")
                   .arg(whiteBalance)
                   .arg(tint));
}

QPointF MainWindow::previewCanvasPosition(
    QObject *watched,
    const QPointF &position) const
{
    if (watched == m_previewLabel) {
        return position;
    }
    return m_previewLabel->mapFrom(
        m_previewScrollArea->viewport(),
        position.toPoint());
}

bool MainWindow::hasCurrentPreview() const
{
    return m_fileList->currentItem() != nullptr
        && !m_currentPreviewImage.isNull()
        && listItemPath(m_fileList->currentItem()) == m_lastPreviewedInputPath;
}

void MainWindow::applyParameterSettings(const ConversionSettings &settings)
{
    const bool oldDelaySignals = m_rTransitionDelaySlider->blockSignals(true);
    const bool oldSmoothnessSignals = m_rTransitionSmoothnessSlider->blockSignals(true);
    const bool oldDelaySpinSignals = m_rTransitionDelaySpinBox->blockSignals(true);
    const bool oldSmoothnessSpinSignals =
        m_rTransitionSmoothnessSpinBox->blockSignals(true);
    const bool oldCorrectionSignals =
        m_correctPreviewOutliersCheckBox->blockSignals(true);
    const int delayValue =
        std::clamp(static_cast<int>(settings.rTransitionDelay * 100.0 + 0.5),
                   0,
                   100);
    const int smoothnessValue =
        std::clamp(static_cast<int>(settings.rTransitionSmoothness * 100.0 + 0.5),
                   0,
                   100);
    m_rTransitionDelaySlider->setValue(delayValue);
    m_rTransitionDelaySpinBox->setValue(delayValue);
    m_rTransitionSmoothnessSlider->setValue(smoothnessValue);
    m_rTransitionSmoothnessSpinBox->setValue(smoothnessValue);
    m_correctPreviewOutliersCheckBox->setChecked(
        settings.correctPreviewOutliers);
    m_rTransitionDelaySlider->blockSignals(oldDelaySignals);
    m_rTransitionSmoothnessSlider->blockSignals(oldSmoothnessSignals);
    m_rTransitionDelaySpinBox->blockSignals(oldDelaySpinSignals);
    m_rTransitionSmoothnessSpinBox->blockSignals(oldSmoothnessSpinSignals);
    m_correctPreviewOutliersCheckBox->blockSignals(oldCorrectionSignals);
}

void MainWindow::loadSavedDefaults()
{
    QSettings settingsStore = appSettings();
    ConversionSettings defaults;
    defaults.exportMode = ExportMode::RawCfa6MP;
    defaults.rTransitionDelay = settingsStore.value(QStringLiteral("defaults/rTransitionDelay"), 0.5).toDouble();
    defaults.rTransitionSmoothness = settingsStore.value(QStringLiteral("defaults/rTransitionSmoothness"), 0.5).toDouble();
    defaults.correctPreviewOutliers =
        settingsStore.value(
            QStringLiteral("defaults/correctPreviewOutliers"),
            settingsStore.value(
                QStringLiteral("defaults/correctPreviewCfaOutliers"),
                false)).toBool();

    applyParameterSettings(defaults);

    const int previewExposure = settingsStore.value(QStringLiteral("defaults/previewExposureSlider"),
                                                    kDefaultPreviewExposureSliderValue).toInt();
    m_previewExposureSlider->setValue(std::clamp(previewExposure,
                                                 m_previewExposureSlider->minimum(),
                                                 m_previewExposureSlider->maximum()));
    const int previewWhiteBalance = settingsStore.value(QStringLiteral("defaults/previewWhiteBalanceSlider"),
                                                        kDefaultPreviewWhiteBalanceSliderValue).toInt();
    m_previewWhiteBalanceSlider->setValue(std::clamp(previewWhiteBalance,
                                                     m_previewWhiteBalanceSlider->minimum(),
                                                     m_previewWhiteBalanceSlider->maximum()));
    const int previewTint = settingsStore.value(QStringLiteral("defaults/previewTintSlider"),
                                                kDefaultPreviewTintSliderValue).toInt();
    m_previewTintSlider->setValue(std::clamp(previewTint,
                                             m_previewTintSlider->minimum(),
                                             m_previewTintSlider->maximum()));
    const int previewGamma = settingsStore.value(QStringLiteral("defaults/previewGammaSlider"),
                                                 kDefaultPreviewGammaSliderValue).toInt();
    m_previewGammaSlider->setValue(std::clamp(previewGamma,
                                              m_previewGammaSlider->minimum(),
                                              m_previewGammaSlider->maximum()));
    const int previewContrast = settingsStore.value(QStringLiteral("defaults/previewContrastSlider"),
                                                    kDefaultPreviewContrastSliderValue).toInt();
    m_previewContrastSlider->setValue(std::clamp(previewContrast,
                                                 m_previewContrastSlider->minimum(),
                                                 m_previewContrastSlider->maximum()));
    const int previewShadows = settingsStore.value(QStringLiteral("defaults/previewShadowsSlider"),
                                                   kDefaultPreviewShadowsSliderValue).toInt();
    m_previewShadowsSlider->setValue(std::clamp(previewShadows,
                                                m_previewShadowsSlider->minimum(),
                                                m_previewShadowsSlider->maximum()));
    const int previewShadowRange = settingsStore.value(QStringLiteral("defaults/previewShadowRangeSlider"),
                                                       kDefaultPreviewShadowRangeSliderValue).toInt();
    m_previewShadowRangeSlider->setValue(std::clamp(previewShadowRange,
                                                    m_previewShadowRangeSlider->minimum(),
                                                    m_previewShadowRangeSlider->maximum()));
    const int previewSaturation = settingsStore.value(QStringLiteral("defaults/previewSaturationSlider"),
                                                      kDefaultPreviewSaturationSliderValue).toInt();
    m_previewSaturationSlider->setValue(std::clamp(previewSaturation,
                                                   m_previewSaturationSlider->minimum(),
                                                   m_previewSaturationSlider->maximum()));
    const int previewSharpening = settingsStore.value(QStringLiteral("defaults/previewSharpeningSlider"),
                                                      kDefaultPreviewSharpeningSliderValue).toInt();
    m_previewSharpeningSlider->setValue(std::clamp(previewSharpening,
                                                   m_previewSharpeningSlider->minimum(),
                                                   m_previewSharpeningSlider->maximum()));
    const int previewHighlightCompression = settingsStore.value(QStringLiteral("defaults/previewHighlightCompressionSlider"),
                                                                kDefaultPreviewHighlightCompressionSliderValue).toInt();
    m_previewHighlightCompressionSlider->setValue(std::clamp(previewHighlightCompression,
                                                             m_previewHighlightCompressionSlider->minimum(),
                                                             m_previewHighlightCompressionSlider->maximum()));
    const int previewZoom = settingsStore.value(QStringLiteral("defaults/previewZoomSlider"),
                                                kDefaultPreviewZoomSliderValue).toInt();
    m_previewZoomSlider->setValue(std::clamp(previewZoom,
                                             m_previewZoomSlider->minimum(),
                                             m_previewZoomSlider->maximum()));
    const int previewRotation = settingsStore.value(QStringLiteral("defaults/previewRotation"), 0).toInt();
    const int rotationIndex = m_previewRotationCombo->findData(previewRotation);
    m_previewRotationCombo->setCurrentIndex(rotationIndex >= 0 ? rotationIndex : 0);

    m_autoPreviewCheckBox->setChecked(settingsStore.value(QStringLiteral("defaults/autoPreview"),
                                                          kDefaultAutoPreview).toBool());
}

void MainWindow::saveCurrentDefaults() const
{
    QSettings settingsStore = appSettings();
    const ConversionSettings settings = currentSettings();
    settingsStore.setValue(QStringLiteral("defaults/rTransitionDelay"), settings.rTransitionDelay);
    settingsStore.setValue(QStringLiteral("defaults/rTransitionSmoothness"), settings.rTransitionSmoothness);
    settingsStore.setValue(
        QStringLiteral("defaults/correctPreviewOutliers"),
        settings.correctPreviewOutliers);
    settingsStore.setValue(QStringLiteral("defaults/previewExposureSlider"), m_previewExposureSlider->value());
    settingsStore.setValue(QStringLiteral("defaults/previewWhiteBalanceSlider"), m_previewWhiteBalanceSlider->value());
    settingsStore.setValue(QStringLiteral("defaults/previewTintSlider"), m_previewTintSlider->value());
    settingsStore.setValue(QStringLiteral("defaults/previewGammaSlider"), m_previewGammaSlider->value());
    settingsStore.setValue(QStringLiteral("defaults/previewContrastSlider"), m_previewContrastSlider->value());
    settingsStore.setValue(QStringLiteral("defaults/previewShadowsSlider"), m_previewShadowsSlider->value());
    settingsStore.setValue(QStringLiteral("defaults/previewShadowRangeSlider"), m_previewShadowRangeSlider->value());
    settingsStore.setValue(QStringLiteral("defaults/previewSaturationSlider"), m_previewSaturationSlider->value());
    settingsStore.setValue(QStringLiteral("defaults/previewSharpeningSlider"), m_previewSharpeningSlider->value());
    settingsStore.setValue(QStringLiteral("defaults/previewHighlightCompressionSlider"), m_previewHighlightCompressionSlider->value());
    settingsStore.setValue(QStringLiteral("defaults/previewZoomSlider"), m_previewZoomSlider->value());
    settingsStore.setValue(QStringLiteral("defaults/previewRotation"), m_previewRotationCombo->currentData().toInt());
    settingsStore.setValue(QStringLiteral("defaults/autoPreview"), m_autoPreviewCheckBox->isChecked());
}

void MainWindow::onSaveDefaults()
{
    saveCurrentDefaults();
    showStatus(tr("Defaults saved."));
}

void MainWindow::onResetDefaults()
{
    ConversionSettings defaults;
    defaults.exportMode = ExportMode::RawCfa6MP;
    defaults.rTransitionDelay = 0.5;
    defaults.rTransitionSmoothness = 0.5;
    defaults.correctPreviewOutliers = false;
    applyParameterSettings(defaults);
    m_previewZoomSlider->setValue(kDefaultPreviewZoomSliderValue);
    m_previewExposureSlider->setValue(kDefaultPreviewExposureSliderValue);
    m_previewWhiteBalanceSlider->setValue(kDefaultPreviewWhiteBalanceSliderValue);
    m_previewTintSlider->setValue(kDefaultPreviewTintSliderValue);
    m_previewGammaSlider->setValue(kDefaultPreviewGammaSliderValue);
    m_previewContrastSlider->setValue(kDefaultPreviewContrastSliderValue);
    m_previewShadowsSlider->setValue(kDefaultPreviewShadowsSliderValue);
    m_previewShadowRangeSlider->setValue(kDefaultPreviewShadowRangeSliderValue);
    m_previewSaturationSlider->setValue(kDefaultPreviewSaturationSliderValue);
    m_previewSharpeningSlider->setValue(kDefaultPreviewSharpeningSliderValue);
    m_previewHighlightCompressionSlider->setValue(kDefaultPreviewHighlightCompressionSliderValue);
    m_previewRotationCombo->setCurrentIndex(0);
    m_autoPreviewCheckBox->setChecked(kDefaultAutoPreview);
    queueAutoPreview();
    showStatus(tr("Defaults restored."));
}

void MainWindow::queueAutoPreview()
{
    if (m_busy || !m_autoPreviewCheckBox->isChecked()) {
        return;
    }
    if (!m_fileList->currentItem()) {
        return;
    }
    m_autoPreviewTimer->start();
}

void MainWindow::onAutoPreviewTimer()
{
    if (m_busy || !m_autoPreviewCheckBox->isChecked()) {
        return;
    }
    onUpdatePreview();
}

ConversionSettings MainWindow::currentSettings() const
{
    ConversionSettings settings;
    settings.exportMode = ExportMode::RawCfa6MP;
    settings.previewMaxSize = 0;
    settings.rTransitionDelay = static_cast<double>(m_rTransitionDelaySlider->value()) / 100.0;
    settings.rTransitionSmoothness = static_cast<double>(m_rTransitionSmoothnessSlider->value()) / 100.0;
    settings.previewRotation = m_previewRotationCombo->currentData().toInt();
    settings.linearChromaSuppression = 1.0;
    settings.correctPreviewOutliers =
        m_correctPreviewOutliersCheckBox->isChecked();
    settings.exportPlaneImages = m_exportPlaneImagesCheckBox->isChecked();
    return settings;
}
