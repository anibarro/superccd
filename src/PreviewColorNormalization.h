#ifndef PREVIEWCOLORNORMALIZATION_H
#define PREVIEWCOLORNORMALIZATION_H

#include <array>

namespace superccd {

struct PreviewChannelGains {
    double red = 1.0;
    double green = 1.0;
    double blue = 1.0;
};

PreviewChannelGains derivePreviewChannelGains(double avgRed,
                                             double avgGreen,
                                             double avgBlue,
                                             bool hasAsShotNeutral,
                                             const std::array<double, 3> &asShotNeutral,
                                             double asShotTint);

double previewScaleToFit16Bit(double maxRed,
                              double maxGreen,
                              double maxBlue,
                              const PreviewChannelGains &gains);

} // namespace superccd

#endif // PREVIEWCOLORNORMALIZATION_H
