#include "MainWindow.h"
#include "DngWriter.h"
#include "ExposureToolsWindow.h"
#include "PresetManager.h"
#include "ImageSettingsManager.h"
#include "PreviewCanvas.h"
#include "PreviewImageProcessing.h"
#include "TransitionCurveWidget.h"

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
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
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
#include <QStyle>
#include <QStyleOptionGroupBox>
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

namespace {
constexpr int kDefaultDelaySliderValue = 20;
constexpr int kDefaultSmoothnessSliderValue = 50;
// Default slider position for Merge start. The slider is non-linear: 0..80 maps
// linearly to start = 0..0.9985 and 80..100 is a smoothstep up to 1.0. Position
// 72 is start ~= 0.90: S stays linear until it clips and has about 8x less noise
// than R, so R only takes over just below clipping.
constexpr int kDefaultStartSliderValue = 72;
constexpr double kDefaultMergeStart = 0.90;
constexpr double kDefaultMergeWidth = 0.20;
constexpr int kDefaultPreviewZoomSliderValue = 35;
constexpr bool kDefaultAutoPreview = true;
constexpr int kPreviewExportSixMpShortSide = 2016;
constexpr int kMinimumKelvin = 2000;
constexpr int kMaximumKelvin = 20000;
constexpr int kTemperatureSliderSteps = 1000;

// The Temperature slider is uniform in mired, because equal Kelvin steps are huge
// color changes at low temperatures and invisible ones at high temperatures.
int kelvinFromSlider(int position)
{
    const double low = 1.0e6 / kMaximumKelvin;
    const double high = 1.0e6 / kMinimumKelvin;
    const double t = static_cast<double>(position) / kTemperatureSliderSteps;
    const double kelvin = 1.0e6 / (high + (low - high) * t);
    return static_cast<int>(std::lround(kelvin / 10.0) * 10);
}

int sliderFromKelvin(int kelvin)
{
    const double low = 1.0e6 / kMaximumKelvin;
    const double high = 1.0e6 / kMinimumKelvin;
    const double mired = 1.0e6 / std::clamp(kelvin, kMinimumKelvin, kMaximumKelvin);
    return static_cast<int>(std::lround((high - mired) / (high - low) * kTemperatureSliderSteps));
}
constexpr int kItemPreviewRotationRole = Qt::UserRole + 1;

enum class PreviewExportSize {
    FullSize = 0,
    SixMp = 1
};

enum class PreviewExportFormat {
    Jpeg = 0,
    Tiff16 = 1
};

// --- Merge start slider mapping -------------------------------------------
// The Merge start slider (0..100) is a non-linear control over the actual
// S-brightness start (0..1): the first 80% of the slider is linear
// (0..0.9985) and the last 20% is a smoothstep that lets the user reach
// pure S (start = 1.0) with fine control. The fine zone is intentionally
// narrow because the useful adjustments live in the very top fraction of
// the S brightness range -- in practice the difference between start =
// 0.99 and start = 1.0 is far too coarse to be useful on a 100-step
// slider, so the linear zone is pushed almost all the way to 1.0 and the
// fine zone covers only the last 0.15%.
namespace {
constexpr double kMergeStartSliderLinearCeiling = 80.0;
constexpr double kMergeStartLinearMax = 0.9985;

double mergeStartFromSlider(int sliderValue)
{
    const double s = std::clamp(static_cast<double>(sliderValue), 0.0, 100.0);
    if (s <= kMergeStartSliderLinearCeiling) {
        return s * (kMergeStartLinearMax / kMergeStartSliderLinearCeiling);
    }
    const double u = (s - kMergeStartSliderLinearCeiling)
                     / (100.0 - kMergeStartSliderLinearCeiling);
    return kMergeStartLinearMax + (1.0 - kMergeStartLinearMax) * (u * u * (3.0 - 2.0 * u));
}

int sliderFromMergeStart(double start)
{
    const double st = std::clamp(start, 0.0, 1.0);
    if (st <= kMergeStartLinearMax) {
        return static_cast<int>(std::lround(
            st * (kMergeStartSliderLinearCeiling / kMergeStartLinearMax)));
    }
    // Invert the smoothstep on the last 20%: solve 3u^2 - 2u^3 = s
    // (where s = (start - 0.99) / 0.01) using Newton's method.
    const double s = (st - kMergeStartLinearMax) / (1.0 - kMergeStartLinearMax);
    double u = 0.5;
    for (int i = 0; i < 50; ++i) {
        const double f = 3.0 * u * u - 2.0 * u * u * u - s;
        const double fp = 6.0 * u - 6.0 * u * u;
        if (std::abs(fp) < 1e-12) {
            break;
        }
        const double next = u - f / fp;
        u = std::clamp(next, 0.0, 1.0);
        if (std::abs(next - u) < 1e-9) {
            break;
        }
    }
    const double slider = kMergeStartSliderLinearCeiling
                          + (100.0 - kMergeStartSliderLinearCeiling) * u;
    return static_cast<int>(std::lround(std::clamp(slider, 0.0, 100.0)));
}
} // namespace

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

int collapsedGroupBoxHeight(const QGroupBox *groupBox)
{
    if (!groupBox) {
        return 0;
    }

    QStyleOptionGroupBox option;
    option.initFrom(groupBox);
    option.text = groupBox->title();
    option.lineWidth = groupBox->style()->pixelMetric(QStyle::PM_DefaultFrameWidth, &option, groupBox);
    option.subControls = QStyle::SC_GroupBoxFrame | QStyle::SC_GroupBoxLabel | QStyle::SC_GroupBoxCheckBox;

    const QSize collapsedSize =
        groupBox->style()->sizeFromContents(QStyle::CT_GroupBox, &option, QSize(0, 0), groupBox);
    return std::max(collapsedSize.height(), groupBox->fontMetrics().height() + 14);
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
    if (image.isNull() || exportSize == PreviewExportSize::FullSize) {
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
    , m_rTransitionStartSlider(new QSlider(Qt::Horizontal, this))
    , m_rTransitionDelaySlider(new QSlider(Qt::Horizontal, this))
    , m_rTransitionSmoothnessSlider(new QSlider(Qt::Horizontal, this))
    , m_rLumaNoiseReductionSlider(new QSlider(Qt::Horizontal, this))
    , m_chromaNoiseReductionSlider(new QSlider(Qt::Horizontal, this))
    , m_previewZoomSlider(new QSlider(Qt::Horizontal, this))
    , m_whiteBalancePickerButton(new QPushButton(tr("White Balance Picker: Off"), this))
    , m_previewRotationCombo(new QComboBox(this))
    , m_previewMethodRow(new QWidget(this))
    , m_previewMethodReconstructionButton(new QRadioButton(tr("Reconstruction"), this))
    , m_previewMethodAmazeButton(new QRadioButton(tr("Amaze debayer"), this))
    , m_previewMethodGroup(new QButtonGroup(this))
    , m_correctPreviewOutliersCheckBox(
          new QCheckBox(tr("Correct isolated light/dark pixels"), this))
    , m_autoPreviewCheckBox(new QCheckBox(tr("Update preview automatically"), this))
    , m_showExposureToolsCheckBox(new QCheckBox(tr("Exposure tools window"), this))
    , m_exposureToolsWindow(new ExposureToolsWindow(this))
    , m_previewWindow(new QWidget(this, Qt::Window))
    , m_previewScrollArea(new QScrollArea(m_previewWindow))
    , m_previewLabel(new PreviewCanvas(m_previewScrollArea))
    , m_showPreviewButton(new QPushButton(tr("Show Preview"), this))
    , m_previewButton(new QPushButton(tr("Update Preview"), this))
    , m_exportPreviewButton(new QPushButton(tr("Export Preview"), this))
    , m_convertCurrentButton(new QPushButton(tr("Convert"), this))
    , m_convertAllButton(new QPushButton(tr("Convert All"), this))
    , m_exportPlaneImagesCheckBox(new QCheckBox(tr("Export S/R Planes"), this))
    , m_presetCombo(new QComboBox(this))
    , m_savePresetButton(new QPushButton(tr("Save current"), this))
    , m_resetPresetButton(new QPushButton(tr("Reset"), this))
    , m_presetManager(new PresetManager())
    , m_imageSettingsManager(new ImageSettingsManager())
    , m_statusLabel(new QLabel(tr("Ready."), this))
    , m_statusClearTimer(new QTimer(this))
    , m_autoPreviewTimer(new QTimer(this))
    , m_exposureScopeTimer(new QTimer(this))
    , m_rTransitionStartSpinBox(new QSpinBox(this))
    , m_rTransitionDelaySpinBox(new QSpinBox(this))
    , m_rTransitionSmoothnessSpinBox(new QSpinBox(this))
    , m_rLumaNoiseReductionSpinBox(new QSpinBox(this))
    , m_chromaNoiseReductionSpinBox(new QSpinBox(this))
    , m_transitionCurveWidget(new TransitionCurveWidget(this))
    , m_previewZoomSpinBox(new QSpinBox(this))
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

    m_exposureToolsWindow->hide();
    m_showExposureToolsCheckBox->setToolTip(
        tr("Show or hide the floating Exposure Tools window (Histogram, "
           "RGB/Luma Waveform)."));

    QPushButton *addFilesButton = new QPushButton(tr("Add RAF Files..."), this);
    QPushButton *removeFilesButton = new QPushButton(tr("Remove Selected"), this);
    QPushButton *selectFolderButton = new QPushButton(tr("Select"), this);
    QLabel *warmupNoteLabel = new QLabel(
        tr("Note: the first preview or conversion for a RAF file is slower because the app has to decode and cache the raw data."),
        this);
    m_rTransitionStartSlider->setRange(0, 100);
    m_rTransitionStartSlider->setValue(kDefaultStartSliderValue);
    m_rTransitionStartSlider->setToolTip(
        tr("S brightness (as a percentage of the channel white point) at which the S->R merge begins. "
           "Pixels darker than this are pure S.\n\n"
           "The slider is non-linear: 0..80 maps to start=0..0.9985 linearly, "
           "and 80..100 is a smoothstep that lets you reach start=1.0 (pure S, no R detail) "
           "with fine control. Position 100 means no merge at all."));
    // Transition width slider minimum is 1 (not 0) so the merge curve
    // is always visible and there's always a real transition region.
    m_rTransitionDelaySlider->setRange(1, 100);
    m_rTransitionDelaySlider->setValue(kDefaultDelaySliderValue);
    m_rTransitionDelaySlider->setToolTip(
        tr("Width of the S->R transition in S-brightness units. 1 = the narrowest "
           "transition the UI allows, 100 = the widest transition the algorithm "
           "allows (0.40 of the S range). The end of the transition is clamped so "
           "it never exceeds the maximum S brightness."));
    m_rTransitionSmoothnessSlider->setRange(0, 100);
    m_rTransitionSmoothnessSlider->setValue(kDefaultSmoothnessSliderValue);
    m_rTransitionSmoothnessSlider->setToolTip(
        tr("Eases the shape of the merge: 0 = a straight line, 100 = a sharp "
           "exponential ramp that approaches a 90 degree shoulder."));
    m_rLumaNoiseReductionSlider->setRange(0, 100);
    m_rLumaNoiseReductionSlider->setValue(0);
    m_rLumaNoiseReductionSlider->setToolTip(
        tr("Noise reduction of the R pixels before merging, scaled to their measured "
           "noise. 0 = off. Stronger settings smooth recovered highlights more, and it "
           "also applies to the DNG."));
    m_chromaNoiseReductionSlider->setRange(0, 100);
    m_chromaNoiseReductionSlider->setValue(0);
    m_chromaNoiseReductionSlider->setToolTip(
        tr("Smooths color noise in noisy areas: shadows, and R highlights at high ISO. "
           "0 = off. Only affects the Amaze debayer preview and its exports, not the DNG."));
    m_previewZoomSlider->setRange(5, 400);
    m_previewZoomSlider->setValue(kDefaultPreviewZoomSliderValue);
    m_whiteBalancePickerButton->setCheckable(true);
    m_whiteBalancePickerButton->setToolTip(
        tr("Turn on the picker, move the box over a neutral gray area, use the "
           "mouse wheel to resize it, and left-click to set white balance and tint."));
    m_previewRotationCombo->addItem(tr("Normal"), 0);
    m_previewRotationCombo->addItem(tr("Rotate 90 CW"), 90);
    m_previewRotationCombo->addItem(tr("Rotate 180"), 180);
    m_previewRotationCombo->addItem(tr("Rotate 90 CCW"), 270);
    m_previewMethodGroup->addButton(m_previewMethodReconstructionButton, static_cast<int>(PreviewMethod::Reconstruction));
    m_previewMethodGroup->addButton(m_previewMethodAmazeButton, static_cast<int>(PreviewMethod::AmazeDebayer));
    m_previewMethodAmazeButton->setChecked(true);
    m_previewMethodAmazeButton->setToolTip(
        tr("Demosaics the merged S/R CFA with AMaZE at the native 6 MP of the sensor. "
           "Keeps fine color detail best. Previews and exports are 6 MP."));
    {
        QHBoxLayout *methodLayout = new QHBoxLayout(m_previewMethodRow);
        methodLayout->setContentsMargins(0, 0, 0, 0);
        methodLayout->addWidget(m_previewMethodReconstructionButton);
        methodLayout->addWidget(m_previewMethodAmazeButton);
        methodLayout->addStretch();
    }
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
    m_exposureScopeTimer->setSingleShot(true);
    // 75 ms is fast enough to feel live (≈13 Hz) while reducing the
    // number of full-image applyDisplayAdjustments() rebuilds per
    // slider drag from 30+ to ~13. On a Raspberry Pi this is the
    // difference between the Exposure slider feeling sluggish and
    // feeling responsive while the scopes window is open.
    m_exposureScopeTimer->setInterval(75);
    warmupNoteLabel->setWordWrap(true);
    warmupNoteLabel->setStyleSheet(QStringLiteral("color:#808080;"));

    // Configure spinboxes to work with sliders
    // Start spinbox: 0 to 100 (slider position, NOT the actual start
    // value: the slider is non-linear, see slider tooltip for details).
    m_rTransitionStartSpinBox->setRange(0, 100);
    m_rTransitionStartSpinBox->setValue(kDefaultStartSliderValue);
    m_rTransitionStartSpinBox->setSingleStep(1);
    m_rTransitionStartSpinBox->setSuffix(QStringLiteral("%"));
    m_rTransitionStartSpinBox->setToolTip(
        tr("Slider position (0-100), not the actual S brightness. "
           "The mapping is non-linear: 0..80 = start 0..0.9985, "
           "80..100 = start 0.9985..1.0 (smoothstep)."));
    // Delay spinbox: 1 to 100 (matches the slider minimum)
    m_rTransitionDelaySpinBox->setRange(1, 100);
    m_rTransitionDelaySpinBox->setValue(kDefaultDelaySliderValue);
    m_rTransitionDelaySpinBox->setSingleStep(1);
    
    // Smoothness spinbox: 0 to 100 (represents 0.00 to 1.00)
    m_rTransitionSmoothnessSpinBox->setRange(0, 100);
    m_rTransitionSmoothnessSpinBox->setValue(kDefaultSmoothnessSliderValue);
    m_rTransitionSmoothnessSpinBox->setSingleStep(1);

    // R luma NR spinbox: 0 to 100 (represents 0.00 to 1.00)
    m_rLumaNoiseReductionSpinBox->setRange(0, 100);
    m_rLumaNoiseReductionSpinBox->setValue(0);
    m_rLumaNoiseReductionSpinBox->setSingleStep(1);
    m_chromaNoiseReductionSpinBox->setRange(0, 100);
    m_chromaNoiseReductionSpinBox->setValue(0);
    m_chromaNoiseReductionSpinBox->setSingleStep(1);

    // Zoom spinbox: 5% to 400%
    m_previewZoomSpinBox->setRange(5, 400);
    m_previewZoomSpinBox->setValue(kDefaultPreviewZoomSliderValue);
    m_previewZoomSpinBox->setSuffix(QStringLiteral("%"));
    m_previewZoomSpinBox->setSingleStep(5);
    



    




    
    
    

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

    // Transition controls group box (collapsible via the group's built-in checkable title)
    QGroupBox *transitionGroup = new QGroupBox(tr("Transition Settings"), this);
    transitionGroup->setCheckable(true);
    const bool transitionExpandedDefault = true;
    transitionGroup->setChecked(appSettings().value(
        QStringLiteral("ui/transitionGroupExpanded"), transitionExpandedDefault).toBool());
    QFormLayout *transitionLayout = new QFormLayout(transitionGroup);

    QHBoxLayout *startLayout = new QHBoxLayout;
    startLayout->addWidget(m_rTransitionStartSlider, 1);
    startLayout->addWidget(m_rTransitionStartSpinBox, 0);
    transitionLayout->addRow(tr("Merge start:"), startLayout);

    QHBoxLayout *delayLayout = new QHBoxLayout;
    delayLayout->addWidget(m_rTransitionDelaySlider, 1);
    delayLayout->addWidget(m_rTransitionDelaySpinBox, 0);
    transitionLayout->addRow(tr("Transition width:"), delayLayout);

    QHBoxLayout *smoothnessLayout = new QHBoxLayout;
    smoothnessLayout->addWidget(m_rTransitionSmoothnessSlider, 1);
    smoothnessLayout->addWidget(m_rTransitionSmoothnessSpinBox, 0);
    transitionLayout->addRow(tr("Smoothness:"), smoothnessLayout);

    QHBoxLayout *lumaNRLayout = new QHBoxLayout;
    lumaNRLayout->addWidget(m_rLumaNoiseReductionSlider, 1);
    lumaNRLayout->addWidget(m_rLumaNoiseReductionSpinBox, 0);
    transitionLayout->addRow(tr("R noise reduction:"), lumaNRLayout);

    QHBoxLayout *chromaNRLayout = new QHBoxLayout;
    chromaNRLayout->addWidget(m_chromaNoiseReductionSlider, 1);
    chromaNRLayout->addWidget(m_chromaNoiseReductionSpinBox, 0);
    transitionLayout->addRow(tr("Chroma noise reduction:"), chromaNRLayout);

    QLabel *transitionCurveLabel = new QLabel(tr("Merge curve:"), this);
    transitionLayout->addRow(transitionCurveLabel, m_transitionCurveWidget);

    // When the user toggles the Transition Settings group's checkbox, hide
    // (or show) the group's children and clamp the collapsed height so the
    // title bar stays visible without leaving a large empty frame behind.
    auto setTransitionChildrenVisible = [transitionGroup](bool visible) {
        // Walk every item in the form layout (rows can contain either a
        // widget or a nested layout holding the actual widgets).
        for (int i = 0; i < transitionGroup->layout()->count(); ++i) {
            QLayoutItem *item = transitionGroup->layout()->itemAt(i);
            if (!item) {
                continue;
            }
            if (QWidget *widget = item->widget()) {
                widget->setVisible(visible);
            } else if (QLayout *nested = item->layout()) {
                for (int j = 0; j < nested->count(); ++j) {
                    if (QWidget *inner = nested->itemAt(j)->widget()) {
                        inner->setVisible(visible);
                    }
                }
            }
        }
    };

    auto updateTransitionGroupHeight = [transitionGroup](bool expanded) {
        if (expanded) {
            transitionGroup->setMaximumHeight(QWIDGETSIZE_MAX);
        } else {
            transitionGroup->setMaximumHeight(collapsedGroupBoxHeight(transitionGroup));
        }
        transitionGroup->updateGeometry();
        if (QWidget *parent = transitionGroup->parentWidget()) {
            if (QLayout *parentLayout = parent->layout()) {
                parentLayout->invalidate();
                parentLayout->activate();
            }
        }
    };

    connect(transitionGroup, &QGroupBox::toggled, this, [setTransitionChildrenVisible,
                                                         updateTransitionGroupHeight](bool checked) {
        appSettings().setValue(QStringLiteral("ui/transitionGroupExpanded"), checked);
        setTransitionChildrenVisible(checked);
        updateTransitionGroupHeight(checked);
    });

    // Apply the initial state (which may have been restored from settings).
    updateTransitionGroupHeight(transitionGroup->isChecked());
    if (!transitionGroup->isChecked()) {
        setTransitionChildrenVisible(false);
    }

    // Preview controls group box (wrapped in scrollable area)
    QGroupBox *previewGroup = new QGroupBox(tr("Preview Controls"), this);
    QFormLayout *previewControlsLayout = new QFormLayout(previewGroup);

    // First control: toggle that shows or hides the floating Exposure Tools
    // window (Histogram, Waveform).
    previewControlsLayout->addRow(m_showExposureToolsCheckBox);

    previewControlsLayout->addRow(tr("Method:"), m_previewMethodRow);

    const auto addDevelopGroup = [&](const QString &title) {
        QFrame *frame = new QFrame(this);
        frame->setObjectName(QStringLiteral("developGroupFrame"));
        frame->setFrameShape(QFrame::StyledPanel);
        frame->setStyleSheet(QStringLiteral(
            "QFrame#developGroupFrame {"
            "  background: rgba(255,255,255,0.03);"
            "  border: 1px solid rgba(255,255,255,0.08);"
            "  border-radius: 4px;"
            "  margin: 0px;"
            "  padding: 4px;"
            "}"));
        QVBoxLayout *layout = new QVBoxLayout(frame);
        layout->setContentsMargins(4, 4, 4, 4);
        layout->setSpacing(4);
        QLabel *titleLabel = new QLabel(title, this);
        titleLabel->setStyleSheet(QStringLiteral(
            "color: #b0b0b0; font-weight: bold; font-size: 10px;"));
        layout->addWidget(titleLabel);
        previewControlsLayout->addRow(frame);
        return layout;
    };

    QVBoxLayout *toneLayout = addDevelopGroup(tr("Tone"));
    addDevelopControl(Develop::Exposure, "developExposure", tr("Exposure:"),
                      tr("Exposure compensation in stops. 0 is the exposure as shot."),
                      -500, 500, 0, 100.0, 2, toneLayout);
    addDevelopControl(Develop::Contrast, "developContrast", tr("Contrast:"),
                      tr("Steepens or flattens the midtones around middle gray."),
                      -100, 100, 0, 1.0, 0, toneLayout);
    addDevelopControl(Develop::Highlights, "developHighlights", tr("Highlights:"),
                      tr("Negative values bring the bright areas down, revealing the "
                         "detail held by the R photosites. Local contrast is kept."),
                      -100, 100, 0, 1.0, 0, toneLayout);
    addDevelopControl(Develop::Shadows, "developShadows", tr("Shadows:"),
                      tr("Positive values lift the dark areas while keeping local "
                         "contrast. The lift fades out below the sensor noise floor."),
                      -100, 100, 0, 1.0, 0, toneLayout);
    addDevelopControl(Develop::Whites, "developWhites", tr("Whites:"),
                      tr("Moves the point where tones roll off into white."),
                      -100, 100, 0, 1.0, 0, toneLayout);
    addDevelopControl(Develop::Blacks, "developBlacks", tr("Blacks:"),
                      tr("Sets the black point: negative deepens, positive lifts."),
                      -100, 100, 0, 1.0, 0, toneLayout);
    addDevelopControl(Develop::Gamma, "developGamma", tr("Gamma:"),
                      tr("Midtone gamma, as in Levels. 1.00 leaves the image unchanged; "
                         "higher values brighten the midtones."),
                      30, 300, 100, 100.0, 2, toneLayout);

    QVBoxLayout *colorLayout = addDevelopGroup(tr("Color"));
    m_temperatureSlider = new QSlider(Qt::Horizontal, this);
    m_temperatureSlider->setRange(0, kTemperatureSliderSteps);
    m_temperatureSlider->setToolTip(
        tr("Color temperature of the light in the scene. Higher values make the image "
           "warmer. The slider moves evenly in perceived color, not in Kelvin."));
    m_temperatureSpinBox = new QSpinBox(this);
    m_temperatureSpinBox->setRange(kMinimumKelvin, kMaximumKelvin);
    m_temperatureSpinBox->setSingleStep(50);
    m_temperatureSpinBox->setSuffix(QStringLiteral(" K"));
    m_temperatureSpinBox->setGroupSeparatorShown(false);
    m_temperatureSpinBox->setLocale(QLocale::C);
    m_temperatureAsShotButton = new QPushButton(tr("As Shot"), this);
    m_temperatureAsShotButton->setToolTip(tr("Returns to the white balance of the camera."));
    {
        QHBoxLayout *row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(4);
        QLabel *rowLabel = new QLabel(tr("Temperature:"), this);
        rowLabel->setMinimumWidth(90);
        row->addWidget(rowLabel);
        row->addWidget(m_temperatureSlider, 1);
        row->addWidget(m_temperatureSpinBox, 0);
        row->addWidget(m_temperatureAsShotButton, 0);
        colorLayout->addLayout(row);
    }
    connect(m_temperatureSlider, &QSlider::valueChanged, this, [this](int position) {
        setTemperatureKelvin(kelvinFromSlider(position));
        onDevelopChanged();
    });
    connect(m_temperatureSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int kelvin) {
        setTemperatureKelvin(kelvin);
        onDevelopChanged();
    });
    connect(m_temperatureAsShotButton, &QPushButton::clicked, this, [this]() {
        setTemperatureKelvin(0);
        onDevelopChanged();
    });
    setTemperatureKelvin(0);
    addDevelopControl(Develop::Tint, "developTint", tr("Tint:"),
                      tr("Relative to the white balance as shot. Positive is more magenta."),
                      -100, 100, 0, 1.0, 0, colorLayout);
    QHBoxLayout *pickerRow = new QHBoxLayout;
    pickerRow->setContentsMargins(0, 0, 0, 0);
    pickerRow->setSpacing(4);
    QLabel *pickerLabel = new QLabel(tr("WB picker:"), this);
    pickerLabel->setMinimumWidth(90);
    pickerRow->addWidget(pickerLabel);
    pickerRow->addWidget(m_whiteBalancePickerButton, 0);
    pickerRow->addStretch();
    colorLayout->addLayout(pickerRow);
    addDevelopControl(Develop::Vibrance, "developVibrance", tr("Vibrance:"),
                      tr("Raises muted colors most and protects skin tones."),
                      -100, 100, 0, 1.0, 0, colorLayout);
    addDevelopControl(Develop::Saturation, "developSaturation", tr("Saturation:"),
                      tr("Scales all colors evenly, at constant lightness."),
                      -100, 100, 0, 1.0, 0, colorLayout);

    QVBoxLayout *highlightLayout = addDevelopGroup(tr("Highlight reconstruction"));
    m_reconstructCheckBox = new QCheckBox(tr("Rebuild saturated highlights"), this);
    m_reconstructCheckBox->setChecked(true);
    m_reconstructCheckBox->setToolTip(
        tr("Where even the R photosites saturated, rebuilds the capped channels from "
           "the others. Turn it off to see what the sensor recorded."));
    highlightLayout->addWidget(m_reconstructCheckBox);
    connect(m_reconstructCheckBox, &QCheckBox::toggled, this, [this](bool) {
        onDevelopChanged();
    });
    addDevelopControl(Develop::ReconstructThreshold, "developReconstructThreshold",
                      tr("Threshold:"),
                      tr("How close to the measured sensor ceiling, in percent, the repair "
                         "begins. Lower it if pink edges remain; raise it if bright "
                         "colors near the ceiling lose saturation."),
                      80, 99, 92, 1.0, 0, highlightLayout);
    addDevelopControl(Develop::ReconstructNeutralize, "developReconstructNeutralize",
                      tr("Neutralize:"),
                      tr("How far areas with two or more saturated channels go toward "
                         "white. 0 keeps the recorded hue; 100 gives pure white."),
                      0, 100, 60, 1.0, 0, highlightLayout);

    QVBoxLayout *detailLayout = addDevelopGroup(tr("Detail"));
    addDevelopControl(Develop::SharpenAmount, "developSharpenAmount", tr("Sharpening:"),
                      tr("Luminance sharpening of the finished image. It eases off in "
                         "deep shadows and in highlights that come from the noisier "
                         "R photosites."),
                      0, 100, 0, 1.0, 0, detailLayout);

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

    // Make transition group prefer its natural size horizontally, but allow
    // it to shrink vertically (so it can fully collapse when toggled off
    // instead of keeping the platform's empty frame area).
    transitionGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    QHBoxLayout *outputFolderLayout = new QHBoxLayout;
    outputFolderLayout->addWidget(m_outputFolder);
    outputFolderLayout->addWidget(selectFolderButton);

    QFormLayout *optionsLayout = new QFormLayout;
    optionsLayout->addRow(tr("Output folder:"), outputFolderLayout);
    optionsLayout->addRow(transitionGroup);
    optionsLayout->addRow(previewScrollArea);

    QHBoxLayout *presetLayout = new QHBoxLayout;
    presetLayout->setContentsMargins(0, 0, 0, 0);
    presetLayout->setSpacing(4);
    presetLayout->addWidget(m_presetCombo, 1);
    presetLayout->addWidget(m_savePresetButton);
    presetLayout->addWidget(m_resetPresetButton);
    optionsLayout->addRow(tr("Preset:"), presetLayout);

    QVBoxLayout *rightLayout = new QVBoxLayout;
    rightLayout->addLayout(optionsLayout);
    QHBoxLayout *previewButtonsLayout = new QHBoxLayout;
    previewButtonsLayout->addWidget(m_showPreviewButton);
    previewButtonsLayout->addWidget(m_previewButton);
    previewButtonsLayout->addWidget(m_exportPreviewButton);
    QPushButton *showExposureToolsButton = new QPushButton(tr("Show Exposure Tools"), this);
    previewButtonsLayout->addWidget(showExposureToolsButton);
    rightLayout->addLayout(previewButtonsLayout);
    rightLayout->addWidget(warmupNoteLabel);

    // Create container widgets for the splitter
    QWidget *leftContainer = new QWidget(this);
    leftContainer->setLayout(leftLayout);
    QWidget *rightContainer = new QWidget(this);
    rightContainer->setLayout(rightLayout);
    rightContainer->setMinimumWidth(460);
    
    QSplitter *topSplitter = new QSplitter(Qt::Horizontal, this);
    topSplitter->addWidget(leftContainer);
    topSplitter->addWidget(rightContainer);
    
    // Set initial sizes with RAF list getting more space
    QList<int> splitterSizes;
    splitterSizes.append(460);
    splitterSizes.append(460);
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
    connect(showExposureToolsButton, &QPushButton::clicked, this, [this]() {
        onShowExposureToolsToggled(true);
        m_showExposureToolsCheckBox->setChecked(true);
    });
    connect(m_showExposureToolsCheckBox, &QCheckBox::toggled, this,
            [this](bool checked) {
                onShowExposureToolsToggled(checked);
                // Persist the toggle state immediately so the user's
                // choice is remembered across sessions, independent of
                // whether they exit the app via the window close button,
                // the menu, or the toggle checkbox itself.
                appSettings().setValue(
                    QStringLiteral("exposureTools/visible"), checked);
            });
    // Re-push the current preview when the user toggles "meter visible area
    // only" so the scopes immediately reflect the new sampling region.
    connect(m_exposureToolsWindow->meterVisibleAreaCheckBox(), &QCheckBox::toggled,
            this, [this](bool) { pushExposureToolsFromCache(); });
    connect(m_fileList, &QListWidget::currentRowChanged, this, [this](int row) {
        m_whiteBalancePickerButton->setChecked(false);
        if (row >= 0) {
            const QListWidgetItem *item = m_fileList->item(row);
            const int metadataRotation = item ? item->data(kItemPreviewRotationRole).toInt() : 0;
            const int rotationIndex = m_previewRotationCombo->findData(metadataRotation);
            const QSignalBlocker blocker(m_previewRotationCombo);
            m_previewRotationCombo->setCurrentIndex(rotationIndex >= 0 ? rotationIndex : 0);
            
            // Load per-image settings for the selected file
            loadImageSettingsForCurrentFile();
            
            queueAutoPreview();
            updateControls(false);
        }
    });
    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onPresetSelected);
    connect(m_savePresetButton, &QPushButton::clicked,
            this, &MainWindow::onSavePresetClicked);
        connect(m_resetPresetButton, &QPushButton::clicked,
            this, &MainWindow::onResetPresetClicked);
    connect(m_statusClearTimer, &QTimer::timeout, m_statusLabel, &QLabel::clear);
    connect(m_autoPreviewTimer, &QTimer::timeout, this, &MainWindow::onAutoPreviewTimer);
    connect(m_exposureScopeTimer,
            &QTimer::timeout,
            this,
            &MainWindow::pushExposureToolsFromCache);
    // The preview is expensive to render, so only queue an auto-preview
    // update once the user has finished dragging the slider. The curve
    // widget and the spinbox still update live via the valueChanged
    // connections below.
    connect(m_rTransitionStartSlider, &QSlider::sliderReleased, this, [this]() {
        queueAutoPreview();
    });
    connect(m_rTransitionDelaySlider, &QSlider::sliderReleased, this, [this]() {
        queueAutoPreview();
    });
    connect(m_rTransitionSmoothnessSlider, &QSlider::sliderReleased, this, [this]() {
        queueAutoPreview();
    });
    connect(m_rLumaNoiseReductionSlider, &QSlider::sliderReleased, this, [this]() {
        queueAutoPreview();
    });
    connect(m_chromaNoiseReductionSlider, &QSlider::sliderReleased, this, [this]() {
        queueAutoPreview();
    });
    // Keep the visual merge curve in sync with the sliders.
    connect(m_rTransitionStartSlider, &QSlider::valueChanged, this, [this](int) {
        m_transitionCurveWidget->setParameters(
            mergeStartFromSlider(m_rTransitionStartSlider->value()),
            m_rTransitionDelaySlider->value() / 100.0,
            m_rTransitionSmoothnessSlider->value() / 100.0);
    });
    connect(m_rTransitionDelaySlider, &QSlider::valueChanged, this, [this](int) {
        m_transitionCurveWidget->setParameters(
            mergeStartFromSlider(m_rTransitionStartSlider->value()),
            m_rTransitionDelaySlider->value() / 100.0,
            m_rTransitionSmoothnessSlider->value() / 100.0);
    });
    connect(m_rTransitionSmoothnessSlider, &QSlider::valueChanged, this, [this](int) {
        m_transitionCurveWidget->setParameters(
            mergeStartFromSlider(m_rTransitionStartSlider->value()),
            m_rTransitionDelaySlider->value() / 100.0,
            m_rTransitionSmoothnessSlider->value() / 100.0);
    });
    // Prime the curve with the initial slider values.
    m_transitionCurveWidget->setParameters(
        mergeStartFromSlider(m_rTransitionStartSlider->value()),
        m_rTransitionDelaySlider->value() / 100.0,
        m_rTransitionSmoothnessSlider->value() / 100.0);
    connect(m_previewZoomSlider, &QSlider::valueChanged, this, &MainWindow::onPreviewZoomChanged);
    // Keep the exposure tools in sync with the visible part of the preview
    // window whenever the user zooms or scrolls.
    const auto refreshExposureToolsIfVisible = [this]() {
        if (m_exposureToolsWindow->metersVisibleAreaOnly()) {
            pushExposureToolsFromCache();
        }
    };
    connect(m_previewZoomSlider, &QSlider::valueChanged, this,
            [refreshExposureToolsIfVisible](int) { refreshExposureToolsIfVisible(); });
    connect(m_previewScrollArea->horizontalScrollBar(), &QScrollBar::valueChanged,
            this, [refreshExposureToolsIfVisible](int) { refreshExposureToolsIfVisible(); });
    connect(m_previewScrollArea->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [refreshExposureToolsIfVisible](int) { refreshExposureToolsIfVisible(); });
    connect(m_whiteBalancePickerButton,
            &QPushButton::toggled,
            this,
            &MainWindow::onWhiteBalancePickerToggled);
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
    connect(m_previewMethodGroup, &QButtonGroup::buttonClicked,
            this, &MainWindow::onPreviewMethodChanged);

