#include "AmazeDemosaic.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

int main()
{
    constexpr int width = 128;
    constexpr int height = 128;
    std::vector<uint16_t> cfa(static_cast<size_t>(width) * static_cast<size_t>(height), 0);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int gradient = 2048 + x * 128 + y * 96;
            cfa[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] =
                static_cast<uint16_t>(std::clamp(gradient, 0, 60000));
        }
    }

    SuperCCDMetadata metadata;
    metadata.whiteLevel = 65535;

    std::vector<uint16_t> rgb;
    int rgbWidth = 0;
    int rgbHeight = 0;
    QString error;
    if (!demosaicSparseBayerAmaze(cfa, width, height, metadata, rgb, rgbWidth, rgbHeight, error)) {
        std::fprintf(stderr, "AMaZE demosaic failed: %s\n", error.toUtf8().constData());
        return 1;
    }

    if (rgbWidth <= 0 || rgbHeight <= 0 ||
        rgb.size() < static_cast<size_t>(rgbWidth) * static_cast<size_t>(rgbHeight) * 3) {
        std::fprintf(stderr, "AMaZE demosaic returned invalid dimensions or buffer size\n");
        return 1;
    }

    uint16_t maxValue = 0;
    for (uint16_t value : rgb) {
        maxValue = std::max(maxValue, value);
    }
    if (maxValue == 0) {
        std::fprintf(stderr, "AMaZE demosaic returned an all-black image\n");
        return 1;
    }

    return 0;
}
