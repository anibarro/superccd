#include "SuperCCDProcessor.h"
#include "PreviewImageProcessing.h"

#include <QCoreApplication>
#include <QImage>
#include <QSettings>
#include <QString>

#include <cstdio>
#include <cstring>

namespace {

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

PreviewAdjustmentValues loadPreviewAdjustments()
{
    QSettings settings(QStringLiteral("superccd"), QStringLiteral("superccd2dng"));
    PreviewAdjustmentValues adjustments;
    adjustments.exposureTenthsEv =
        settings.value(QStringLiteral("defaults/previewExposureSlider"),
                       kDefaultPreviewExposureSliderValue).toInt();
    adjustments.whiteBalance =
        settings.value(QStringLiteral("defaults/previewWhiteBalanceSlider"),
                       kDefaultPreviewWhiteBalanceSliderValue).toInt();
    adjustments.tint =
        settings.value(QStringLiteral("defaults/previewTintSlider"),
                       kDefaultPreviewTintSliderValue).toInt();
    adjustments.gammaHundredths =
        settings.value(QStringLiteral("defaults/previewGammaSlider"),
                       kDefaultPreviewGammaSliderValue).toInt();
    adjustments.contrast =
        settings.value(QStringLiteral("defaults/previewContrastSlider"),
                       kDefaultPreviewContrastSliderValue).toInt();
    adjustments.shadows =
        settings.value(QStringLiteral("defaults/previewShadowsSlider"),
                       kDefaultPreviewShadowsSliderValue).toInt();
    adjustments.shadowRange =
        settings.value(QStringLiteral("defaults/previewShadowRangeSlider"),
                       kDefaultPreviewShadowRangeSliderValue).toInt();
    adjustments.saturation =
        settings.value(QStringLiteral("defaults/previewSaturationSlider"),
                       kDefaultPreviewSaturationSliderValue).toInt();
    adjustments.highlightCompression =
        settings.value(QStringLiteral("defaults/previewHighlightCompressionSlider"),
                       kDefaultPreviewHighlightCompressionSliderValue).toInt();
    return adjustments;
}

}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: preview_render_probe <input> <output> [--max-size=N] [--rotation=0|90|180|270] [--export-defaults]\n");
        return 2;
    }

    ConversionSettings settings;
    settings.exportMode = ExportMode::RawCfa6MP;
    settings.previewMaxSize = 0;
    settings.previewRotation = 0;
    bool applyExportDefaults = false;

    for (int i = 3; i < argc; ++i) {
        if (std::strncmp(argv[i], "--max-size=", 11) == 0) {
            settings.previewMaxSize = std::atoi(argv[i] + 11);
        } else if (std::strncmp(argv[i], "--rotation=", 11) == 0) {
            settings.previewRotation = std::atoi(argv[i] + 11);
        } else if (std::strcmp(argv[i], "--export-defaults") == 0) {
            applyExportDefaults = true;
        } else {
            std::fprintf(stderr, "unknown option: %s\n", argv[i]);
            return 2;
        }
    }

    SuperCCDProcessor processor;
    QImage preview;
    QString error;
    if (!processor.renderPreview(QString::fromLocal8Bit(argv[1]), settings, preview, error)) {
        std::fprintf(stderr, "preview render failed: %s\n", error.toUtf8().constData());
        return 1;
    }

    if (applyExportDefaults) {
        preview = PreviewImageProcessing::applyExportAdjustments16(
            preview,
            loadPreviewAdjustments());
        QSettings settingsStore(QStringLiteral("superccd"), QStringLiteral("superccd2dng"));
        PreviewImageProcessing::applyLumaSharpening16(
            preview,
            settingsStore.value(QStringLiteral("defaults/previewSharpeningSlider"),
                                kDefaultPreviewSharpeningSliderValue).toInt());
    }

    const QString outputPath = QString::fromLocal8Bit(argv[2]);
    if (applyExportDefaults &&
        outputPath.endsWith(QStringLiteral(".jpg"), Qt::CaseInsensitive)) {
        QSettings settingsStore(QStringLiteral("superccd"), QStringLiteral("superccd2dng"));
        const int quality =
            settingsStore.value(QStringLiteral("previewExport/quality"), 90).toInt();
        const QImage jpegImage = preview.convertToFormat(QImage::Format_RGB32);
        if (!jpegImage.save(outputPath, "JPG", quality)) {
            std::fprintf(stderr, "failed to save preview image\n");
            return 1;
        }
    } else if (!preview.save(outputPath)) {
        std::fprintf(stderr, "failed to save preview image\n");
        return 1;
    }

    return 0;
}
