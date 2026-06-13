#ifndef PREVIEWIMAGEPROCESSING_H
#define PREVIEWIMAGEPROCESSING_H

#include <QImage>

struct PreviewAdjustmentValues {
    int exposureTenthsEv = 0;
    int whiteBalance = 0;
    int tint = 0;
    int gammaHundredths = 220;
    int contrast = 0;
    int saturation = 0;
    int highlightCompression = 0;
};

namespace PreviewImageProcessing {

QImage applyDisplayAdjustments(const QImage &scaledSource,
                               const PreviewAdjustmentValues &adjustments);
void applyLumaSharpening8(QImage &image, int amount);
void applyLumaSharpening16(QImage &image, int amount);

}

#endif // PREVIEWIMAGEPROCESSING_H
