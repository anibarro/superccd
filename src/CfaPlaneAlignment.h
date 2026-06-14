#ifndef CFAPLANEALIGNMENT_H
#define CFAPLANEALIGNMENT_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace superccd {

struct CfaPlaneAlignmentInfo {
    int offsetX = 0;
    int offsetY = 0;
    uint64_t comparedSamples = 0;
    double colorMatchRatio = 0.0;
    double correlation = 0.0;
    std::size_t extrapolatedSamples = 0;
    std::size_t droppedSourceSamples = 0;
    bool usedMetadataOffset = false;
};

bool alignSecondaryCfaToPrimary(
    const std::vector<uint16_t> &primary,
    const std::vector<uint8_t> &primaryChannels,
    int primaryWidth,
    int primaryHeight,
    int primaryFujiWidth,
    const std::vector<uint16_t> &secondary,
    const std::vector<uint8_t> &secondaryChannels,
    int secondaryWidth,
    int secondaryHeight,
    int secondaryFujiWidth,
    std::vector<uint16_t> &alignedSecondary,
    std::vector<uint8_t> &canonicalChannels,
    CfaPlaneAlignmentInfo &info,
    std::string &error);

} // namespace superccd

#endif // CFAPLANEALIGNMENT_H
