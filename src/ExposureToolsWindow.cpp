#include "ExposureToolsWindow.h"

#include "HistogramWidget.h"
#include "WaveformWidget.h"
#include "VectorscopeWidget.h"

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
    m_vectorscope = new VectorscopeWidget(this);

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

    // Vectorscope tab: a small toolbar with a "Transparency" slider,
    // mirroring the waveform tab's transparency control.
    m_vectorscopeTransparencySlider = new QSlider(Qt::Horizontal, this);
    m_vectorscopeTransparencySlider->setRange(0, 100);
    m_vectorscopeTransparencySlider->setValue(0);
    m_vectorscopeTransparencySlider->setToolTip(
        tr("How transparent the vectorscope dots are. 0 = fully opaque, "
           "100 = fully transparent."));
    m_vectorscopeTransparencySpinBox = new QSpinBox(this);
    m_vectorscopeTransparencySpinBox->setRange(0, 100);
    m_vectorscopeTransparencySpinBox->setValue(0);
    m_vectorscopeTransparencySpinBox->setSuffix(tr("%"));
    m_vectorscopeTransparencySpinBox->setToolTip(
        m_vectorscopeTransparencySlider->toolTip());
    QLabel *vectorscopeTransparencyLabel = new QLabel(tr("Transparency:"), this);
    connect(m_vectorscopeTransparencySlider, &QSlider::valueChanged,
            this, [this](int value) {
                if (m_vectorscopeTransparencySpinBox->value() != value) {
                    m_vectorscopeTransparencySpinBox->setValue(value);
                }
                m_vectorscope->setTransparency(value / 100.0);
            });
    connect(m_vectorscopeTransparencySpinBox,
            QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int value) {
                if (m_vectorscopeTransparencySlider->value() != value) {
                    m_vectorscopeTransparencySlider->setValue(value);
                }
                m_vectorscope->setTransparency(value / 100.0);
            });
    QHBoxLayout *vectorscopeToolbar = new QHBoxLayout;
    vectorscopeToolbar->setContentsMargins(0, 0, 0, 0);
    vectorscopeToolbar->addStretch(1);
    vectorscopeToolbar->addWidget(vectorscopeTransparencyLabel);
    vectorscopeToolbar->addWidget(m_vectorscopeTransparencySlider, 2);
    vectorscopeToolbar->addWidget(m_vectorscopeTransparencySpinBox, 0);
    QVBoxLayout *vectorscopeTabLayout = new QVBoxLayout;
    vectorscopeTabLayout->setContentsMargins(6, 6, 6, 6);
    vectorscopeTabLayout->addLayout(vectorscopeToolbar);
    vectorscopeTabLayout->addWidget(m_vectorscope, 1);
    QWidget *vectorscopeTab = new QWidget(this);
    vectorscopeTab->setLayout(vectorscopeTabLayout);

    m_tabWidget->addTab(m_histogram, tr("Histogram"));
    m_tabWidget->addTab(waveformTab, tr("Waveform"));
    m_tabWidget->addTab(vectorscopeTab, tr("Vectorscope"));

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
    m_vectorscope->setSourceImage(image, rect);
}

bool ExposureToolsWindow::metersVisibleAreaOnly() const
{
    return m_meterVisibleCheckBox && m_meterVisibleCheckBox->isChecked();
}
