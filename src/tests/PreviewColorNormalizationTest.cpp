#include "PreviewColorNormalization.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

bool nearlyEqual(double left, double right, double epsilon = 1e-9)
{
    return std::abs(left - right) <= epsilon;
}

bool testScaleKeepsColorRatiosStable()
{
    const superccd::PreviewChannelGains gains{1.0, 1.0, 1.0};
    const double scale = superccd::previewScaleToFit16Bit(2000.0, 1000.0, 500.0, gains);
    const double red = 2000.0 * gains.red * scale;
    const double green = 1000.0 * gains.green * scale;
    const double blue = 500.0 * gains.blue * scale;

    if (!nearlyEqual(red / green, 2.0) || !nearlyEqual(green / blue, 2.0)) {
        std::fprintf(stderr, "shared preview scaling changed the channel ratios\n");
        return false;
    }
    if (!nearlyEqual(red, 65535.0)) {
        std::fprintf(stderr, "shared preview scaling did not map the brightest balanced channel to full scale\n");
        return false;
    }
    return true;
}

bool testMetadataGainsOverrideAverageRatios()
{
    const std::array<double, 3> asShotNeutral = {0.5, 1.0, 2.0};
    const superccd::PreviewChannelGains gains =
        superccd::derivePreviewChannelGains(400.0,
                                            200.0,
                                            100.0,
                                            true,
                                            asShotNeutral,
                                            25.0);

    if (!nearlyEqual(gains.red, 2.0) ||
        !nearlyEqual(gains.green, 0.8) ||
        !nearlyEqual(gains.blue, 0.5)) {
        std::fprintf(stderr, "metadata-derived preview gains were not applied as expected\n");
        return false;
    }
    return true;
}

bool testReferenceIgnoresIsolatedHighlightOutlier()
{
    std::vector<std::uint32_t> histogram(65536, 0);
    histogram[4000] = 100000;
    histogram[32000] = 1;

    const double reference =
        superccd::previewReferenceLevelFromHistogram(histogram, 100001, 0.9995);
    if (!nearlyEqual(reference, 4000.0)) {
        std::fprintf(stderr, "isolated highlight outlier still changed preview reference\n");
        return false;
    }
    return true;
}

bool testReferenceKeepsBroadHighlights()
{
    std::vector<std::uint32_t> histogram(65536, 0);
    histogram[4000] = 95000;
    histogram[32000] = 5000;

    const double reference =
        superccd::previewReferenceLevelFromHistogram(histogram, 100000, 0.9995);
    if (!nearlyEqual(reference, 32000.0)) {
        std::fprintf(stderr, "broad highlights were ignored too aggressively\n");
        return false;
    }
    return true;
}

} // namespace

int main()
{
    return testScaleKeepsColorRatiosStable() &&
           testMetadataGainsOverrideAverageRatios() &&
           testReferenceIgnoresIsolatedHighlightOutlier() &&
           testReferenceKeepsBroadHighlights()
        ? 0
        : 1;
}
