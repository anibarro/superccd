#include "ExposureToolsWindow.h"

#include "HistogramWidget.h"
#include "WaveformWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

ExposureToolsWindow::ExposureToolsWindow(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle(tr("Exposure Tools"));
    setMinimumSize(420, 360);
    resize(620, 460);

    m_tabWidget = new QTabWidget(this);
    m_histogram = new HistogramWidget(this);
    m_waveform = new WaveformWidget(this);

    // Waveform tab: a small toolbar above the waveform widget with a
    // "Waveform mode" dropdown. Default is "RGB split".
    m_waveformModeCombo = new QComboBox(this);
    m_waveformModeCombo->addItem(tr("All"),
                                 static_cast<int>(WaveformWidget::AllChannels));
    m_waveformModeCombo->addItem(tr("RGB split"),
                                 static_cast<int>(WaveformWidget::RgbSplit));
    m_waveformModeCombo->addItem(tr("Luma"),
                                 static_cast<int>(WaveformWidget::LumaOnly));
    m_waveformModeCombo->setCurrentIndex(
        m_waveformModeCombo->findData(
            static_cast<int>(WaveformWidget::RgbSplit)));
    m_waveform->setMode(WaveformWidget::RgbSplit);
    connect(m_waveformModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                const int modeValue = m_waveformModeCombo->itemData(index).toInt();
                m_waveform->setMode(
                    static_cast<WaveformWidget::Mode>(modeValue));
            });

    QLabel *waveformModeLabel = new QLabel(tr("Waveform mode:"), this);

    // Transparency slider: 0 = fully opaque, 100 = fully transparent.
    // The value maps to WaveformWidget::setTransparency(value / 100.0).
    m_waveformTransparencySlider = new QSlider(Qt::Horizontal, this);
    m_waveformTransparencySlider->setRange(0, 100);
    m_waveformTransparencySlider->setValue(0);
    m_waveformTransparencySlider->setToolTip(
        tr("How transparent the waveform is. 0 = fully opaque, "
           "100 = fully transparent."));
    m_waveformTransparencySpinBox = new QSpinBox(this);
    m_waveformTransparencySpinBox->setRange(0, 100);
    m_waveformTransparencySpinBox->setValue(0);
    m_waveformTransparencySpinBox->setSuffix(tr("%"));
    m_waveformTransparencySpinBox->setToolTip(
        m_waveformTransparencySlider->toolTip());
    QLabel *waveformTransparencyLabel = new QLabel(tr("Transparency:"), this);
    connect(m_waveformTransparencySlider, &QSlider::valueChanged,
            this, [this](int value) {
                if (m_waveformTransparencySpinBox->value() != value) {
                    m_waveformTransparencySpinBox->setValue(value);
                }
                m_waveform->setTransparency(value / 100.0);
            });
    connect(m_waveformTransparencySpinBox,
            QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int value) {
                if (m_waveformTransparencySlider->value() != value) {
                    m_waveformTransparencySlider->setValue(value);
                }
                m_waveform->setTransparency(value / 100.0);
            });

    QHBoxLayout *waveformToolbar = new QHBoxLayout;
    waveformToolbar->setContentsMargins(0, 0, 0, 0);
    waveformToolbar->addWidget(waveformModeLabel);
    waveformToolbar->addWidget(m_waveformModeCombo, 1);
    waveformToolbar->addSpacing(12);
    waveformToolbar->addWidget(waveformTransparencyLabel);
    waveformToolbar->addWidget(m_waveformTransparencySlider, 2);
    waveformToolbar->addWidget(m_waveformTransparencySpinBox, 0);
    QVBoxLayout *waveformTabLayout = new QVBoxLayout;
    waveformTabLayout->setContentsMargins(6, 6, 6, 6);
    waveformTabLayout->addLayout(waveformToolbar);
    waveformTabLayout->addWidget(m_waveform, 1);
    QWidget *waveformTab = new QWidget(this);
    waveformTab->setLayout(waveformTabLayout);

    // Histogram tab: a small toolbar with a "Histogram mode" dropdown
    // mirroring the waveform's mode selector.
    m_histogramModeCombo = new QComboBox(this);
    m_histogramModeCombo->addItem(tr("All"),
                                  static_cast<int>(HistogramWidget::AllChannels));
    m_histogramModeCombo->addItem(tr("RGB split"),
                                  static_cast<int>(HistogramWidget::RgbSplit));
    m_histogramModeCombo->addItem(tr("Luma"),
                                  static_cast<int>(HistogramWidget::LumaOnly));
    m_histogramModeCombo->setCurrentIndex(
        m_histogramModeCombo->findData(
            static_cast<int>(HistogramWidget::AllChannels)));
    m_histogram->setMode(HistogramWidget::AllChannels);
    connect(m_histogramModeCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                const int modeValue = m_histogramModeCombo->itemData(index).toInt();
                m_histogram->setMode(
                    static_cast<HistogramWidget::Mode>(modeValue));
            });
    QLabel *histogramModeLabel = new QLabel(tr("Histogram mode:"), this);
    QHBoxLayout *histogramToolbar = new QHBoxLayout;
    histogramToolbar->setContentsMargins(0, 0, 0, 0);
    histogramToolbar->addWidget(histogramModeLabel);
    histogramToolbar->addWidget(m_histogramModeCombo, 1);
    histogramToolbar->addStretch(2);
    QVBoxLayout *histogramTabLayout = new QVBoxLayout;
    histogramTabLayout->setContentsMargins(6, 6, 6, 6);
    histogramTabLayout->addLayout(histogramToolbar);
    histogramTabLayout->addWidget(m_histogram, 1);
    QWidget *histogramTab = new QWidget(this);
    histogramTab->setLayout(histogramTabLayout);

    m_tabWidget->addTab(histogramTab, tr("Histogram"));
    m_tabWidget->addTab(waveformTab, tr("Waveform"));

    m_meterVisibleCheckBox = new QCheckBox(
        tr("Meter visible area only"), this);
    m_meterVisibleCheckBox->setToolTip(
        tr("When enabled, the exposure tools only sample the part of the "
           "image currently visible inside the preview window."));

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addWidget(m_tabWidget, 1);
    layout->addWidget(m_meterVisibleCheckBox);
}

void ExposureToolsWindow::setSourceImage(const QImage &image, const QRect &visibleRect)
{
    const QRect rect = metersVisibleAreaOnly() ? visibleRect : QRect();
    m_histogram->setSourceImage(image, rect);
    m_waveform->setSourceImage(image, rect);
}

bool ExposureToolsWindow::metersVisibleAreaOnly() const
{
    return m_meterVisibleCheckBox && m_meterVisibleCheckBox->isChecked();
}