    // Bidirectional connections between spinboxes and sliders
    // When slider changes, update spinbox
    connect(m_rTransitionStartSlider, &QSlider::valueChanged, this, [this](int value) {
        m_rTransitionStartSpinBox->setValue(value);
    });
    connect(m_rTransitionDelaySlider, &QSlider::valueChanged, this, [this](int value) {
        m_rTransitionDelaySpinBox->setValue(value);
    });
    connect(m_rTransitionSmoothnessSlider, &QSlider::valueChanged, this, [this](int value) {
        m_rTransitionSmoothnessSpinBox->setValue(value);
    });
    connect(m_rLumaNoiseReductionSlider, &QSlider::valueChanged, this, [this](int value) {
        m_rLumaNoiseReductionSpinBox->setValue(value);
    });
    connect(m_chromaNoiseReductionSlider, &QSlider::valueChanged, this, [this](int value) {
        m_chromaNoiseReductionSpinBox->setValue(value);
    });
    connect(m_previewZoomSlider, &QSlider::valueChanged, this, [this](int value) {
        m_previewZoomSpinBox->setValue(value);
    });

    // When spinbox changes, update slider (no auto preview for preview adjustments)
    connect(m_rTransitionStartSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (m_rTransitionStartSlider->value() != value) {
            m_rTransitionStartSlider->setValue(value);
            queueAutoPreview();
        }
    });
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
    connect(m_rLumaNoiseReductionSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (m_rLumaNoiseReductionSlider->value() != value) {
            m_rLumaNoiseReductionSlider->setValue(value);
            queueAutoPreview();
        }
    });
    connect(m_chromaNoiseReductionSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (m_chromaNoiseReductionSlider->value() != value) {
            m_chromaNoiseReductionSlider->setValue(value);
            queueAutoPreview();
        }
    });
    connect(m_previewZoomSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (m_previewZoomSlider->value() != value) {
            m_previewZoomSlider->setValue(value);
        }
    });

    refreshPresetCombo();
    // Prefer Default when it is present; otherwise start with the first
    // available preset. With no presets at all, retain the factory defaults.
    if (m_presetCombo->count() > 0) {
        loadPreset(m_presetCombo->currentText());
    } else {
        loadPreset(PresetManager::kDefaultPresetName);
    }
    updateControls(false);

    const QByteArray previewGeometry =
        appSettings().value(QStringLiteral("windows/previewGeometry")).toByteArray();
    if (!previewGeometry.isEmpty()) {
        m_previewWindow->restoreGeometry(previewGeometry);
    }
    m_previewWindow->show();

    // Exposure tools window: restore geometry and the "meter visible area
    // only" preference. The toggle checkbox drives whether the window is
    // shown, so we honor that state and only show the window when the user
    // had it open last session.
    const QByteArray exposureGeometry =
        appSettings().value(QStringLiteral("windows/exposureToolsGeometry")).toByteArray();
    if (!exposureGeometry.isEmpty()) {
        m_exposureToolsWindow->restoreGeometry(exposureGeometry);
    }
    const bool meterVisibleOnly = appSettings().value(
        QStringLiteral("exposureTools/meterVisibleAreaOnly"), false).toBool();
    {
        const QSignalBlocker blocker(
            m_exposureToolsWindow->meterVisibleAreaCheckBox());
        m_exposureToolsWindow->meterVisibleAreaCheckBox()->setChecked(meterVisibleOnly);
    }
    const bool exposureToolsVisible = appSettings().value(
        QStringLiteral("exposureTools/visible"), false).toBool();
    {
        const QSignalBlocker blocker(m_showExposureToolsCheckBox);
        m_showExposureToolsCheckBox->setChecked(exposureToolsVisible);
    }
    if (exposureToolsVisible) {
        // The exposure tools will be populated automatically the first time
        // a preview is rendered (via updatePreviewDisplay), so we only need
        // to show the window here.
        m_exposureToolsWindow->show();
    }
    m_exposureToolsWindow->installEventFilter(this);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_previewWindow && event->type() == QEvent::Close) {
        appSettings().setValue(QStringLiteral("windows/previewGeometry"),
                               m_previewWindow->saveGeometry());
    }

    if (watched == m_exposureToolsWindow) {
        if (event->type() == QEvent::Close) {
            appSettings().setValue(QStringLiteral("windows/exposureToolsGeometry"),
                                   m_exposureToolsWindow->saveGeometry());
            appSettings().setValue(
                QStringLiteral("exposureTools/meterVisibleAreaOnly"),
                m_exposureToolsWindow->metersVisibleAreaOnly());
            // Closing the window via the X button is treated as a hide,
            // not a "turn this off permanently" — the toggle checkbox
            // and the `exposureTools/visible` setting keep whatever
            // state the user last chose. Reflect that in the UI: the
            // window is hidden but the checkbox is left alone.
        }
    }

    if ((watched == m_previewLabel || watched == m_previewScrollArea->viewport()) && m_scene) {
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
    appSettings().setValue(QStringLiteral("windows/exposureToolsGeometry"),
                           m_exposureToolsWindow->saveGeometry());
    appSettings().setValue(QStringLiteral("exposureTools/visible"),
                           m_showExposureToolsCheckBox->isChecked());
    appSettings().setValue(
        QStringLiteral("exposureTools/meterVisibleAreaOnly"),
        m_exposureToolsWindow->metersVisibleAreaOnly());
    m_previewWindow->close();
    m_exposureToolsWindow->close();
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
        m_scene.reset();
        m_scopeScene.reset();
        m_adjustedDisplayImage = QImage();
        m_exposureScopeTimer->stop();
        m_previewLabel->clearScene();
        m_previewLabel->clear();
        m_previewLabel->setText(tr("Preview not generated."));
        m_previewLabel->unsetCursor();
        m_previewWindow->setWindowTitle(tr("Preview"));
        m_exposureToolsWindow->setSourceImage(QImage(), QRect());
    }
}

