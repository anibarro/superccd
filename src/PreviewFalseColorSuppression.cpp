#include "PreviewFalseColorSuppression.h"

#include "ParallelProcessing.h"

#include <QImage>
#include <QRgba64>

#include <algorithm>
#include <cmath>
#include <vector>

namespace superccd {

void suppressPreviewFalseColor(QImage &image)
{
    if (image.isNull() || image.width() < 2 || image.height() < 2) {
        return;
    }
    if (image.format() != QImage::Format_RGBX64) {
        image = image.convertToFormat(QImage::Format_RGBX64);
    }

    const int width = image.width();
    const int height = image.height();
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<float> luma(pixelCount);
    std::vector<float> chromaR(pixelCount);
    std::vector<float> chromaB(pixelCount);
    std::vector<float> smoothR(pixelCount);
    std::vector<float> smoothB(pixelCount);
    std::vector<float> texture(pixelCount);
    std::vector<float> residual(pixelCount);
    std::vector<float> mask(pixelCount);
    std::vector<float> temp(pixelCount);
    uchar *imageBits = image.bits();
    const qsizetype imageBytesPerLine = image.bytesPerLine();

    auto boxFilter = [&](const std::vector<float> &source,
                         std::vector<float> &destination,
                         int radius) {
        if (radius <= 0) {
            destination = source;
            return;
        }

        const int diameter = radius * 2 + 1;
        superccd::parallel::forRows(height, 8, [&](int y, unsigned) {
            const size_t rowOffset = static_cast<size_t>(y) * static_cast<size_t>(width);
            double sum = 0.0;
            for (int dx = -radius; dx <= radius; ++dx) {
                sum += source[rowOffset + static_cast<size_t>(std::clamp(dx, 0, width - 1))];
            }
            for (int x = 0; x < width; ++x) {
                temp[rowOffset + static_cast<size_t>(x)] =
                    static_cast<float>(sum / static_cast<double>(diameter));
                if (x + 1 >= width) {
                    continue;
                }

                sum += source[rowOffset + static_cast<size_t>(std::clamp(x + radius + 1, 0, width - 1))];
                sum -= source[rowOffset + static_cast<size_t>(std::clamp(x - radius, 0, width - 1))];
            }
        });

        superccd::parallel::forRanges(
            0,
            static_cast<size_t>(width),
            64,
            [&](size_t begin, size_t end, unsigned) {
                for (size_t x = begin; x < end; ++x) {
                    double sum = 0.0;
                    for (int dy = -radius; dy <= radius; ++dy) {
                        const int sourceY = std::clamp(dy, 0, height - 1);
                        sum += temp[static_cast<size_t>(sourceY) * static_cast<size_t>(width) + x];
                    }
                    for (int y = 0; y < height; ++y) {
                        destination[static_cast<size_t>(y) * static_cast<size_t>(width) + x] =
                            static_cast<float>(sum / static_cast<double>(diameter));
                        if (y + 1 >= height) {
                            continue;
                        }

                        const int enteringY = std::clamp(y + radius + 1, 0, height - 1);
                        const int leavingY = std::clamp(y - radius, 0, height - 1);
                        sum += temp[static_cast<size_t>(enteringY) * static_cast<size_t>(width) + x];
                        sum -= temp[static_cast<size_t>(leavingY) * static_cast<size_t>(width) + x];
                    }
                }
            });
    };

    superccd::parallel::forRows(height, 8, [&](int y, unsigned) {
        const QRgba64 *scanLine = reinterpret_cast<const QRgba64 *>(
            imageBits + static_cast<qsizetype>(y) * imageBytesPerLine);
        const size_t rowOffset = static_cast<size_t>(y) * static_cast<size_t>(width);
        for (int x = 0; x < width; ++x) {
            const QRgba64 pixel = scanLine[x];
            const float red = static_cast<float>(pixel.red()) / 65535.0f;
            const float green = static_cast<float>(pixel.green()) / 65535.0f;
            const float blue = static_cast<float>(pixel.blue()) / 65535.0f;
            const size_t idx = rowOffset + static_cast<size_t>(x);
            luma[idx] = (red + green * 2.0f + blue) * 0.25f;
            chromaR[idx] = red - green;
            chromaB[idx] = blue - green;
        }
    });

    boxFilter(chromaR, smoothR, 12);
    boxFilter(smoothR, residual, 12);
    smoothR.swap(residual);
    boxFilter(chromaB, smoothB, 12);
    boxFilter(smoothB, residual, 12);
    smoothB.swap(residual);

    superccd::parallel::forRows(height, 8, [&](int y, unsigned) {
        for (int x = 0; x < width; ++x) {
            const int left = std::max(0, x - 1);
            const int right = std::min(width - 1, x + 1);
            const int up = std::max(0, y - 1);
            const int down = std::min(height - 1, y + 1);
            const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
            const auto at = [&](int sx, int sy) -> float {
                return luma[static_cast<size_t>(sy) * static_cast<size_t>(width) + static_cast<size_t>(sx)];
            };
            const float gx = -at(left, up) - 2.0f * at(left, y) - at(left, down) +
                at(right, up) + 2.0f * at(right, y) + at(right, down);
            const float gy = -at(left, up) - 2.0f * at(x, up) - at(right, up) +
                at(left, down) + 2.0f * at(x, down) + at(right, down);
            texture[idx] = std::sqrt(gx * gx + gy * gy);

            const float dr = chromaR[idx] - smoothR[idx];
            const float db = chromaB[idx] - smoothB[idx];
            residual[idx] = std::sqrt(dr * dr + db * db);
        }
    });

    boxFilter(texture, mask, 7);
    texture.swap(mask);
    boxFilter(residual, mask, 7);
    residual.swap(mask);

    auto ramp = [](float value, float low, float high) -> float {
        const float t = std::clamp((value - low) / (high - low), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };

    superccd::parallel::forRows(height, 8, [&](int y, unsigned) {
        for (int x = 0; x < width; ++x) {
            const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
            const float textureMask = ramp(texture[idx], 0.22f, 0.47f);
            const float residualMask = ramp(residual[idx], 0.035f, 0.095f);
            const float exposureMask = ramp(luma[idx], 0.05f, 0.13f);
            mask[idx] = std::clamp(textureMask * residualMask * exposureMask * 1.6f, 0.0f, 1.0f);
        }
    });
    boxFilter(mask, residual, 3);
    mask.swap(residual);

    superccd::parallel::forRows(height, 8, [&](int y, unsigned) {
        QRgba64 *scanLine = reinterpret_cast<QRgba64 *>(
            imageBits + static_cast<qsizetype>(y) * imageBytesPerLine);
        for (int x = 0; x < width; ++x) {
            const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
            const float strength = std::clamp(mask[idx], 0.0f, 1.0f);
            if (strength <= 0.001f) {
                continue;
            }

            const float repairedR = chromaR[idx] + (smoothR[idx] - chromaR[idx]) * strength;
            const float repairedB = chromaB[idx] + (smoothB[idx] - chromaB[idx]) * strength;
            const float repairedG = luma[idx] - (repairedR + repairedB) * 0.25f;
            scanLine[x] = QRgba64::fromRgba64(
                static_cast<quint16>(std::clamp(static_cast<int>(std::lround((repairedG + repairedR) * 65535.0f)), 0, 65535)),
                static_cast<quint16>(std::clamp(static_cast<int>(std::lround(repairedG * 65535.0f)), 0, 65535)),
                static_cast<quint16>(std::clamp(static_cast<int>(std::lround((repairedG + repairedB) * 65535.0f)), 0, 65535)),
                65535);
        }
    });
}

}
