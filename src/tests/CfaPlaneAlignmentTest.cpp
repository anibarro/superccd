#include "CfaPlaneAlignment.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

uint8_t channelAt(const std::array<uint8_t, 4> &pattern, int x, int y)
{
    return pattern[((y & 1) << 1) | (x & 1)];
}

uint16_t testValue(int x, int y, int channel)
{
    uint32_t value =
        static_cast<uint32_t>((x + 17) * 73856093u) ^
        static_cast<uint32_t>((y + 31) * 19349663u) ^
        static_cast<uint32_t>((channel + 7) * 83492791u);
    return static_cast<uint16_t>(1000u + value % 50000u);
}

bool testFujiGeometryAlignment()
{
    const int primaryWidth = 8;
    const int primaryHeight = 7;
    const int secondaryWidth = 7;
    const int secondaryHeight = 6;
    const std::array<uint8_t, 4> primaryPattern = {3, 2, 0, 1};
    const std::array<uint8_t, 4> secondaryPattern = {0, 1, 3, 2};

    std::vector<uint16_t> primary(
        static_cast<size_t>(primaryWidth) * static_cast<size_t>(primaryHeight));
    std::vector<uint8_t> primaryChannels(primary.size());
    for (int y = 0; y < primaryHeight; ++y) {
        for (int x = 0; x < primaryWidth; ++x) {
            const size_t index =
                static_cast<size_t>(y) * static_cast<size_t>(primaryWidth) +
                static_cast<size_t>(x);
            const uint8_t channel = channelAt(primaryPattern, x, y);
            primaryChannels[index] = channel;
            primary[index] = testValue(x, y, channel);
        }
    }

    std::vector<uint16_t> secondary(
        static_cast<size_t>(secondaryWidth) * static_cast<size_t>(secondaryHeight));
    std::vector<uint8_t> secondaryChannels(secondary.size());
    for (int y = 0; y < secondaryHeight; ++y) {
        for (int x = 0; x < secondaryWidth; ++x) {
            const size_t index =
                static_cast<size_t>(y) * static_cast<size_t>(secondaryWidth) +
                static_cast<size_t>(x);
            const uint8_t channel = channelAt(secondaryPattern, x, y);
            secondaryChannels[index] = channel;
            secondary[index] =
                static_cast<uint16_t>(primary[
                    static_cast<size_t>(y + 1) *
                        static_cast<size_t>(primaryWidth) +
                    static_cast<size_t>(x)] /
                    2);
        }
    }

    std::vector<uint16_t> aligned;
    std::vector<uint8_t> canonicalChannels;
    superccd::CfaPlaneAlignmentInfo info;
    std::string error;
    if (!superccd::alignSecondaryCfaToPrimary(primary,
                                              primaryChannels,
                                              primaryWidth,
                                              primaryHeight,
                                              2144,
                                              secondary,
                                              secondaryChannels,
                                              secondaryWidth,
                                              secondaryHeight,
                                              2143,
                                              aligned,
                                              canonicalChannels,
                                              info,
                                              error)) {
        std::fprintf(stderr, "geometry alignment failed: %s\n", error.c_str());
        return false;
    }

    if (info.offsetX != 0 || info.offsetY != 1 ||
        !info.usedMetadataOffset || info.extrapolatedSamples == 0) {
        std::fprintf(stderr,
                     "unexpected geometry result: offset=(%d,%d) metadata=%d "
                     "extrapolated=%zu\n",
                     info.offsetX,
                     info.offsetY,
                     info.usedMetadataOffset ? 1 : 0,
                     info.extrapolatedSamples);
        return false;
    }

    for (int y = 0; y < secondaryHeight; ++y) {
        for (int x = 0; x < secondaryWidth; ++x) {
            const size_t sourceIndex =
                static_cast<size_t>(y) * static_cast<size_t>(secondaryWidth) +
                static_cast<size_t>(x);
            const size_t targetIndex =
                static_cast<size_t>(y + 1) * static_cast<size_t>(primaryWidth) +
                static_cast<size_t>(x);
            if (aligned[targetIndex] != secondary[sourceIndex]) {
                std::fprintf(stderr, "secondary sample was not translated correctly\n");
                return false;
            }
        }
    }

    for (int y = 0; y < primaryHeight; ++y) {
        for (int x = 0; x < primaryWidth; ++x) {
            const size_t index =
                static_cast<size_t>(y) * static_cast<size_t>(primaryWidth) +
                static_cast<size_t>(x);
            if (canonicalChannels[index] != channelAt(primaryPattern, x, y)) {
                std::fprintf(stderr, "canonical CFA phase changed\n");
                return false;
            }
        }
    }

    aligned.clear();
    canonicalChannels.clear();
    info = superccd::CfaPlaneAlignmentInfo();
    error.clear();
    if (!superccd::alignSecondaryCfaToPrimary(primary,
                                              primaryChannels,
                                              primaryWidth,
                                              primaryHeight,
                                              2144,
                                              secondary,
                                              secondaryChannels,
                                              secondaryWidth,
                                              secondaryHeight,
                                              2144,
                                              aligned,
                                              canonicalChannels,
                                              info,
                                              error)) {
        std::fprintf(stderr, "phase fallback failed: %s\n", error.c_str());
        return false;
    }
    if (info.offsetX != 0 || info.offsetY != 1 ||
        info.usedMetadataOffset) {
        std::fprintf(stderr,
                     "phase fallback chose offset=(%d,%d) metadata=%d\n",
                     info.offsetX,
                     info.offsetY,
                     info.usedMetadataOffset ? 1 : 0);
        return false;
    }
    return true;
}

bool testEqualGeometryIsUnchanged()
{
    const int width = 6;
    const int height = 6;
    const std::array<uint8_t, 4> pattern = {3, 2, 0, 1};
    std::vector<uint16_t> primary(
        static_cast<size_t>(width) * static_cast<size_t>(height));
    std::vector<uint16_t> secondary(primary.size());
    std::vector<uint8_t> channels(primary.size());

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t index =
                static_cast<size_t>(y) * static_cast<size_t>(width) +
                static_cast<size_t>(x);
            channels[index] = channelAt(pattern, x, y);
            primary[index] = testValue(x, y, channels[index]);
            secondary[index] = static_cast<uint16_t>(primary[index] / 3);
        }
    }

    std::vector<uint16_t> aligned;
    std::vector<uint8_t> canonicalChannels;
    superccd::CfaPlaneAlignmentInfo info;
    std::string error;
    if (!superccd::alignSecondaryCfaToPrimary(primary,
                                              channels,
                                              width,
                                              height,
                                              100,
                                              secondary,
                                              channels,
                                              width,
                                              height,
                                              100,
                                              aligned,
                                              canonicalChannels,
                                              info,
                                              error)) {
        std::fprintf(stderr, "equal alignment failed: %s\n", error.c_str());
        return false;
    }

    if (info.offsetX != 0 || info.offsetY != 0 ||
        !info.usedMetadataOffset || aligned != secondary) {
        std::fprintf(stderr, "equal geometry was modified\n");
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!testFujiGeometryAlignment() || !testEqualGeometryIsUnchanged()) {
        return 1;
    }
    return 0;
}
