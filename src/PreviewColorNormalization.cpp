#include "PreviewColorNormalization.h"

#include <algorithm>

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
    return 65535.0 / maxBalancedChannel;
}

} // namespace superccd
