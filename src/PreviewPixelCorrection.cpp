#include "PreviewPixelCorrection.h"
#include "ParallelProcessing.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <mutex>

namespace {

struct Correction {
    size_t index;
    float shift;
};

float luma(const float *pixel)
{
    return (pixel[0] + 2.0f * pixel[1] + pixel[2]) * 0.25f;
}

}

size_t PreviewPixelCorrection::suppressIsolatedLumaOutliers(std::vector<float> &rgb,
                                                            int width,
                                                            int height)
{
    if (width < 3 || height < 3) {
        return 0;
    }

    std::vector<Correction> corrections;
    std::mutex correctionsMutex;
    superccd::parallel::forRanges(1, static_cast<size_t>(height - 1), 32,
                                  [&](size_t begin, size_t end, unsigned) {
        std::vector<Correction> local;
        for (size_t y = begin; y < end; ++y) {
            for (int x = 1; x + 1 < width; ++x) {
                const size_t index = y * static_cast<size_t>(width) + static_cast<size_t>(x);
                const float center = luma(rgb.data() + index * 3);
                std::array<float, 8> neighbors{};
                size_t neighbor = 0;
                float neighborMinimum = std::numeric_limits<float>::max();
                float neighborMaximum = std::numeric_limits<float>::lowest();
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) {
                            continue;
                        }
                        const size_t other = (y + static_cast<size_t>(dy + 1) - 1) *
                            static_cast<size_t>(width) + static_cast<size_t>(x + dx);
                        const float value = luma(rgb.data() + other * 3);
                        neighbors[neighbor++] = value;
                        neighborMinimum = std::min(neighborMinimum, value);
                        neighborMaximum = std::max(neighborMaximum, value);
                    }
                }

                const bool isolatedDark = center < neighborMinimum;
                const bool isolatedLight = center > neighborMaximum;
                if (!isolatedDark && !isolatedLight) {
                    continue;
                }

                std::sort(neighbors.begin(), neighbors.end());
                const float median = (neighbors[3] + neighbors[4]) * 0.5f;
                std::array<float, 8> deviations{};
                for (size_t i = 0; i < neighbors.size(); ++i) {
                    deviations[i] = std::abs(neighbors[i] - median);
                }
                std::sort(deviations.begin(), deviations.end());
                const float medianDeviation = (deviations[3] + deviations[4]) * 0.5f;

                const float deviationThreshold = std::max({
                    0.012f,
                    median * 0.25f,
                    medianDeviation * 3.25f
                });
                if (std::abs(center - median) < deviationThreshold) {
                    continue;
                }

                const float neighborGap = isolatedDark
                    ? neighbors.front() - center
                    : center - neighbors.back();
                if (neighborGap < std::max(0.008f, median * 0.20f)) {
                    continue;
                }
                local.push_back({index, median - center});
            }
        }
        const std::lock_guard<std::mutex> lock(correctionsMutex);
        corrections.insert(corrections.end(), local.begin(), local.end());
    });

    for (const Correction &correction : corrections) {
        float *pixel = rgb.data() + correction.index * 3;
        pixel[0] = std::max(pixel[0] + correction.shift, 0.0f);
        pixel[1] = std::max(pixel[1] + correction.shift, 0.0f);
        pixel[2] = std::max(pixel[2] + correction.shift, 0.0f);
    }
    return corrections.size();
}