void MainWindow::onSelectOutputFolder()
{
    QString folder = QFileDialog::getExistingDirectory(this,
                                                       tr("Select Output Folder"),
                                                       m_outputFolder->text());
    if (!folder.isEmpty()) {
        m_outputFolder->setText(folder);
        m_imageSettingsManager->setOutputFolder(folder);
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

        QString error;
        superccd::SceneImagePtr scene;
        if (!m_processor.renderScene(inputPath, currentSettings(), scene, error)) {
            showStatus(tr("Preview failed: %1").arg(error));
            m_busy = false;
            updateControls(false);
            return;
        }
        setCurrentScene(std::move(scene), inputPath);
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

    QString error;
    superccd::SceneImagePtr scene;
    if (!m_processor.renderScene(inputPath, currentSettings(), scene, error)) {
        m_whiteBalancePickerButton->setChecked(false);
        m_previewLabel->clearScene();
        m_previewLabel->setText(tr("Preview failed."));
        m_previewWindow->setWindowTitle(tr("Preview - Failed"));
        showStatus(tr("Preview failed: %1").arg(error));
        m_busy = false;
        updateControls(false);
        return;
    }

    const bool shouldResetViewport = !m_scene
        || m_scene->width != scene->width
        || m_scene->height != scene->height
        || m_lastPreviewedInputPath != inputPath;
    setCurrentScene(std::move(scene), inputPath);
    updatePreviewDisplay(!shouldResetViewport);
    showStatus(tr("Preview updated."));
    m_busy = false;
    updateControls(false);
}

void MainWindow::setCurrentScene(superccd::SceneImagePtr scene, const QString &inputPath)
{
    m_scene = std::move(scene);
    m_previewLabel->setScene(m_scene);
    setTemperatureKelvin(m_temperatureKelvin);
    m_lastPreviewedInputPath = inputPath;
    m_previewWindow->setWindowTitle(
        tr("Preview - %1").arg(QFileInfo(inputPath).fileName()));
    m_adjustedDisplayImage = QImage();
    m_scopeScene.reset();
}

void MainWindow::onExportPreview()
{
    QListWidgetItem *current = m_fileList->currentItem();
    if (!current || !m_scene) {
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
    const bool amazeSelected = currentPreviewMethod() == PreviewMethod::AmazeDebayer;
    if (amazeSelected) {
        sizeComboBox->addItem(tr("6 MP"), static_cast<int>(PreviewExportSize::SixMp));
        sizeComboBox->setCurrentIndex(0);
        sizeComboBox->setEnabled(false);
    } else {
        sizeComboBox->addItem(tr("12 MP"), static_cast<int>(PreviewExportSize::FullSize));
        if (std::min(m_scene->width, m_scene->height) > kPreviewExportSixMpShortSide) {
            sizeComboBox->addItem(tr("6 MP"), static_cast<int>(PreviewExportSize::SixMp));
        }
    }
    const int exportSizeSetting = settingsStore.contains(QStringLiteral("previewExport/size"))
        ? settingsStore.value(QStringLiteral("previewExport/size"), static_cast<int>(PreviewExportSize::FullSize)).toInt()
        : (settingsStore.value(QStringLiteral("previewExport/export6Mp"), false).toBool()
               ? static_cast<int>(PreviewExportSize::SixMp)
               : static_cast<int>(PreviewExportSize::FullSize));
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

    // Sharpening is the last step at the final size, so an image that gets resized
    // is sharpened afterwards. One already at its final size, such as AMaZE, is
    // sharpened by the renderer, with the noise protection the preview shows.
    superccd::EditParams exportParams = currentEditParams();
    const double sharpening = exportParams.sharpenAmount;
    const bool resizing = m_scene && exportSize != PreviewExportSize::FullSize
        && std::min(m_scene->width, m_scene->height) > kPreviewExportSixMpShortSide;
    if (resizing) {
        exportParams.sharpenAmount = 0.0;
    }
    QImage exportImage = m_scene
        ? PreviewImageProcessing::renderFull16(*m_scene, exportParams)
        : QImage();
    if (exportImage.isNull()) {
        showStatus(tr("Preview export failed."));
        return;
    }

    exportImage = resizeForPreviewExport(exportImage, exportSize);
    if (exportImage.isNull()) {
        showStatus(tr("Preview export resize failed."));
        return;
    }
    if (resizing) {
        PreviewImageProcessing::sharpen16(exportImage, sharpening);
    }

    SuperCCDMetadata exportMetadata;
    const SuperCCDMetadata *exportMetadataPtr = nullptr;
    if (includeExif && exportFormat == PreviewExportFormat::Tiff16) {
        QString metadataReadError;
        if (!SuperCCDProcessor::readMetadata(inputPath, exportMetadata, &metadataReadError)) {
            showStatus(tr("Could not read EXIF metadata from source RAF: %1").arg(metadataReadError));
            return;
        }
        exportMetadataPtr = &exportMetadata;
    }

    const int quality = qualitySpinBox->value();
    if (exportFormat == PreviewExportFormat::Tiff16) {
        QString tiffError;
        if (!DngWriter::writeRgbTiff16(outputPath, exportImage, exportMetadataPtr, tiffError)) {
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
    m_rTransitionStartSlider->setEnabled(!busy);
    m_rTransitionStartSpinBox->setEnabled(!busy);
    m_rTransitionDelaySlider->setEnabled(!busy);
    m_rTransitionDelaySpinBox->setEnabled(!busy);
    m_rTransitionSmoothnessSlider->setEnabled(!busy);
    m_rTransitionSmoothnessSpinBox->setEnabled(!busy);
    m_previewZoomSlider->setEnabled(!busy);
    m_previewZoomSpinBox->setEnabled(!busy);
    m_whiteBalancePickerButton->setEnabled(!busy && hasCurrentPreview());
    for (const DevelopControl &control : m_developControls) {
        control.slider->setEnabled(!busy);
        control.spinBox->setEnabled(!busy);
    }
    m_reconstructCheckBox->setEnabled(!busy);
    m_temperatureSlider->setEnabled(!busy);
    m_temperatureSpinBox->setEnabled(!busy);
    m_temperatureAsShotButton->setEnabled(!busy);
    m_previewRotationCombo->setEnabled(!busy);
    m_correctPreviewOutliersCheckBox->setEnabled(!busy);
    m_showPreviewButton->setEnabled(true);
    m_previewButton->setEnabled(!busy);
    const bool hasCurrentPreview = m_fileList->currentItem() != nullptr
        && m_scene
        && listItemPath(m_fileList->currentItem()) == m_lastPreviewedInputPath;
    m_exportPreviewButton->setEnabled(!busy && hasCurrentPreview);
    m_autoPreviewCheckBox->setEnabled(!busy);
    m_convertCurrentButton->setEnabled(!busy && m_fileList->currentItem() != nullptr);
    m_convertAllButton->setEnabled(!busy);
    m_presetCombo->setEnabled(!busy);
    m_savePresetButton->setEnabled(!busy);
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

void MainWindow::onShowExposureToolsToggled(bool enabled)
{
    if (enabled) {
        if (m_exposureToolsWindow->isMinimized()) {
            m_exposureToolsWindow->setWindowState(
                m_exposureToolsWindow->windowState() & ~Qt::WindowMinimized);
        }
        // Push the current preview (if any) so the scopes aren't blank when
        // first opened.
        pushExposureToolsFromCache();
        m_exposureToolsWindow->show();
        m_exposureToolsWindow->raise();
        m_exposureToolsWindow->activateWindow();
    } else {
        m_exposureToolsWindow->hide();
    }
}

QRect MainWindow::currentPreviewVisibleRect() const
{
    if (!m_scene || m_previewLabel->width() <= 0
        || m_previewLabel->height() <= 0) {
        return QRect();
    }
    const double zoom = static_cast<double>(m_previewZoomSlider->value()) / 100.0;
    if (zoom <= 0.0) {
        return QRect(0, 0, m_scene->width, m_scene->height);
    }
    // The scroll area's horizontal/vertical scroll values tell us which
    // portion of the canvas is on screen. Convert that to image coords.
    const QRect sceneRect(0, 0, m_scene->width, m_scene->height);
    const QPoint topLeftCanvas(
        m_previewScrollArea->horizontalScrollBar()->value(),
        m_previewScrollArea->verticalScrollBar()->value());
    const QSize viewportSize = m_previewScrollArea->viewport()->size();
    const int left = static_cast<int>(std::floor(topLeftCanvas.x() / zoom));
    const int top = static_cast<int>(std::floor(topLeftCanvas.y() / zoom));
    const int right = static_cast<int>(
        std::ceil((topLeftCanvas.x() + viewportSize.width()) / zoom));
    const int bottom = static_cast<int>(
        std::ceil((topLeftCanvas.y() + viewportSize.height()) / zoom));
    return QRect(QPoint(left, top), QPoint(right - 1, bottom - 1))
        .intersected(sceneRect);
}

void MainWindow::pushExposureToolsFromCache()
{
    if (!m_scene) {
        return;
    }

    // The scopes read tones, which sharpening does not move, so they skip it.
    superccd::EditParams params = currentEditParams();
    params.sharpenAmount = 0.0;
    const bool stale = m_adjustedDisplayImage.isNull()
        || m_scopeScene != m_scene
        || m_scopeParams != params;
    if (stale) {
        m_adjustedDisplayImage = PreviewImageProcessing::renderFull8(*m_scene, params);
        m_scopeScene = m_scene;
        m_scopeParams = params;
    }
    if (m_adjustedDisplayImage.isNull()) {
        return;
    }
    m_exposureToolsWindow->setSourceImage(
        m_adjustedDisplayImage, currentPreviewVisibleRect());
}

void MainWindow::onPreviewZoomChanged(int value)
{
    Q_UNUSED(value);
    updatePreviewDisplay();
}

void MainWindow::onDevelopChanged()
{
    markPresetModified();
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
            !m_scene ? Qt::ArrowCursor : Qt::OpenHandCursor);
    }
}

void MainWindow::updatePreviewDisplay(bool preserveViewport)
{
    if (!m_scene) {
        return;
    }

    const QSize oldContentSize = m_previewLabel->size();
    const QSize viewportSize = m_previewScrollArea->viewport()->size();
    const double oldCenterX = preserveViewport && oldContentSize.width() > 0
        ? (static_cast<double>(m_previewScrollArea->horizontalScrollBar()->value()) + viewportSize.width() * 0.5)
            / static_cast<double>(oldContentSize.width())
        : 0.5;
    const double oldCenterY = preserveViewport && oldContentSize.height() > 0
        ? (static_cast<double>(m_previewScrollArea->verticalScrollBar()->value()) + viewportSize.height() * 0.5)
            / static_cast<double>(oldContentSize.height())
        : 0.5;

    const double zoom = static_cast<double>(m_previewZoomSlider->value()) / 100.0;

    const superccd::EditParams params = currentEditParams();
    m_previewLabel->setDisplayState(zoom, params);
    m_previewLabel->setCursor(
        m_whiteBalancePickerButton->isChecked() ? Qt::CrossCursor : Qt::OpenHandCursor);

    const int newHValue = static_cast<int>(
        oldCenterX * m_previewLabel->width() - viewportSize.width() * 0.5 + 0.5);
    const int newVValue = static_cast<int>(
        oldCenterY * m_previewLabel->height() - viewportSize.height() * 0.5 + 0.5);
    m_previewScrollArea->horizontalScrollBar()->setValue(std::max(0, newHValue));
    m_previewScrollArea->verticalScrollBar()->setValue(std::max(0, newVValue));

    // The scopes rebuild a full image, so that is debounced: a continuous drag
    // coalesces into one rebuild per pause, while the canvas still updates live
    // above. Hidden scopes catch up when they are next shown.
    if (m_exposureToolsWindow->isVisible()) {
        m_exposureScopeTimer->start();
    } else {
        m_exposureScopeTimer->stop();
    }
}

void MainWindow::showStatus(const QString &message)
{
    m_statusLabel->setText(message);
    m_statusClearTimer->start(5000);
}

void MainWindow::applyWhiteBalancePickerSample()
{
    const QRect sampleRect = m_previewLabel->whiteBalancePickerSourceRect();
    double kelvin = 0.0;
    double tint = 0.0;
    if (!m_scene || !superccd::estimateNeutralWhiteBalance(*m_scene,
                                                          sampleRect.x(),
                                                          sampleRect.y(),
                                                          sampleRect.width(),
                                                          sampleRect.height(),
                                                          kelvin,
                                                          tint)) {
        showStatus(tr("The selected area could not be used for white balance."));
        return;
    }

    setTemperatureKelvin(std::clamp(static_cast<int>(std::lround(kelvin)),
                                    kMinimumKelvin, kMaximumKelvin));
    setDevelopValue(Develop::Tint, static_cast<int>(std::lround(tint)));
    markPresetModified();
    updatePreviewDisplay();

    showStatus(tr("White balance sampled: %1 K, tint %2.")
                   .arg(m_temperatureKelvin)
                   .arg(developControl(Develop::Tint).slider->value()));
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
        && m_scene
        && listItemPath(m_fileList->currentItem()) == m_lastPreviewedInputPath;
}

void MainWindow::applyParameterSettings(const ConversionSettings &settings)
{
    const bool oldStartSignals = m_rTransitionStartSlider->blockSignals(true);
    const bool oldDelaySignals = m_rTransitionDelaySlider->blockSignals(true);
    const bool oldSmoothnessSignals = m_rTransitionSmoothnessSlider->blockSignals(true);
    const bool oldLumaNRSignals = m_rLumaNoiseReductionSlider->blockSignals(true);
    const bool oldChromaNRSignals = m_chromaNoiseReductionSlider->blockSignals(true);
    const bool oldChromaNRSpinSignals = m_chromaNoiseReductionSpinBox->blockSignals(true);
    const bool oldStartSpinSignals = m_rTransitionStartSpinBox->blockSignals(true);
    const bool oldDelaySpinSignals = m_rTransitionDelaySpinBox->blockSignals(true);
    const bool oldSmoothnessSpinSignals =
        m_rTransitionSmoothnessSpinBox->blockSignals(true);
    const bool oldLumaNRSpinSignals =
        m_rLumaNoiseReductionSpinBox->blockSignals(true);
    const bool oldCorrectionSignals =
        m_correctPreviewOutliersCheckBox->blockSignals(true);
    // Map the actual start (0..1) back to the slider position so the
    // non-linear control is reflected when loading saved settings.
    const int startValue = sliderFromMergeStart(settings.rTransitionStart);
    const int delayValue =
        std::clamp(static_cast<int>(settings.rTransitionDelay * 100.0 + 0.5),
                   1,  // minimum 1: never collapse the merge window
                   100);
    const int smoothnessValue =
        std::clamp(static_cast<int>(settings.rTransitionSmoothness * 100.0 + 0.5),
                   0,
                   100);
    const int lumaNRValue =
        std::clamp(static_cast<int>(settings.rLumaNoiseReduction * 100.0 + 0.5),
                   0,
                   100);
    m_rTransitionStartSlider->setValue(startValue);
    m_rTransitionStartSpinBox->setValue(startValue);
    m_rTransitionDelaySlider->setValue(delayValue);
    m_rTransitionDelaySpinBox->setValue(delayValue);
    m_rTransitionSmoothnessSlider->setValue(smoothnessValue);
    m_rTransitionSmoothnessSpinBox->setValue(smoothnessValue);
    m_rLumaNoiseReductionSlider->setValue(lumaNRValue);
    m_rLumaNoiseReductionSpinBox->setValue(lumaNRValue);
    const int chromaNRValue =
        std::clamp(static_cast<int>(settings.chromaNoiseReduction * 100.0 + 0.5), 0, 100);
    m_chromaNoiseReductionSlider->setValue(chromaNRValue);
    m_chromaNoiseReductionSpinBox->setValue(chromaNRValue);
    m_correctPreviewOutliersCheckBox->setChecked(
        settings.correctPreviewOutliers);
    m_rTransitionStartSlider->blockSignals(oldStartSignals);
    m_rTransitionDelaySlider->blockSignals(oldDelaySignals);
    m_rTransitionSmoothnessSlider->blockSignals(oldSmoothnessSignals);
    m_rLumaNoiseReductionSlider->blockSignals(oldLumaNRSignals);
    m_chromaNoiseReductionSlider->blockSignals(oldChromaNRSignals);
    m_chromaNoiseReductionSpinBox->blockSignals(oldChromaNRSpinSignals);
    m_rTransitionStartSpinBox->blockSignals(oldStartSpinSignals);
    m_rTransitionDelaySpinBox->blockSignals(oldDelaySpinSignals);
    m_rTransitionSmoothnessSpinBox->blockSignals(oldSmoothnessSpinSignals);
    m_rLumaNoiseReductionSpinBox->blockSignals(oldLumaNRSpinSignals);
    m_correctPreviewOutliersCheckBox->blockSignals(oldCorrectionSignals);
}

void MainWindow::refreshPresetCombo()
{
    const QSignalBlocker blocker(m_presetCombo);
    m_presetCombo->clear();
    const QStringList presets = m_presetManager->availablePresets();
    for (const QString &name : presets) {
        m_presetCombo->addItem(name);
    }
}

void MainWindow::markPresetModified()
{
    if (m_presetComboGuard) {
        return;
    }
    
    const QJsonObject current = collectCurrentStateAsJson();
    const bool modified = (current != m_loadedPresetData);
    
    // Always update the asterisk based on whether current differs from loaded preset
    m_presetModified = modified;
    const int index = m_presetCombo->currentIndex();
    if (index >= 0) {
        QString text = m_presetCombo->itemText(index);
        if (modified && !text.endsWith(QStringLiteral(" *"))) {
            m_presetCombo->setItemText(index, text + QStringLiteral(" *"));
        } else if (!modified && text.endsWith(QStringLiteral(" *"))) {
            text.chop(2);
            m_presetCombo->setItemText(index, text);
        }
    }
    
    // Auto-save per-image settings whenever any value changes
    saveImageSettingsForCurrentFile();
}

void MainWindow::loadPreset(const QString &name)
{
    // Clear asterisks from all presets in the dropdown
    for (int i = 0; i < m_presetCombo->count(); ++i) {
        QString text = m_presetCombo->itemText(i);
        if (text.endsWith(QStringLiteral(" *"))) {
            text.chop(2);
            m_presetCombo->setItemText(i, text);
        }
    }
    
    const QJsonObject jsonData = m_presetManager->loadPreset(name);
    if (jsonData.isEmpty()) {
        // Fall back to factory defaults if preset is missing or invalid
        ConversionSettings defaults;
        defaults.exportMode = ExportMode::RawCfa6MP;
        defaults.rTransitionStart = kDefaultMergeStart;
        defaults.rTransitionDelay = kDefaultMergeWidth;
        defaults.rTransitionSmoothness = 0.5;
        defaults.rLumaNoiseReduction = 0.0;
        defaults.correctPreviewOutliers = false;
        applyParameterSettings(defaults);
        m_previewZoomSlider->setValue(kDefaultPreviewZoomSliderValue);
        for (const DevelopControl &control : m_developControls) {
            setDevelopValue(control.id, control.defaultValue);
        }
        setTemperatureKelvin(0);
        {
            const QSignalBlocker reconstructBlocker(m_reconstructCheckBox);
            m_reconstructCheckBox->setChecked(true);
        }
        m_previewRotationCombo->setCurrentIndex(0);
        m_autoPreviewCheckBox->setChecked(kDefaultAutoPreview);
        m_previewMethodAmazeButton->setChecked(true);
        m_loadedPresetData = collectCurrentStateAsJson();
    } else {
        applyJsonPreset(jsonData);
        m_loadedPresetData = jsonData;
    }
    m_presetModified = false;
    
    // Manually trigger preview update since slider signals were blocked
    updatePreviewDisplay();
}

void MainWindow::applyJsonPreset(const QJsonObject &presetData)
{
    // Conversion settings
    ConversionSettings settings;
    settings.exportMode = ExportMode::RawCfa6MP;
    settings.rTransitionStart = presetData.value(QStringLiteral("rTransitionStart")).toDouble(kDefaultMergeStart);
    settings.rTransitionDelay = presetData.value(QStringLiteral("rTransitionDelay")).toDouble(kDefaultMergeWidth);
    settings.rTransitionSmoothness = presetData.value(QStringLiteral("rTransitionSmoothness")).toDouble(0.5);
    settings.rLumaNoiseReduction = presetData.value(QStringLiteral("rLumaNoiseReduction")).toDouble(0.0);
    settings.chromaNoiseReduction = presetData.value(QStringLiteral("chromaNoiseReduction")).toDouble(0.0);
    settings.correctPreviewOutliers = presetData.value(QStringLiteral("correctPreviewOutliers")).toBool(false);
    applyParameterSettings(settings);

    // Develop settings saved by versions before 2.0 used different keys and
    // units, so they do not apply here and such files open with neutral values.
    for (const DevelopControl &control : m_developControls) {
        setDevelopValue(control.id,
                        presetData.value(QLatin1String(control.key)).toInt(control.defaultValue));
    }
    setTemperatureKelvin(presetData.value(QStringLiteral("developTemperatureKelvin")).toInt(0));
    const QSignalBlocker reconstructBlocker(m_reconstructCheckBox);
    m_reconstructCheckBox->setChecked(
        presetData.value(QStringLiteral("developReconstruct")).toBool(true));
}

QJsonObject MainWindow::collectCurrentStateAsJson() const
{
    QJsonObject presetData;
    presetData[QStringLiteral("version")] = QString::fromLatin1(APP_VERSION_STRING);
    const ConversionSettings settings = currentSettings();
    presetData[QStringLiteral("rTransitionStart")] = settings.rTransitionStart;
    presetData[QStringLiteral("rTransitionDelay")] = settings.rTransitionDelay;
    presetData[QStringLiteral("rTransitionSmoothness")] = settings.rTransitionSmoothness;
    presetData[QStringLiteral("rLumaNoiseReduction")] = settings.rLumaNoiseReduction;
    presetData[QStringLiteral("chromaNoiseReduction")] = settings.chromaNoiseReduction;
    presetData[QStringLiteral("correctPreviewOutliers")] = settings.correctPreviewOutliers;
    for (const DevelopControl &control : m_developControls) {
        presetData[QLatin1String(control.key)] = control.slider->value();
    }
    presetData[QStringLiteral("developReconstruct")] = m_reconstructCheckBox->isChecked();
    presetData[QStringLiteral("developTemperatureKelvin")] = m_temperatureKelvin;
    return presetData;
}

void MainWindow::loadImageSettingsForCurrentFile()
{
    if (!m_imageSettingsManager || !m_fileList->currentItem()) {
        return;
    }
    
    const QString inputPath = listItemPath(m_fileList->currentItem());
    if (inputPath.isEmpty()) {
        return;
    }
    
    const QFileInfo fileInfo(inputPath);
    const QString filename = fileInfo.fileName();
    const QString inputFolder = fileInfo.absolutePath();
    
    // Use the input file's folder for settings storage
    m_imageSettingsManager->setOutputFolder(inputFolder);
    
    // Check if per-image settings exist
    if (m_imageSettingsManager->hasImageSettings(filename)) {
        // Load and apply the per-image settings
        QJsonObject imageSettings = m_imageSettingsManager->loadImageSettings(filename);
        if (!imageSettings.isEmpty()) {
            m_presetComboGuard = true;
            applyJsonPreset(imageSettings);
            m_presetComboGuard = false;
            // Let markPresetModified() determine if asterisk should be shown
            // by comparing current state against the selected preset
            markPresetModified();
            return;
        }
    }
    
    // No per-image settings found: start from the immutable Default preset,
    // rather than carrying over the previous image's current controls.
    QJsonObject initialPreset =
        m_presetManager->loadPreset(PresetManager::kDefaultPresetName);
    if (initialPreset.isEmpty()) {
        const QStringList presets = m_presetManager->availablePresets();
        if (presets.isEmpty()) {
            return;
        }
        initialPreset = m_presetManager->loadPreset(presets.first());
        if (initialPreset.isEmpty()) {
            return;
        }
    }

    const int defaultPresetIndex =
        m_presetCombo->findText(PresetManager::kDefaultPresetName);
    if (defaultPresetIndex >= 0) {
        const QSignalBlocker blocker(m_presetCombo);
        m_presetCombo->setCurrentIndex(defaultPresetIndex);
    }
    
    m_presetComboGuard = true;
    applyJsonPreset(initialPreset);
    m_presetComboGuard = false;
    
    // Capture the current state after applying the preset (not the raw JSON from file)
    // This accounts for slider conversions and floating-point precision differences
    m_loadedPresetData = collectCurrentStateAsJson();
    
    // Update the asterisk display (should remove it since we just loaded a preset)
    markPresetModified();
    
    // Save the selected preset as the initial per-image settings
    m_imageSettingsManager->saveImageSettings(filename, m_loadedPresetData);
}

void MainWindow::saveImageSettingsForCurrentFile()
{
    if (!m_imageSettingsManager || !m_fileList->currentItem()) {
        return;
    }
    
    const QString inputPath = listItemPath(m_fileList->currentItem());
    if (inputPath.isEmpty()) {
        return;
    }
    
    const QFileInfo fileInfo(inputPath);
    const QString filename = fileInfo.fileName();
    const QString inputFolder = fileInfo.absolutePath();
    
    // Use the input file's folder for settings storage
    m_imageSettingsManager->setOutputFolder(inputFolder);
    
    const QJsonObject currentSettings = collectCurrentStateAsJson();
    
    m_imageSettingsManager->saveImageSettings(filename, currentSettings);
}

void MainWindow::onPresetSelected(int index)
{
    if (index < 0 || m_presetComboGuard) {
        return;
    }
    QString presetName = m_presetCombo->itemText(index);
    // Remove the asterisk if present
    if (presetName.endsWith(QStringLiteral(" *"))) {
        presetName.chop(2);
        m_presetCombo->setItemText(index, presetName);
    }
    loadPreset(presetName);

    // Persist the selected preset values as this image's saved settings.
    // Signals were blocked during loadPreset(), so the normal auto-save
    // path through markPresetModified() was not triggered.
    saveImageSettingsForCurrentFile();

    showStatus(tr("Preset '%1' applied.").arg(presetName));
}

void MainWindow::onResetPresetClicked()
{
    const int index = m_presetCombo->currentIndex();
    if (index >= 0) {
        onPresetSelected(index);
    }
}

void MainWindow::onSavePresetClicked()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Save Preset"),
                                               tr("Preset name:"),
                                               QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) {
        return;
    }

    const QString trimmedName = name.trimmed();

    if (PresetManager::isDefaultPreset(trimmedName)) {
        QMessageBox::warning(this, tr("Cannot Overwrite"),
                             tr("The 'Default' preset cannot be overwritten."));
        return;
    }

    if (m_presetManager->presetExists(trimmedName)) {
        const QMessageBox::StandardButton reply = QMessageBox::question(
            this, tr("Overwrite Preset"),
            tr("A preset named '%1' already exists. Do you want to overwrite it?").arg(trimmedName),
            QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            return;
        }
    }

    const QJsonObject jsonData = collectCurrentStateAsJson();
    if (!m_presetManager->savePreset(trimmedName, jsonData)) {
        QMessageBox::critical(this, tr("Save Failed"),
                              tr("Failed to save preset '%1'.").arg(trimmedName));
        return;
    }

    refreshPresetCombo();
    const int newIndex = m_presetCombo->findText(trimmedName);
    if (newIndex >= 0) {
        const QSignalBlocker blocker(m_presetCombo);
        m_presetCombo->setCurrentIndex(newIndex);
    }
    
    // Update the loaded preset data and clear modified flag
    m_loadedPresetData = jsonData;
    m_presetModified = false;
    
    showStatus(tr("Preset '%1' saved.").arg(trimmedName));
}

void MainWindow::queueAutoPreview()
{
    // The merge controls all come through here, and their changes have to reach the
    // per-image settings like the develop sliders do.
    markPresetModified();
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

MainWindow::DevelopControl &MainWindow::developControl(Develop id)
{
    return m_developControls[static_cast<size_t>(id)];
}

const MainWindow::DevelopControl &MainWindow::developControl(Develop id) const
{
    return m_developControls[static_cast<size_t>(id)];
}

void MainWindow::addDevelopControl(Develop id,
                                   const char *key,
                                   const QString &label,
                                   const QString &toolTip,
                                   int minimum,
                                   int maximum,
                                   int defaultValue,
                                   double scale,
                                   int decimals,
                                   QVBoxLayout *layout)
{
    DevelopControl &control = developControl(id);
    control.id = id;
    control.key = key;
    control.defaultValue = defaultValue;
    control.scale = scale;
    control.slider = new QSlider(Qt::Horizontal, this);
    control.spinBox = new QDoubleSpinBox(this);

    QSlider *slider = control.slider;
    QDoubleSpinBox *spinBox = control.spinBox;
    slider->setRange(minimum, maximum);
    slider->setValue(defaultValue);
    slider->setToolTip(toolTip);
    spinBox->setDecimals(decimals);
    spinBox->setRange(minimum / scale, maximum / scale);
    spinBox->setSingleStep(1.0 / scale);
    spinBox->setValue(defaultValue / scale);
    spinBox->setToolTip(toolTip);
    // A dot is always the decimal separator, whatever the system locale.
    spinBox->setLocale(QLocale::C);
    spinBox->setGroupSeparatorShown(false);

    QHBoxLayout *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);
    QLabel *rowLabel = new QLabel(label, this);
    rowLabel->setMinimumWidth(90);
    row->addWidget(rowLabel);
    row->addWidget(slider, 1);
    row->addWidget(spinBox, 0);
    layout->addLayout(row);

    connect(slider, &QSlider::valueChanged, this, [this, slider, spinBox, scale](int value) {
        const QSignalBlocker blocker(spinBox);
        spinBox->setValue(value / scale);
        onDevelopChanged();
        Q_UNUSED(slider);
    });
    connect(spinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [slider, scale](double value) {
        slider->setValue(static_cast<int>(std::lround(value * scale)));
    });
}

void MainWindow::setDevelopValue(Develop id, int value)
{
    DevelopControl &control = developControl(id);
    const QSignalBlocker sliderBlocker(control.slider);
    const QSignalBlocker spinBoxBlocker(control.spinBox);
    control.slider->setValue(value);
    control.spinBox->setValue(control.slider->value() / control.scale);
}

double MainWindow::developValue(Develop id) const
{
    const DevelopControl &control = developControl(id);
    return control.slider->value() / control.scale;
}

superccd::EditParams MainWindow::currentEditParams() const
{
    superccd::EditParams params;
    params.exposureEv = developValue(Develop::Exposure);
    params.contrast = developValue(Develop::Contrast);
    params.highlights = developValue(Develop::Highlights);
    params.shadows = developValue(Develop::Shadows);
    params.whites = developValue(Develop::Whites);
    params.blacks = developValue(Develop::Blacks);
    params.gamma = developValue(Develop::Gamma);
    params.kelvin = m_temperatureKelvin;
    params.tint = developValue(Develop::Tint);
    params.vibrance = developValue(Develop::Vibrance);
    params.saturation = developValue(Develop::Saturation);
    params.sharpenAmount = developValue(Develop::SharpenAmount);
    params.reconstructHighlights = m_reconstructCheckBox->isChecked();
    params.reconstructThreshold = developValue(Develop::ReconstructThreshold);
    params.reconstructNeutralize = developValue(Develop::ReconstructNeutralize);
    return params;
}

void MainWindow::setTemperatureKelvin(int kelvin)
{
    m_temperatureKelvin = kelvin;
    // As shot shows the camera's own value, read from the current photo.
    int shown = kelvin;
    if (shown <= 0) {
        shown = m_scene
            ? static_cast<int>(std::lround(superccd::solveWhiteBalance(*m_scene, 0.0, 0.0).kelvin))
            : 5500;
    }
    const QSignalBlocker sliderBlocker(m_temperatureSlider);
    const QSignalBlocker spinBoxBlocker(m_temperatureSpinBox);
    m_temperatureSlider->setValue(sliderFromKelvin(shown));
    m_temperatureSpinBox->setValue(shown);
}

ConversionSettings MainWindow::currentSettings() const
{
    ConversionSettings settings;
    settings.exportMode = ExportMode::RawCfa6MP;
    settings.previewMethod = currentPreviewMethod();
    settings.rTransitionStart = mergeStartFromSlider(m_rTransitionStartSlider->value());
    settings.rTransitionDelay = static_cast<double>(m_rTransitionDelaySlider->value()) / 100.0;
    settings.rTransitionSmoothness = static_cast<double>(m_rTransitionSmoothnessSlider->value()) / 100.0;
    settings.rLumaNoiseReduction = static_cast<double>(m_rLumaNoiseReductionSlider->value()) / 100.0;
    settings.chromaNoiseReduction = static_cast<double>(m_chromaNoiseReductionSlider->value()) / 100.0;
    settings.previewRotation = m_previewRotationCombo->currentData().toInt();
    settings.linearChromaSuppression = 1.0;
    settings.correctPreviewOutliers =
        m_correctPreviewOutliersCheckBox->isChecked();
    settings.exportPlaneImages = m_exportPlaneImagesCheckBox->isChecked();
    return settings;
}

PreviewMethod MainWindow::currentPreviewMethod() const
{
    const int id = m_previewMethodGroup->checkedId();
    if (id == static_cast<int>(PreviewMethod::AmazeDebayer)) {
        return PreviewMethod::AmazeDebayer;
    }
    return PreviewMethod::Reconstruction;
}

void MainWindow::onPreviewMethodChanged()
{
    m_whiteBalancePickerButton->setChecked(false);
    m_lastPreviewedInputPath.clear();
    updateControls(false);
    queueAutoPreview();
    
    // Rebuild the preview image from the raw data using the new method
    if (hasCurrentPreview()) {
        onUpdatePreview();
    }
}
