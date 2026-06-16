#ifndef PREVIEWIMAGEPROCESSING_H
#define PREVIEWIMAGEPROCESSING_H

#include <QImage>

#include <optional>

struct PreviewAdjustmentValues {
    int exposureTenthsEv = 0;
    int whiteBalance = 0;
    int tint = 0;
    int gammaHundredths = 220;
    int contrast = 0;
    int shadows = 0;
    int shadowRange = 100;
    int saturation = 0;
    int highlightCompression = 0;
};

struct PreviewWhiteBalanceEstimate {
    double whiteBalance = 0.0;
    double tint = 0.0;
};

namespace PreviewImageProcessing {

QImage applyDisplayAdjustments(const QImage &scaledSource,
                               const PreviewAdjustmentValues &adjustments);
std::optional<PreviewWhiteBalanceEstimate> estimateNeutralWhiteBalance(
    const QImage &source,
    const QRect &sampleRect);
void applyLumaSharpening8(QImage &image, int amount);
void applyLumaSharpening16(QImage &image, int amount);

}

#endif // PREVIEWIMAGEPROCESSING_H
