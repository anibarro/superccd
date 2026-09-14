#include "PreviewImageProcessing.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

bool nearlyEqual(double left, double right, double epsilon = 1e-9)
{
    return std::abs(left - right) <= epsilon;
}

// Rebuilds the shadow-recovery curve by exercising the same LUT path
// used for 16-bit export, then checks that output never decreases as
// input increases. A non-monotonic curve causes the "negative shadow"
// bug reported when shadow range is low and shadow strength is high.
bool testShadowRecoveryCurveIsMonotonic()
{
    PreviewAdjustmentValues adjustments;
    adjustments.shadows = 100;
    adjustments.shadowRange = 40;

    std::vector<int> outputs;
    outputs.reserve(256);
    for (int i = 0; i <= 255; ++i) {
        // Same scaling used inside buildToneLut16 for 16-bit values.
        const double compressed = (static_cast<double>(i) / 257.0) * 255.0;
        const double linear = std::pow(std::clamp(compressed / 255.0, 0.0, 1.0), 1.0 / 2.2);
        // PreviewImageProcessing::applyShadowRecovery is not exported, so
        // exercise the public LUT path which is what the UI actually uses.
        outputs.push_back(i);
    }

    // Build a single-channel LUT so we can inspect the tone curve directly.
    QImage dummy(1, 1, QImage::Format_RGBX64);
    PreviewAdjustmentValues strongNarrow;
    strongNarrow.shadows = 100;
    strongNarrow.shadowRange = 40;

    PreviewAdjustmentValues weakNarrow;
    weakNarrow.shadows = 50;
    weakNarrow.shadowRange = 40;

    PreviewAdjustmentValues strongWide;
    strongWide.shadows = 100;
    strongWide.shadowRange = 100;

    auto sampleCurve = [](const PreviewAdjustmentValues &adj) {
        std::vector<int> result(256);
        for (int i = 0; i <= 255; ++i) {
            const double compressed = (static_cast<double>(i) / 257.0) * 255.0;
            const double linear = std::pow(std::clamp(compressed / 255.0, 0.0, 1.0), 1.0 / 2.2);
            // Use applyExportAdjustments16 on a two-pixel image and read the result.
            QImage src(2, 1, QImage::Format_RGBX64);
            src.setPixelColor(0, 0, QColor::fromRgbF(0.0, 0.0, 0.0));
            src.setPixelColor(1, 0, QColor::fromRgbF(linear, linear, linear));
            QImage out = PreviewImageProcessing::applyExportAdjustments16(src, adj);
            const QRgba64 px = out.pixelColor(1, 0).rgba64();
            result[i] = static_cast<int>(px.red());
        }
        return result;
    };

    auto checkMonotonic = [](const std::vector<int> &curve, const char *label) {
        for (size_t i = 1; i < curve.size(); ++i) {
            if (curve[i] < curve[i - 1] - 1) {
                std::fprintf(stderr,
                             "shadow recovery curve %s is not monotonic: "
                             "input %zu -> %d, input %zu -> %d\n",
                             label,
                             i - 1, curve[i - 1],
                             i, curve[i]);
                return false;
            }
        }
        return true;
    };

    const auto strongNarrowCurve = sampleCurve(strongNarrow);
    const auto weakNarrowCurve = sampleCurve(weakNarrow);
    const auto strongWideCurve = sampleCurve(strongWide);

    return checkMonotonic(strongNarrowCurve, "shadows=100,range=40")
        && checkMonotonic(weakNarrowCurve, "shadows=50,range=40")
        && checkMonotonic(strongWideCurve, "shadows=100,range=100");
}

bool testFlatCurveRetainsEdgeContrast()
{
    QImage source(101, 1, QImage::Format_RGBX64);
    for (int x = 0; x <= 100; ++x) {
        const double value = static_cast<double>(x) / 100.0;
        source.setPixelColor(x, 0, QColor::fromRgbF(value, value, value));
    }

    PreviewAdjustmentValues adjustments;
    adjustments.flatExposureEnabled = true;
    adjustments.flatBlackPoint = 0.0;
    adjustments.flatWhitePoint = 1.0;
    adjustments.contrast = 80;
    adjustments.flatContrastRange = 40;
    adjustments.flatContrastPivot = 50;
    adjustments.flatContrastTransition = 100;
    adjustments.gammaHundredths = 100;

    const QImage output =
        PreviewImageProcessing::applyExportAdjustments16(source, adjustments);
    quint16 previous = 0;
    for (int x = 0; x <= 100; ++x) {
        const quint16 value = output.pixelColor(x, 0).rgba64().red();
        if (value < previous) {
            return false;
        }
        previous = value;
    }

    const quint16 lowerEdgeStep = output.pixelColor(31, 0).rgba64().red()
        - output.pixelColor(29, 0).rgba64().red();
    const quint16 upperEdgeStep = output.pixelColor(71, 0).rgba64().red()
        - output.pixelColor(69, 0).rgba64().red();
    return lowerEdgeStep > 400 && upperEdgeStep > 400;
}

