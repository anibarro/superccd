#include "PreviewColorNormalization.h"

#include <algorithm>
#include <cmath>

namespace superccd {

PreviewChannelGains derivePreviewChannelGains(double avgRed,
                                             double avgGreen,
                                             double avgBlue,
                                             bool hasAsShotNeutral,
                                             const std::array<double, 3> &asShotNeutral,
                                             double asShotTint)
{
    PreviewChannelGains gains;
    gains.red = avgRed > 0.0 ? avgGreen / avgRed : 1.0;
    gains.blue = avgBlue > 0.0 ? avgGreen / avgBlue : 1.0;

    if (hasAsShotNeutral &&
        asShotNeutral[0] > 0.0 &&
        asShotNeutral[1] > 0.0 &&
        asShotNeutral[2] > 0.0) {
        gains.red = asShotNeutral[1] / asShotNeutral[0];
        gains.green = 1.0 / (1.0 + asShotTint * 0.01);
        gains.blue = asShotNeutral[1] / asShotNeutral[2];
    }

    return gains;
}

double previewScaleToFit16Bit(double referenceLevel)
{
    return 65535.0 / std::max(1.0, referenceLevel);
}

double previewScaleToFit16Bit(double maxRed,
                              double maxGreen,
                              double maxBlue,
                              const PreviewChannelGains &gains)
{
    const double maxBalancedChannel = std::max(
        {1.0,
         maxRed * gains.red,
         maxGreen * gains.green,
         maxBlue * gains.blue});
    return previewScaleToFit16Bit(maxBalancedChannel);
}

double previewReferenceLevelFromHistogram(
    const std::vector<std::uint32_t> &histogram,
    std::uint64_t sampleCount,
    double percentile)
{
    if (histogram.empty() || sampleCount == 0) {
        return 1.0;
    }

    const double clampedPercentile = std::clamp(percentile, 0.0, 1.0);
    const std::uint64_t targetRank = std::max<std::uint64_t>(
        1,
        static_cast<std::uint64_t>(std::ceil(
            static_cast<double>(sampleCount) * clampedPercentile)));

    std::uint64_t cumulative = 0;
    for (std::size_t index = 0; index < histogram.size(); ++index) {
        cumulative += histogram[index];
        if (cumulative >= targetRank) {
            return static_cast<double>(index);
        }
    }

    return static_cast<double>(histogram.size() - 1);
}

} // namespace superccd
