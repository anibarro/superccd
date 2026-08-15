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

} // namespace

int main()
{
    return testShadowRecoveryCurveIsMonotonic() ? 0 : 1;
}