bool testFlatExposureChangesBrightness()
{
    QImage source(1, 1, QImage::Format_RGBX64);
    source.setPixelColor(0, 0, QColor::fromRgbF(0.5, 0.5, 0.5));

    PreviewAdjustmentValues base;
    base.flatExposureEnabled = true;
    base.flatBlackPoint = 0.0;
    base.flatWhitePoint = 1.0;
    base.gammaHundredths = 100;

    PreviewAdjustmentValues brighter = base;
    brighter.exposureTenths = 1000;

    const quint16 baseValue =
        PreviewImageProcessing::applyExportAdjustments16(source, base)
            .pixelColor(0, 0).rgba64().red();
    const quint16 brighterValue =
        PreviewImageProcessing::applyExportAdjustments16(source, brighter)
            .pixelColor(0, 0).rgba64().red();
    return brighterValue > baseValue;
}

bool testFlatContrastPreservesChroma()
{
    QImage source(1, 1, QImage::Format_RGBX64);
    source.setPixelColor(0, 0, QColor::fromRgbF(0.62, 0.42, 0.30));

    PreviewAdjustmentValues adjustments;
    adjustments.flatExposureEnabled = true;
    adjustments.flatBlackPoint = 0.0;
    adjustments.flatWhitePoint = 1.0;
    adjustments.contrast = 100;
    adjustments.flatContrastRange = 50;
    adjustments.flatContrastPivot = 63;
    adjustments.flatContrastTransition = 100;
    adjustments.gammaHundredths = 100;

    const QRgba64 output =
        PreviewImageProcessing::applyExportAdjustments16(source, adjustments)
            .pixelColor(0, 0).rgba64();
    const double redGreenRatio = static_cast<double>(output.red()) /
        std::max<double>(output.green(), 1.0);
    const double greenBlueRatio = static_cast<double>(output.green()) /
        std::max<double>(output.blue(), 1.0);
    return std::abs(redGreenRatio - 1.476) < 0.03
        && std::abs(greenBlueRatio - 1.4) < 0.03;
}

bool testFlatContrastExpandsLuminance()
{
    QImage source(2, 1, QImage::Format_RGBX64);
    source.setPixelColor(0, 0, QColor::fromRgbF(0.30, 0.30, 0.30));
    source.setPixelColor(1, 0, QColor::fromRgbF(0.70, 0.70, 0.70));

    PreviewAdjustmentValues base;
    base.flatExposureEnabled = true;
    base.flatBlackPoint = 0.0;
    base.flatWhitePoint = 1.0;
    base.flatContrastRange = 100;
    base.flatContrastPivot = 50;
    base.flatContrastTransition = 100;
    base.gammaHundredths = 100;

    PreviewAdjustmentValues contrasted = base;
    contrasted.contrast = 100;

    const QImage baseImage =
        PreviewImageProcessing::applyExportAdjustments16(source, base);
    const QImage contrastedImage =
        PreviewImageProcessing::applyExportAdjustments16(source, contrasted);
    const int baseSpan = baseImage.pixelColor(1, 0).rgba64().red()
        - baseImage.pixelColor(0, 0).rgba64().red();
    const int contrastedSpan = contrastedImage.pixelColor(1, 0).rgba64().red()
        - contrastedImage.pixelColor(0, 0).rgba64().red();
    return contrastedSpan > baseSpan;
}

bool testFlatTransitionDoesNotExtendRange()
{
    QImage source(2, 1, QImage::Format_RGBX64);
    source.setPixelColor(0, 0, QColor::fromRgbF(0.20, 0.20, 0.20));
    source.setPixelColor(1, 0, QColor::fromRgbF(0.80, 0.80, 0.80));

    PreviewAdjustmentValues sharp;
    sharp.flatExposureEnabled = true;
    sharp.flatBlackPoint = 0.0;
    sharp.flatWhitePoint = 1.0;
    sharp.contrast = 100;
    sharp.flatContrastRange = 40;
    sharp.flatContrastPivot = 50;
    sharp.flatContrastTransition = 0;
    sharp.gammaHundredths = 100;

    PreviewAdjustmentValues soft = sharp;
    soft.flatContrastTransition = 100;
    const QImage sharpImage =
        PreviewImageProcessing::applyExportAdjustments16(source, sharp);
    const QImage softImage =
        PreviewImageProcessing::applyExportAdjustments16(source, soft);
    return sharpImage.pixelColor(0, 0).rgba64().red()
            == softImage.pixelColor(0, 0).rgba64().red()
        && sharpImage.pixelColor(1, 0).rgba64().red()
            == softImage.pixelColor(1, 0).rgba64().red();
}

} // namespace

int main()
{
    const bool shadow = testShadowRecoveryCurveIsMonotonic();
    const bool edge = testFlatCurveRetainsEdgeContrast();
    const bool exposure = testFlatExposureChangesBrightness();
    const bool chroma = testFlatContrastPreservesChroma();
    const bool contrast = testFlatContrastExpandsLuminance();
    const bool transition = testFlatTransitionDoesNotExtendRange();
    return shadow && edge && exposure && chroma && contrast && transition ? 0 : 1;
}
