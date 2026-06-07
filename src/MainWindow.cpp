#include "MainWindow.h"

#include <QAction>
#include <QBoxLayout>
#include <QButtonGroup>
#include <QCheckBox>
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
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSettings>
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
#include <array>

namespace {
constexpr int kDefaultDelaySliderValue = 50;
constexpr int kDefaultSmoothnessSliderValue = 50;
constexpr int kDefaultPreviewExposureSliderValue = 0;
constexpr int kDefaultPreviewWhiteBalanceSliderValue = 0;
constexpr int kDefaultPreviewTintSliderValue = 0;
constexpr int kDefaultPreviewHighlightCompressionSliderValue = 0;
constexpr int kDefaultPreviewZoomSliderValue = 20;
constexpr bool kDefaultAutoPreview = false;
constexpr int kPreviewToneLutMaxInput = 8192;

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

QWidget *createFileListRow(const QString &displayName,
                           const QString &fullPath,
                           const QImage &thumbnail,
                           QWidget *parent)
{
    QWidget *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(8);

    QLabel *thumbLabel = new QLabel(row);
    thumbLabel->setFixedSize(96, 96);
    thumbLabel->setAlignment(Qt::AlignCenter);
    thumbLabel->setStyleSheet(QStringLiteral("background:#2a2a2a; border:1px solid #505050;"));
    if (!thumbnail.isNull()) {
        thumbLabel->setPixmap(QPixmap::fromImage(thumbnail.scaled(96, 96,
                                                                  Qt::KeepAspectRatio,
                                                                  Qt::SmoothTransformation)));
    } else {
        thumbLabel->setText(QStringLiteral("No\nthumb"));
    }

    QLabel *textLabel = new QLabel(displayName, row);
    textLabel->setToolTip(fullPath);
    textLabel->setWordWrap(true);
    textLabel->setTextInteractionFlags(Qt::NoTextInteraction);

    layout->addWidget(thumbLabel, 0);
    layout->addWidget(textLabel, 1);
    return row;
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_fileList(new QListWidget(this))
    , m_outputFolder(new QLineEdit(this))
    , m_rTransitionDelaySlider(new QSlider(Qt::Horizontal, this))
    , m_rTransitionDelayValueLabel(new QLabel(this))
    , m_rTransitionSmoothnessSlider(new QSlider(Qt::Horizontal, this))
    , m_rTransitionSmoothnessValueLabel(new QLabel(this))
    , m_previewZoomSlider(new QSlider(Qt::Horizontal, this))
    , m_previewZoomValueLabel(new QLabel(this))
    , m_previewExposureSlider(new QSlider(Qt::Horizontal, this))
    , m_previewExposureValueLabel(new QLabel(this))
    , m_previewWhiteBalanceSlider(new QSlider(Qt::Horizontal, this))
    , m_previewWhiteBalanceValueLabel(new QLabel(this))
    , m_previewTintSlider(new QSlider(Qt::Horizontal, this))
    , m_previewTintValueLabel(new QLabel(this))
    , m_previewGammaSlider(new QSlider(Qt::Horizontal, this))
    , m_previewGammaValueLabel(new QLabel(this))
    , m_previewContrastSlider(new QSlider(Qt::Horizontal, this))
    , m_previewContrastValueLabel(new QLabel(this))
    , m_previewSaturationSlider(new QSlider(Qt::Horizontal, this))
    , m_previewSaturationValueLabel(new QLabel(this))
    , m_previewHighlightCompressionSlider(new QSlider(Qt::Horizontal, this))
    , m_previewHighlightCompressionValueLabel(new QLabel(this))
    , m_previewRotationCombo(new QComboBox(this))
    , m_autoPreviewCheckBox(new QCheckBox(tr("Update preview automatically"), this))
    , m_previewScrollArea(new QScrollArea(this))
    , m_previewLabel(new QLabel(this))
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
{
    setWindowTitle(tr("SuperCCD RAF to DNG Converter v%1").arg(QString::fromLatin1(APP_VERSION_STRING)));
    resize(1180, 760);
    setAcceptDrops(true);

    QWidget *central = new QWidget(this);
    setCentralWidget(central);

    QPushButton *addFilesButton = new QPushButton(tr("Add RAF Files..."), this);
    QPushButton *removeFilesButton = new QPushButton(tr("Remove Selected"), this);
    QPushButton *selectFolderButton = new QPushButton(tr("Select Output Folder..."), this);
    QLabel *warmupNoteLabel = new QLabel(
        tr("Note: the first preview or conversion for a RAF file is slower because the app has to decode and cache the raw data."),
        this);

    m_rTransitionDelaySlider->setRange(0, 100);
    m_rTransitionDelaySlider->setValue(kDefaultDelaySliderValue);
    m_rTransitionDelayValueLabel->setText(QString::number(kDefaultDelaySliderValue / 100.0, 'f', 2));
    m_rTransitionSmoothnessSlider->setRange(0, 100);
    m_rTransitionSmoothnessSlider->setValue(kDefaultSmoothnessSliderValue);
    m_rTransitionSmoothnessValueLabel->setText(QString::number(kDefaultSmoothnessSliderValue / 100.0, 'f', 2));
    m_previewZoomSlider->setRange(5, 400);
    m_previewZoomSlider->setValue(kDefaultPreviewZoomSliderValue);
    m_previewZoomValueLabel->setText(QStringLiteral("%1%").arg(kDefaultPreviewZoomSliderValue));
    m_previewExposureSlider->setRange(-30, 40);
    m_previewExposureSlider->setValue(kDefaultPreviewExposureSliderValue);
    m_previewExposureValueLabel->setText(QStringLiteral("%1%2 EV")
                                             .arg(kDefaultPreviewExposureSliderValue >= 0 ? "+" : "")
                                             .arg(kDefaultPreviewExposureSliderValue / 10.0, 0, 'f', 1));
    m_previewWhiteBalanceSlider->setRange(-100, 100);
    m_previewWhiteBalanceSlider->setValue(kDefaultPreviewWhiteBalanceSliderValue);
    m_previewWhiteBalanceValueLabel->setText(QString::number(kDefaultPreviewWhiteBalanceSliderValue));
    m_previewTintSlider->setRange(-100, 100);
    m_previewTintSlider->setValue(kDefaultPreviewTintSliderValue);
    m_previewTintValueLabel->setText(QString::number(kDefaultPreviewTintSliderValue));
    // Gamma slider: range 0-300 (0 to 3.0), default 220 (gamma 2.2)
    m_previewGammaSlider->setRange(0, 300);
    m_previewGammaSlider->setValue(220);
    m_previewGammaValueLabel->setText(QStringLiteral("2.20"));
    // Contrast slider: range -200 to +200, default 0
    m_previewContrastSlider->setRange(-200, 200);
    m_previewContrastSlider->setValue(0);
    m_previewContrastValueLabel->setText(QStringLiteral("0"));
    // Saturation slider: range -100 to +100, default 0
    m_previewSaturationSlider->setRange(-100, 100);
    m_previewSaturationSlider->setValue(0);
    m_previewSaturationValueLabel->setText(QStringLiteral("0"));
    // Highlight compression slider: range 0 to 100, default 0
    m_previewHighlightCompressionSlider->setRange(0, 100);
    m_previewHighlightCompressionSlider->setValue(kDefaultPreviewHighlightCompressionSliderValue);
    m_previewHighlightCompressionValueLabel->setText(QStringLiteral("0"));
    m_previewRotationCombo->addItem(tr("Normal"), 0);
    m_previewRotationCombo->addItem(tr("Rotate 90 CW"), 90);
    m_previewRotationCombo->addItem(tr("Rotate 180"), 180);
    m_previewRotationCombo->addItem(tr("Rotate 90 CCW"), 270);
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
    m_previewScrollArea->setMinimumWidth(960);
    m_previewScrollArea->setMinimumHeight(320);
    m_previewScrollArea->viewport()->installEventFilter(this);
    m_previewLabel->installEventFilter(this);
    m_statusClearTimer->setSingleShot(true);
    m_autoPreviewTimer->setSingleShot(true);
    m_autoPreviewTimer->setInterval(250);
    warmupNoteLabel->setWordWrap(true);
    warmupNoteLabel->setStyleSheet(QStringLiteral("color:#808080;"));

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
    transitionLayout->addRow(tr("Handoff delay:"), m_rTransitionDelaySlider);
    transitionLayout->addRow(tr(""), m_rTransitionDelayValueLabel);
    transitionLayout->addRow(tr("Smoothness:"), m_rTransitionSmoothnessSlider);
    transitionLayout->addRow(tr(""), m_rTransitionSmoothnessValueLabel);

    // Preview controls group box
    QGroupBox *previewGroup = new QGroupBox(tr("Preview Controls"), this);
    QFormLayout *previewControlsLayout = new QFormLayout(previewGroup);
    previewControlsLayout->addRow(tr("Exposure:"), m_previewExposureSlider);
    previewControlsLayout->addRow(tr(""), m_previewExposureValueLabel);
    previewControlsLayout->addRow(tr("Gamma:"), m_previewGammaSlider);
    previewControlsLayout->addRow(tr(""), m_previewGammaValueLabel);
    previewControlsLayout->addRow(tr("Contrast:"), m_previewContrastSlider);
    previewControlsLayout->addRow(tr(""), m_previewContrastValueLabel);
    previewControlsLayout->addRow(tr("Saturation:"), m_previewSaturationSlider);
    previewControlsLayout->addRow(tr(""), m_previewSaturationValueLabel);
    previewControlsLayout->addRow(tr("Highlight compression:"), m_previewHighlightCompressionSlider);
    previewControlsLayout->addRow(tr(""), m_previewHighlightCompressionValueLabel);
    previewControlsLayout->addRow(tr("White balance:"), m_previewWhiteBalanceSlider);
    previewControlsLayout->addRow(tr(""), m_previewWhiteBalanceValueLabel);
    previewControlsLayout->addRow(tr("Tint:"), m_previewTintSlider);
    previewControlsLayout->addRow(tr(""), m_previewTintValueLabel);
    previewControlsLayout->addRow(tr("Rotation:"), m_previewRotationCombo);
    previewControlsLayout->addRow(tr("Zoom:"), m_previewZoomSlider);
    previewControlsLayout->addRow(tr(""), m_previewZoomValueLabel);
    previewControlsLayout->addRow(tr(""), m_autoPreviewCheckBox);

    QFormLayout *optionsLayout = new QFormLayout;
    optionsLayout->addRow(tr("Output folder:"), m_outputFolder);
    optionsLayout->addRow(tr(""), selectFolderButton);
    optionsLayout->addRow(transitionGroup);
    optionsLayout->addRow(previewGroup);

    QHBoxLayout *defaultsButtonsLayout = new QHBoxLayout;
    defaultsButtonsLayout->addWidget(m_resetDefaultsButton);
    defaultsButtonsLayout->addWidget(m_saveDefaultsButton);

    QVBoxLayout *rightLayout = new QVBoxLayout;
    rightLayout->addLayout(optionsLayout);
    rightLayout->addLayout(defaultsButtonsLayout);
    QHBoxLayout *previewButtonsLayout = new QHBoxLayout;
    previewButtonsLayout->addWidget(m_previewButton);
    previewButtonsLayout->addWidget(m_exportPreviewButton);
    rightLayout->addLayout(previewButtonsLayout);
    rightLayout->addWidget(warmupNoteLabel);

    QHBoxLayout *topRowLayout = new QHBoxLayout;
    topRowLayout->addLayout(leftLayout, 2);
    topRowLayout->addLayout(rightLayout, 1);

    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->addLayout(topRowLayout, 1);
    mainLayout->addWidget(m_previewScrollArea, 2);
    mainLayout->addWidget(m_statusLabel);
    central->setLayout(mainLayout);

    connect(addFilesButton, &QPushButton::clicked, this, &MainWindow::onAddFiles);
    connect(removeFilesButton, &QPushButton::clicked, this, &MainWindow::onRemoveSelectedFiles);
    connect(selectFolderButton, &QPushButton::clicked, this, &MainWindow::onSelectOutputFolder);
    connect(m_convertCurrentButton, &QPushButton::clicked, this, &MainWindow::onConvertCurrent);
    connect(m_convertAllButton, &QPushButton::clicked, this, &MainWindow::onConvertAll);
    connect(m_previewButton, &QPushButton::clicked, this, &MainWindow::onUpdatePreview);
    connect(m_exportPreviewButton, &QPushButton::clicked, this, &MainWindow::onExportPreview);
    connect(m_fileList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0) {
            queueAutoPreview();
            updateControls(false);
        }
    });
    connect(m_resetDefaultsButton, &QPushButton::clicked, this, &MainWindow::onResetDefaults);
    connect(m_saveDefaultsButton, &QPushButton::clicked, this, &MainWindow::onSaveDefaults);
    connect(m_statusClearTimer, &QTimer::timeout, m_statusLabel, &QLabel::clear);
    connect(m_autoPreviewTimer, &QTimer::timeout, this, &MainWindow::onAutoPreviewTimer);
    connect(m_rTransitionDelaySlider, &QSlider::valueChanged, this, [this](int value) {
        m_rTransitionDelayValueLabel->setText(QString::number(static_cast<double>(value) / 100.0, 'f', 2));
        queueAutoPreview();
    });
    connect(m_rTransitionSmoothnessSlider, &QSlider::valueChanged, this, [this](int value) {
        m_rTransitionSmoothnessValueLabel->setText(QString::number(static_cast<double>(value) / 100.0, 'f', 2));
        queueAutoPreview();
    });
    connect(m_previewZoomSlider, &QSlider::valueChanged, this, &MainWindow::onPreviewZoomChanged);
    connect(m_previewExposureSlider, &QSlider::valueChanged, this, &MainWindow::onPreviewExposureChanged);
    connect(m_previewWhiteBalanceSlider, &QSlider::valueChanged, this, &MainWindow::onPreviewWhiteBalanceChanged);
    connect(m_previewTintSlider, &QSlider::valueChanged, this, &MainWindow::onPreviewTintChanged);
    connect(m_previewGammaSlider, &QSlider::valueChanged, this, &MainWindow::onPreviewGammaChanged);
    connect(m_previewContrastSlider, &QSlider::valueChanged, this, &MainWindow::onPreviewContrastChanged);
    connect(m_previewSaturationSlider, &QSlider::valueChanged, this, &MainWindow::onPreviewSaturationChanged);
    connect(m_previewHighlightCompressionSlider, &QSlider::valueChanged, this, &MainWindow::onPreviewHighlightCompressionChanged);
    connect(m_previewRotationCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        queueAutoPreview();
    });
    connect(m_autoPreviewCheckBox, &QCheckBox::toggled, this, [this](bool enabled) {
        if (enabled) {
            queueAutoPreview();
        } else {
            m_autoPreviewTimer->stop();
        }
    });

    loadSavedDefaults();
    updateControls(false);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if ((watched == m_previewLabel || watched == m_previewScrollArea->viewport()) && !m_currentPreviewImage.isNull()) {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
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
            item->setSizeHint(QSize(0, 104));
            QImage thumbnail;
            SuperCCDProcessor::extractEmbeddedThumbnail(file, thumbnail, nullptr);
            m_fileList->setItemWidget(item, createFileListRow(info.fileName(), file, thumbnail, m_fileList));
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
            item->setSizeHint(QSize(0, 104));
            QImage thumbnail;
            SuperCCDProcessor::extractEmbeddedThumbnail(file, thumbnail, nullptr);
            m_fileList->setItemWidget(item, createFileListRow(info.fileName(), file, thumbnail, m_fileList));
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
        m_previewLabel->clear();
        m_previewLabel->setText(tr("Preview not generated."));
        m_previewLabel->unsetCursor();
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
        m_lastPreviewedInputPath = inputPath;
        m_previewLabel->setPixmap(QPixmap::fromImage(preview.scaled(m_previewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
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
        m_previewLabel->setText(tr("Preview failed."));
        showStatus(tr("Preview failed: %1").arg(error));
        m_busy = false;
        updateControls(false);
        return;
    }

    const bool shouldFitPreview = m_currentPreviewImage.isNull()
        || m_currentPreviewImage.size() != preview.size()
        || m_lastPreviewedInputPath != inputPath;

    m_currentPreviewImage = preview;
    m_lastPreviewedInputPath = inputPath;
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
            m_previewZoomValueLabel->setText(QStringLiteral("%1%").arg(fitZoom));
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

    QSpinBox *qualitySpinBox = new QSpinBox(&dialog);
    qualitySpinBox->setRange(1, 100);
    qualitySpinBox->setValue(std::clamp(settingsStore.value(QStringLiteral("previewExport/quality"), 90).toInt(), 1, 100));
    qualitySpinBox->setSuffix(tr("%"));
    layout->addRow(tr("JPEG quality:"), qualitySpinBox);

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

    const QString outputPath = QDir(targetFolder).filePath(QFileInfo(inputPath).completeBaseName() + QStringLiteral("_preview.jpg"));
    if (QFile::exists(outputPath)) {
        const auto overwrite = QMessageBox::question(this,
                                                     tr("Overwrite Preview"),
                                                     tr("The file already exists:\n%1\n\nOverwrite it?").arg(outputPath));
        if (overwrite != QMessageBox::Yes) {
            return;
        }
    }

    const QImage exportImage = buildAdjustedPreviewImage();
    if (exportImage.isNull()) {
        showStatus(tr("Preview export failed."));
        return;
    }

    const int quality = qualitySpinBox->value();
    if (!exportImage.save(outputPath, "JPG", quality)) {
        showStatus(tr("Could not save preview JPG."));
        return;
    }

    settingsStore.setValue(QStringLiteral("previewExport/folder"), targetFolder);
    settingsStore.setValue(QStringLiteral("previewExport/quality"), quality);
    showStatus(tr("Preview exported to %1").arg(outputPath));
}

void MainWindow::updateControls(bool busy)
{
    m_fileList->setEnabled(!busy);
    m_outputFolder->setEnabled(!busy);
    m_rTransitionDelaySlider->setEnabled(!busy);
    m_rTransitionDelayValueLabel->setEnabled(!busy);
    m_rTransitionSmoothnessSlider->setEnabled(!busy);
    m_rTransitionSmoothnessValueLabel->setEnabled(!busy);
    m_previewZoomSlider->setEnabled(!busy);
    m_previewZoomValueLabel->setEnabled(!busy);
    m_previewExposureSlider->setEnabled(!busy);
    m_previewExposureValueLabel->setEnabled(!busy);
    m_previewWhiteBalanceSlider->setEnabled(!busy);
    m_previewWhiteBalanceValueLabel->setEnabled(!busy);
    m_previewTintSlider->setEnabled(!busy);
    m_previewTintValueLabel->setEnabled(!busy);
    m_previewGammaSlider->setEnabled(!busy);
    m_previewGammaValueLabel->setEnabled(!busy);
    m_previewContrastSlider->setEnabled(!busy);
    m_previewContrastValueLabel->setEnabled(!busy);
    m_previewSaturationSlider->setEnabled(!busy);
    m_previewSaturationValueLabel->setEnabled(!busy);
    m_previewHighlightCompressionSlider->setEnabled(!busy);
    m_previewHighlightCompressionValueLabel->setEnabled(!busy);
    m_previewRotationCombo->setEnabled(!busy);
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

void MainWindow::onPreviewZoomChanged(int value)
{
    m_previewZoomValueLabel->setText(QStringLiteral("%1%").arg(value));
    updatePreviewDisplay();
}

void MainWindow::onPreviewExposureChanged(int value)
{
    const double ev = static_cast<double>(value) / 10.0;
    m_previewExposureValueLabel->setText(QStringLiteral("%1%2 EV")
                                             .arg(ev >= 0.0 ? "+" : "")
                                             .arg(ev, 0, 'f', 1));
    updatePreviewDisplay();
}

void MainWindow::onPreviewWhiteBalanceChanged(int value)
{
    m_previewWhiteBalanceValueLabel->setText(QString::number(value));
    updatePreviewDisplay();
}

void MainWindow::onPreviewTintChanged(int value)
{
    m_previewTintValueLabel->setText(QString::number(value));
    updatePreviewDisplay();
}

void MainWindow::onPreviewGammaChanged(int value)
{
    const double gamma = static_cast<double>(value) / 100.0;
    m_previewGammaValueLabel->setText(QStringLiteral("%1").arg(gamma, 0, 'f', 2));
    updatePreviewDisplay();
}

void MainWindow::onPreviewContrastChanged(int value)
{
    m_previewContrastValueLabel->setText(QString::number(value));
    updatePreviewDisplay();
}

void MainWindow::onPreviewSaturationChanged(int value)
{
    m_previewSaturationValueLabel->setText(QString::number(value));
    updatePreviewDisplay();
}

void MainWindow::onPreviewHighlightCompressionChanged(int value)
{
    m_previewHighlightCompressionValueLabel->setText(QString::number(value));
    updatePreviewDisplay();
}

QImage MainWindow::buildAdjustedPreviewImage() const
{
    if (m_currentPreviewImage.isNull()) {
        return QImage();
    }

    QImage displayImage = m_currentPreviewImage.convertToFormat(QImage::Format_RGB32);

    const double exposureEv = static_cast<double>(m_previewExposureSlider->value()) / 10.0;
    const double exposureScale = std::pow(2.0, exposureEv);
    const double wbBias = static_cast<double>(m_previewWhiteBalanceSlider->value()) / 100.0;
    const double redScale = std::pow(2.0, wbBias);
    const double blueScale = std::pow(2.0, -wbBias);
    const double tintBias = static_cast<double>(m_previewTintSlider->value()) / 100.0;
    const double greenScale = std::pow(2.0, -tintBias);
    constexpr double kOriginalGamma = 2.2;
    const double newGamma = static_cast<double>(m_previewGammaSlider->value()) / 100.0;
    const double contrastBias = static_cast<double>(m_previewContrastSlider->value()) / 100.0;
    const double contrastScale = 1.0 + contrastBias;
    const double invOriginalGamma = 1.0 / kOriginalGamma;
    const double invNewGamma = 1.0 / newGamma;
    const double highlightCompression = static_cast<double>(m_previewHighlightCompressionSlider->value()) / 100.0;

    uint8_t gammaLut[256];
    for (int i = 0; i < 256; ++i) {
        double v = static_cast<double>(i);

        double vLinear = std::pow(std::clamp(v / 255.0, 0.0, 1.0), invOriginalGamma);

        if (contrastScale != 1.0) {
            vLinear = (vLinear - 0.5) * contrastScale + 0.5;
        }

        v = std::clamp(std::pow(std::clamp(vLinear, 0.0, 1.0), invNewGamma) * 255.0, 0.0, 255.0);
        gammaLut[i] = static_cast<uint8_t>(v + 0.5);
    }

    std::array<uint8_t, kPreviewToneLutMaxInput + 1> toneLut{};
    const double compressionStart = 200.0 - highlightCompression * 152.0;
    const double compressionStrength = highlightCompression * 2.0
        + highlightCompression * highlightCompression * 14.0;
    for (int i = 0; i <= kPreviewToneLutMaxInput; ++i) {
        double compressed = static_cast<double>(i);
        if (highlightCompression > 0.0 && compressed > compressionStart) {
            const double excess = compressed - compressionStart;
            compressed = compressionStart + excess / (1.0 + excess * compressionStrength / 255.0);
        }
        const int lutIndex = std::clamp(static_cast<int>(compressed + 0.5), 0, 255);
        toneLut[static_cast<std::size_t>(i)] = gammaLut[lutIndex];
    }

    const double saturationBias = static_cast<double>(m_previewSaturationSlider->value()) / 100.0;
    const double saturationScale = 1.0 + saturationBias;

    for (int y = 0; y < displayImage.height(); ++y) {
        QRgb *scanLine = reinterpret_cast<QRgb *>(displayImage.scanLine(y));
        for (int x = 0; x < displayImage.width(); ++x) {
            const QRgb src = scanLine[x];

            int r = static_cast<int>(qRed(src) * exposureScale * redScale + 0.5);
            int g = static_cast<int>(qGreen(src) * exposureScale * greenScale + 0.5);
            int b = static_cast<int>(qBlue(src) * exposureScale * blueScale + 0.5);

            r = toneLut[static_cast<std::size_t>(std::clamp(r, 0, kPreviewToneLutMaxInput))];
            g = toneLut[static_cast<std::size_t>(std::clamp(g, 0, kPreviewToneLutMaxInput))];
            b = toneLut[static_cast<std::size_t>(std::clamp(b, 0, kPreviewToneLutMaxInput))];

            if (saturationScale != 1.0) {
                const double gray = (r + g + b) / 3.0;
                r = static_cast<int>(std::clamp(gray + (r - gray) * saturationScale, 0.0, 255.0) + 0.5);
                g = static_cast<int>(std::clamp(gray + (g - gray) * saturationScale, 0.0, 255.0) + 0.5);
                b = static_cast<int>(std::clamp(gray + (b - gray) * saturationScale, 0.0, 255.0) + 0.5);
            }

            scanLine[x] = qRgb(r, g, b);
        }
    }

    return displayImage;
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
    const QSize scaledSize(std::max(1, static_cast<int>(m_currentPreviewImage.width() * zoom)),
                           std::max(1, static_cast<int>(m_currentPreviewImage.height() * zoom)));
    const QImage displayImage = buildAdjustedPreviewImage();
    if (displayImage.isNull()) {
        return;
    }

    const QPixmap pixmap = QPixmap::fromImage(displayImage.scaled(scaledSize,
                                                                  Qt::KeepAspectRatio,
                                                                  Qt::SmoothTransformation));
    m_previewLabel->setPixmap(pixmap);
    m_previewLabel->resize(pixmap.size());
    m_previewLabel->setCursor(Qt::OpenHandCursor);

    const int newHValue = static_cast<int>(oldCenterX * pixmap.width() - viewportSize.width() * 0.5 + 0.5);
    const int newVValue = static_cast<int>(oldCenterY * pixmap.height() - viewportSize.height() * 0.5 + 0.5);
    m_previewScrollArea->horizontalScrollBar()->setValue(std::max(0, newHValue));
    m_previewScrollArea->verticalScrollBar()->setValue(std::max(0, newVValue));
}

void MainWindow::showStatus(const QString &message)
{
    m_statusLabel->setText(message);
    m_statusClearTimer->start(5000);
}

void MainWindow::applyParameterSettings(const ConversionSettings &settings)
{
    const bool oldDelaySignals = m_rTransitionDelaySlider->blockSignals(true);
    const bool oldSmoothnessSignals = m_rTransitionSmoothnessSlider->blockSignals(true);
    m_rTransitionDelaySlider->setValue(std::clamp(static_cast<int>(settings.rTransitionDelay * 100.0 + 0.5), 0, 100));
    m_rTransitionSmoothnessSlider->setValue(std::clamp(static_cast<int>(settings.rTransitionSmoothness * 100.0 + 0.5), 0, 100));
    m_rTransitionDelaySlider->blockSignals(oldDelaySignals);
    m_rTransitionSmoothnessSlider->blockSignals(oldSmoothnessSignals);
    m_rTransitionDelayValueLabel->setText(QString::number(settings.rTransitionDelay, 'f', 2));
    m_rTransitionSmoothnessValueLabel->setText(QString::number(settings.rTransitionSmoothness, 'f', 2));
}

void MainWindow::loadSavedDefaults()
{
    QSettings settingsStore = appSettings();
    ConversionSettings defaults;
    defaults.exportMode = ExportMode::RawCfa6MP;
    defaults.rTransitionDelay = settingsStore.value(QStringLiteral("defaults/rTransitionDelay"), 0.5).toDouble();
    defaults.rTransitionSmoothness = settingsStore.value(QStringLiteral("defaults/rTransitionSmoothness"), 0.5).toDouble();

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
    settingsStore.setValue(QStringLiteral("defaults/previewExposureSlider"), m_previewExposureSlider->value());
    settingsStore.setValue(QStringLiteral("defaults/previewWhiteBalanceSlider"), m_previewWhiteBalanceSlider->value());
    settingsStore.setValue(QStringLiteral("defaults/previewTintSlider"), m_previewTintSlider->value());
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
    applyParameterSettings(defaults);
    m_previewExposureSlider->setValue(kDefaultPreviewExposureSliderValue);
    m_previewWhiteBalanceSlider->setValue(kDefaultPreviewWhiteBalanceSliderValue);
    m_previewTintSlider->setValue(kDefaultPreviewTintSliderValue);
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
    settings.exportPlaneImages = m_exportPlaneImagesCheckBox->isChecked();
    return settings;
}
