#include "PreviewPixelCorrection.h"

#include <QRgba64>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace {

struct Correction {
    int x;
    int y;
    QRgba64 pixel;
};

float luma(const QRgba64 &pixel)
{
    return (static_cast<float>(pixel.red())
            + 2.0f * static_cast<float>(pixel.green())
            + static_cast<float>(pixel.blue())) * 0.25f;
}

}

size_t PreviewPixelCorrection::suppressIsolatedLumaOutliers(QImage &image)
{
    if (image.width() < 3 || image.height() < 3) {
        return 0;
    }
    if (image.format() != QImage::Format_RGBX64) {
        image = image.convertToFormat(QImage::Format_RGBX64);
    }

    std::vector<Correction> corrections;
    corrections.reserve(1024);
    for (int y = 1; y + 1 < image.height(); ++y) {
        const QRgba64 *centerLine =
            reinterpret_cast<const QRgba64 *>(image.constScanLine(y));
        for (int x = 1; x + 1 < image.width(); ++x) {
            const QRgba64 centerPixel = centerLine[x];
            const float center = luma(centerPixel);
            std::array<float, 8> neighbors{};
            size_t neighbor = 0;
            float neighborMinimum = std::numeric_limits<float>::max();
            float neighborMaximum = std::numeric_limits<float>::lowest();
            for (int dy = -1; dy <= 1; ++dy) {
                const QRgba64 *sourceLine =
                    reinterpret_cast<const QRgba64 *>(image.constScanLine(y + dy));
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) {
                        continue;
                    }
                    const float neighborLuma = luma(sourceLine[x + dx]);
                    neighbors[neighbor++] = neighborLuma;
                    neighborMinimum = std::min(neighborMinimum, neighborLuma);
                    neighborMaximum = std::max(neighborMaximum, neighborLuma);
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

            const float centerDeviation = std::abs(center - median);
            const float deviationThreshold = std::max({
                3084.0f,
                median * 0.25f,
                medianDeviation * 3.25f
            });
            if (centerDeviation < deviationThreshold) {
                continue;
            }

            const float neighborGap = isolatedDark
                ? neighbors.front() - center
                : center - neighbors.back();
            const float gapThreshold = std::max(2056.0f, median * 0.20f);
            if (neighborGap < gapThreshold) {
                continue;
            }

            const int shift = static_cast<int>(std::lround(median - center));
            corrections.push_back({
                x,
                y,
                QRgba64::fromRgba64(
                    static_cast<quint16>(std::clamp(
                        static_cast<int>(centerPixel.red()) + shift, 0, 65535)),
                    static_cast<quint16>(std::clamp(
                        static_cast<int>(centerPixel.green()) + shift, 0, 65535)),
                    static_cast<quint16>(std::clamp(
                        static_cast<int>(centerPixel.blue()) + shift, 0, 65535)),
                    65535)
            });
        }
    }

    for (const Correction &correction : corrections) {
        QRgba64 *outputLine =
            reinterpret_cast<QRgba64 *>(image.scanLine(correction.y));
        outputLine[correction.x] = correction.pixel;
    }

    return corrections.size();
}
