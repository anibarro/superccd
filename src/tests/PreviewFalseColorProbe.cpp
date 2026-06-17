#include "PreviewFalseColorSuppression.h"
#include "PreviewImageProcessing.h"

#include <QCoreApplication>
#include <QImage>
#include <QString>

#include <cstdio>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    if (argc != 3 && argc != 4) {
        std::fprintf(stderr,
                     "usage: preview_false_color_probe [--display-defaults] <input> <output>\n");
        return 2;
    }

    bool applyDisplayDefaults = false;
    int firstPathIndex = 1;
    if (argc == 4) {
        const QString option = QString::fromLocal8Bit(argv[1]);
        if (option != QStringLiteral("--display-defaults")) {
            std::fprintf(stderr, "unknown option\n");
            return 2;
        }
        applyDisplayDefaults = true;
        firstPathIndex = 2;
    }

    const QString inputPath = QString::fromLocal8Bit(argv[firstPathIndex]);
    const QString outputPath = QString::fromLocal8Bit(argv[firstPathIndex + 1]);

    QImage image(inputPath);
    if (image.isNull()) {
        std::fprintf(stderr, "failed to load input image\n");
        return 1;
    }

    if (applyDisplayDefaults) {
        PreviewAdjustmentValues adjustments;
        adjustments.exposureTenthsEv = 0;
        adjustments.whiteBalance = 0;
        adjustments.tint = 0;
        adjustments.gammaHundredths = 40;
        adjustments.contrast = 38;
        adjustments.shadows = 0;
        adjustments.shadowRange = 100;
        adjustments.saturation = 64;
        adjustments.highlightCompression = 30;
        image = PreviewImageProcessing::applyDisplayAdjustments(image, adjustments);
    } else {
        superccd::suppressPreviewFalseColor(image);
    }

    if (!image.save(outputPath)) {
        std::fprintf(stderr, "failed to save output image\n");
        return 1;
    }

    return 0;
}
