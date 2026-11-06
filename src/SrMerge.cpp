#include "SrMerge.h"
#include "ParallelProcessing.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>

namespace superccd {
namespace {

constexpr uint8_t kOutsideDiamond = 0xff;

// Green repeats along the anti-diagonal every step, so the half-step to the S
// site is a 4-tap interpolation. Its negative lobes keep more detail than a
// plain average of the two nearest samples.
struct Tap {
    int dc;
    int dr;
    float weight;
};
constexpr std::array<Tap, 4> kGreenTaps = {{
    {0, 0, 9.0f / 16.0f},
    {-1, 1, 9.0f / 16.0f},
    {1, -1, -1.0f / 16.0f},
    {-2, 2, -1.0f / 16.0f},
}};

// Red and blue sit on a coarser lattice. The S site falls inside the square
// formed by these four same-color R samples, three quarters of the way to the
// first one on both axes, which gives these bilinear weights.
constexpr std::array<Tap, 4> kRedBlueTaps = {{
    {0, 0, 0.5625f},
    {-2, 0, 0.1875f},
    {0, 2, 0.1875f},
    {-2, 2, 0.0625f},
}};

template <typename Taps>
float interpolateTaps(const Taps &taps,
                      const std::vector<uint16_t> &cfa,
                      int width,
                      int height,
                      int column,
                      int row)
{
    float sum = 0.0f;
    float weightSum = 0.0f;
    float positiveSum = 0.0f;
    float positiveWeight = 0.0f;
    bool complete = true;
    for (const Tap &tap : taps) {
        const int c = column + tap.dc;
        const int r = row + tap.dr;
        const uint16_t sample =
            (c >= 0 && c < width && r >= 0 && r < height)
                ? cfa[static_cast<size_t>(r) * static_cast<size_t>(width) +
                      static_cast<size_t>(c)]
                : uint16_t{0};
        if (sample == 0) {
            complete = false;
            continue;
        }
        sum += tap.weight * static_cast<float>(sample);
        weightSum += tap.weight;
        if (tap.weight > 0.0f) {
            positiveSum += tap.weight * static_cast<float>(sample);
            positiveWeight += tap.weight;
        }
    }
    if (complete) {
        return std::max(sum / weightSum, 0.0f);
    }
    // Next to the diamond edge a partial negative lobe would overshoot.
    return positiveWeight > 0.0f ? positiveSum / positiveWeight : 0.0f;
}

struct LevelHistogram {
    std::array<std::vector<uint32_t>, 3> counts;

    LevelHistogram()
    {
        for (auto &channel : counts) {
            channel.assign(65536, 0);
        }
    }
};

// Sensors pile up at a hard ceiling when they clip. A real pile-up has many
// samples within a couple of ADU of the maximum; a few stray hot pixels do not.
double detectClipLevel(const std::vector<uint32_t> &histogram,
                       double fallback,
                       uint32_t minimumPileUp)
{
    int top = 65535;
    while (top > 0 && histogram[static_cast<size_t>(top)] == 0) {
        --top;
    }
    uint64_t pile = 0;
    for (int level = top; level >= std::max(top - 2, 0); --level) {
        pile += histogram[static_cast<size_t>(level)];
    }
    if (pile >= minimumPileUp) {
        return std::max(static_cast<double>(top) - 2.0, 1.0);
    }
    return fallback;
}

// The R diodes saturate softly and a little differently in each frame, so the
// ceiling has to be measured. Saturated areas pile up into a bump in the histogram,
// well above the falling tail of the real highlights. Each color is measured alone,
// because a large bright area in one channel can fill in the tail below the bump of
// another. Returns 0 when this color has no bump.
double detectRCeiling(const std::vector<uint32_t> &histogram, double nominal)
{
    constexpr int kBinSize = 64;
    constexpr int kBinCount = 65536 / kBinSize;
    constexpr int kBelow = 6; // Compare with the tail about 400 ADU lower
    constexpr uint64_t kMinimumCount = 200;

    std::array<uint64_t, kBinCount + 2> bins{};
    for (size_t level = 0; level < histogram.size(); ++level) {
        bins[level / kBinSize] += histogram[level];
    }
    int top = kBinCount - 1;
    while (top > 0 && bins[static_cast<size_t>(top)] == 0) {
        --top;
    }
    // Real ceilings measured so far stay above 85% of the nominal level. Anything
    // lower is a large bright area, such as a sky, and not saturation.
    const int lowest = std::max(static_cast<int>(0.85 * nominal / kBinSize), kBelow + 1);
    for (int bin = top; bin >= lowest; --bin) {
        const uint64_t count = bins[static_cast<size_t>(bin)];
        const uint64_t tail = bins[static_cast<size_t>(bin - kBelow)];
        if (count >= kMinimumCount &&
            count >= bins[static_cast<size_t>(bin - 1)] &&
            count >= bins[static_cast<size_t>(bin + 1)] &&
            static_cast<double>(count) > 1.5 * static_cast<double>(tail)) {
            return (bin + 0.5) * kBinSize;
        }
    }
    return 0.0;
}

struct BlockSample {
    double s;
    double r;
    double weight;
};

// Robust straight-line fit of S against R, using Huber reweighting so blocks
// with motion, flare or leftover clipping do not drag the gain.
void fitGain(const std::vector<BlockSample> &blocks,
             double nominalGain,
             double maximumIntercept,
             double &gain,
             double &intercept)
{
    gain = nominalGain;
    intercept = 0.0;
    if (blocks.size() < 6) {
        return;
    }

    double sumS = 0.0;
    double sumR = 0.0;
    double minR = blocks.front().r;
    double maxR = blocks.front().r;
    for (const BlockSample &block : blocks) {
        sumS += block.s * block.weight;
        sumR += block.r * block.weight;
        minR = std::min(minR, block.r);
        maxR = std::max(maxR, block.r);
    }
    if (sumR <= 0.0) {
        return;
    }
    gain = sumS / sumR;

    // An intercept is only identifiable when the blocks span a real range.
    const bool fitIntercept = blocks.size() >= 24 && maxR > minR * 1.5;
    std::vector<double> residualWeight(blocks.size(), 1.0);
    for (int iteration = 0; iteration < 6; ++iteration) {
        double sw = 0.0;
        double swr = 0.0;
        double sws = 0.0;
        double swrr = 0.0;
        double swrs = 0.0;
        for (size_t i = 0; i < blocks.size(); ++i) {
            const double w = blocks[i].weight * residualWeight[i];
            sw += w;
            swr += w * blocks[i].r;
            sws += w * blocks[i].s;
            swrr += w * blocks[i].r * blocks[i].r;
            swrs += w * blocks[i].r * blocks[i].s;
        }
        const double denominator = sw * swrr - swr * swr;
        if (fitIntercept && denominator > 1e-9) {
            gain = (sw * swrs - swr * sws) / denominator;
            intercept = (sws - gain * swr) / sw;
        } else if (swrr > 0.0) {
            gain = swrs / swrr;
            intercept = 0.0;
        }

        std::vector<double> residuals(blocks.size());
        for (size_t i = 0; i < blocks.size(); ++i) {
            residuals[i] = std::abs(blocks[i].s - (gain * blocks[i].r + intercept));
        }
        std::vector<double> sorted = residuals;
        std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
        const double scale = std::max(sorted[sorted.size() / 2] * 1.4826, 1.0);
        for (size_t i = 0; i < blocks.size(); ++i) {
            const double limit = 1.5 * scale;
            residualWeight[i] = residuals[i] <= limit ? 1.0 : limit / residuals[i];
        }
    }

    // Measured gains stay within about 20% of nominal across aperture, ISO and
    // both camera models. Anything further out means the fit latched onto
    // something other than the S/R relationship.
    if (gain < nominalGain * 0.75 || gain > nominalGain * 1.33 ||
        std::abs(intercept) > maximumIntercept) {
        double sumAll = 0.0;
        double sumRAll = 0.0;
        for (const BlockSample &block : blocks) {
            sumAll += block.s;
            sumRAll += block.r;
        }
        gain = sumRAll > 0.0 ? sumAll / sumRAll : nominalGain;
        intercept = 0.0;
        if (gain < nominalGain * 0.75 || gain > nominalGain * 1.33) {
            gain = nominalGain;
        }
    }
}

// Read noise of the R plane in R ADU. Analog gain raises it with ISO, though not
// fully in proportion.
double rReadNoise(double iso)
{
    return 7.0 * std::pow(std::max(iso, 100.0) / 100.0, 0.7);
}

} // namespace

double srBlendWeight(double normalizedS, const SrMergeParams &params)
{
    const double start = std::clamp(params.start, 0.0, 1.0);

    // In the last sliver of the start control the R contribution fades to zero,
    // so moving to 100 is a gradual change instead of switching R off at once.
    constexpr double kFullWidthCeiling = 0.9985;
    double fade = 1.0;
    if (start > kFullWidthCeiling) {
        const double u = (start - kFullWidthCeiling) / (1.0 - kFullWidthCeiling);
        fade = 1.0 - u * u * (3.0 - 2.0 * u);
    }
    if (fade < 1e-6) {
        return 0.0;
    }

    constexpr double kMaximumWidth = 0.40;
    const double end = std::min(
        start + kMaximumWidth * std::clamp(params.width, 0.0, 1.0), 1.0);
    if (end - start < 1e-6) {
        return normalizedS >= 1.0 ? fade : 0.0;
    }
    if (normalizedS <= start) {
        return 0.0;
    }
    if (normalizedS >= end) {
        return fade;
    }
    const double t = (normalizedS - start) / (end - start);
    constexpr double kEaseInGamma = 2.0;
    return fade * std::pow(
        t, std::exp(kEaseInGamma * std::clamp(params.smoothness, 0.0, 1.0)));
}

void projectRToSSites(const std::vector<uint16_t> &rCfa,
                      const std::vector<uint8_t> &channels,
                      int width,
                      int height,
                      std::vector<float> &projected)
{
    projected.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0.0f);
    parallel::forRows(height, 16, [&](int row, unsigned) {
        const size_t rowOffset = static_cast<size_t>(row) * static_cast<size_t>(width);
        for (int column = 0; column < width; ++column) {
            const size_t index = rowOffset + static_cast<size_t>(column);
            const uint8_t channel = channels[index];
            if (channel == kOutsideDiamond) {
                continue;
            }
            projected[index] = srColorClass(channel) == 1
                ? interpolateTaps(kGreenTaps, rCfa, width, height, column, row)
                : interpolateTaps(kRedBlueTaps, rCfa, width, height, column, row);
        }
    });
}

SrCalibration calibrateSr(const std::vector<uint16_t> &sCfa,
                          const std::vector<float> &projectedR,
                          const std::vector<uint8_t> &channels,
                          int width,
                          int height,
                          double nominalWhite,
                          double iso)
{
    SrCalibration calibration;

    const unsigned workers = parallel::availableWorkers();
    std::vector<LevelHistogram> sHistograms(workers);
    std::vector<LevelHistogram> rHistograms(workers);
    parallel::forRows(height, 16, [&](int row, unsigned worker) {
        const size_t rowOffset = static_cast<size_t>(row) * static_cast<size_t>(width);
        LevelHistogram &sHistogram = sHistograms[worker];
        LevelHistogram &rHistogram = rHistograms[worker];
        for (int column = 0; column < width; ++column) {
            const size_t index = rowOffset + static_cast<size_t>(column);
            if (channels[index] == kOutsideDiamond) {
                continue;
            }
            const int color = srColorClass(channels[index]);
            ++sHistogram.counts[static_cast<size_t>(color)][sCfa[index]];
            ++rHistogram.counts[static_cast<size_t>(color)][static_cast<size_t>(
                std::clamp(static_cast<int>(projectedR[index]), 0, 65535))];
        }
    });
    for (unsigned worker = 1; worker < workers; ++worker) {
        for (size_t color = 0; color < 3; ++color) {
            for (size_t level = 0; level < 65536; ++level) {
                sHistograms[0].counts[color][level] += sHistograms[worker].counts[color][level];
                rHistograms[0].counts[color][level] += rHistograms[worker].counts[color][level];
            }
        }
    }

    // R saturates softly near 5900 ADU at ISO 100. Analog gain raises that
    // proportionally until the ADC ceiling takes over at high ISO.
    const double isoScale = std::max(iso, 100.0) / 100.0;
    const double nominalRClip = std::min(5900.0 * isoScale, nominalWhite * 0.995);
    const double nominalSClip = nominalWhite * 0.9975;
    for (size_t color = 0; color < 3; ++color) {
        calibration.sClip[color] = detectClipLevel(
            sHistograms[0].counts[color], nominalSClip, 64);
    }
    // Colors with no bump of their own take the level of the ones that have one:
    // the diodes are alike, and a color that never got there will not be affected.
    double measured = 0.0;
    double ceilings[3];
    for (size_t color = 0; color < 3; ++color) {
        ceilings[color] = detectRCeiling(rHistograms[0].counts[color], nominalRClip);
        measured = std::max(measured, ceilings[color]);
    }
    for (size_t color = 0; color < 3; ++color) {
        calibration.rClip[color] = ceilings[color] > 0.0
            ? ceilings[color]
            : (measured > 0.0 ? measured : nominalRClip);
    }

    // Per-block means average out the 8x noise difference and let the fit see
    // the sensor response rather than texture. Blocks touching clipped S are
    // rejected, because a clipped sample would pull the ratio down.
    constexpr int kBlockSize = 16;
    const double minimumR = 4.0 * rReadNoise(iso);
    std::array<std::vector<BlockSample>, 3> blocks;
    std::mutex blockMutex;
    const int blockRows = height / kBlockSize;
    const int blockColumns = width / kBlockSize;
    parallel::forRows(blockRows, 4, [&](int blockRow, unsigned) {
        std::array<std::vector<BlockSample>, 3> local;
        for (int blockColumn = 0; blockColumn < blockColumns; ++blockColumn) {
            double sumS[3] = {0.0, 0.0, 0.0};
            double sumSS[3] = {0.0, 0.0, 0.0};
            double sumR[3] = {0.0, 0.0, 0.0};
            double maximumS[3] = {0.0, 0.0, 0.0};
            int count[3] = {0, 0, 0};
            bool complete = true;
            for (int y = 0; y < kBlockSize && complete; ++y) {
                const size_t rowOffset =
                    static_cast<size_t>(blockRow * kBlockSize + y) * static_cast<size_t>(width);
                for (int x = 0; x < kBlockSize; ++x) {
                    const size_t index =
                        rowOffset + static_cast<size_t>(blockColumn * kBlockSize + x);
                    if (channels[index] == kOutsideDiamond || projectedR[index] <= 0.0f) {
                        complete = false;
                        break;
                    }
                    const int color = srColorClass(channels[index]);
                    const double s = sCfa[index];
                    sumS[color] += s;
                    sumSS[color] += s * s;
                    sumR[color] += projectedR[index];
                    maximumS[color] = std::max(maximumS[color], s);
                    ++count[color];
                }
            }
            if (!complete) {
                continue;
            }
            for (size_t color = 0; color < 3; ++color) {
                if (count[color] == 0) {
                    continue;
                }
                const double meanS = sumS[color] / count[color];
                const double meanR = sumR[color] / count[color];
                const double variance = std::max(
                    sumSS[color] / count[color] - meanS * meanS, 0.0);
                const double clip = calibration.sClip[color];
                // R clips at zero, so near its noise floor the mean reads high.
                if (meanR < minimumR || meanS < 0.10 * clip || meanS > 0.85 * clip ||
                    maximumS[color] > 0.95 * clip ||
                    meanR >= calibration.rClip[color] * 0.9 ||
                    std::sqrt(variance) > 0.25 * meanS) {
                    continue;
                }
                local[color].push_back({meanS, meanR, 1.0});
            }
        }
        const std::lock_guard<std::mutex> lock(blockMutex);
        for (size_t color = 0; color < 3; ++color) {
            blocks[color].insert(blocks[color].end(), local[color].begin(), local[color].end());
        }
    });

    for (size_t color = 0; color < 3; ++color) {
        double gain = calibration.gain[color];
        double intercept = 0.0;
        fitGain(blocks[color], calibration.gain[color],
                0.02 * calibration.sClip[color], gain, intercept);
        calibration.gain[color] = gain;
        calibration.intercept[color] = intercept;
        calibration.fittedBlocks[color] = static_cast<int>(blocks[color].size());
    }
    return calibration;
}

void denoiseProjectedR(std::vector<float> &projectedR,
                       const std::vector<uint16_t> &sCfa,
                       const std::vector<uint8_t> &channels,
                       int width,
                       int height,
                       const SrCalibration &calibration,
                       const SrMergeParams &params,
                       double iso,
                       double strength)
{
    const double amount = std::clamp(strength, 0.0, 1.0);
    if (amount <= 0.0) {
        return;
    }

    // The range sigma follows the measured R noise at each sample's own level, so
    // the same setting holds detail equally well at every ISO. Differences a few
    // sigma above the noise count as detail and are kept.
    const double isoScale = std::max(iso, 100.0) / 100.0;
    const float readVariance = static_cast<float>(rReadNoise(iso) * rReadNoise(iso));
    const float shotVariance = static_cast<float>(2.0 * isoScale);
    const float sigmaScale = static_cast<float>(4.25 * amount);
    const float inverseSigmaScaleSquared = 1.0f / (2.0f * sigmaScale * sigmaScale);

    struct Offset {
        int dx;
        int dy;
        float spatialWeight;
    };
    // Stride 2 keeps every neighbor on the same color.
    std::array<Offset, 24> offsets;
    size_t offsetCount = 0;
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            offsets[offsetCount++] = {
                dx * 2,
                dy * 2,
                static_cast<float>(std::exp(-(dx * dx + dy * dy) / (2.0 * 1.5 * 1.5)))};
        }
    }

    const std::vector<float> source = projectedR;
    parallel::forRows(height, 16, [&](int y, unsigned) {
        const size_t rowOffset = static_cast<size_t>(y) * static_cast<size_t>(width);
        for (int x = 0; x < width; ++x) {
            const size_t index = rowOffset + static_cast<size_t>(x);
            const uint8_t channel = channels[index];
            if (channel == kOutsideDiamond || source[index] <= 0.0f) {
                continue;
            }
            const int color = srColorClass(channel);
            if (srBlendWeight(sCfa[index] / calibration.sClip[color], params) <= 0.0) {
                continue;
            }

            const float center = source[index];
            // Both samples of a difference carry noise, hence the factor of 2.
            const float inverseTwoSigmaSquared = inverseSigmaScaleSquared /
                (2.0f * (readVariance + shotVariance * center));
            float sum = center;
            float weightSum = 1.0f;
            for (const Offset &offset : offsets) {
                const int nx = x + offset.dx;
                const int ny = y + offset.dy;
                if (nx < 0 || nx >= width || ny < 0 || ny >= height) {
                    continue;
                }
                const size_t neighbor =
                    static_cast<size_t>(ny) * static_cast<size_t>(width) + static_cast<size_t>(nx);
                if (channels[neighbor] != channel || source[neighbor] <= 0.0f) {
                    continue;
                }
                const float difference = center - source[neighbor];
                const float weight = offset.spatialWeight *
                    std::exp(-difference * difference * inverseTwoSigmaSquared);
                sum += source[neighbor] * weight;
                weightSum += weight;
            }
            projectedR[index] = sum / weightSum;
        }
    });
}

SrMergeStats mergeSr(const std::vector<uint16_t> &sCfa,
                     const std::vector<float> &projectedR,
                     const std::vector<uint8_t> &channels,
                     int width,
                     int height,
                     const SrCalibration &calibration,
                     const SrMergeParams &params,
                     double nominalWhite,
                     std::vector<float> &merged)
{
    merged.resize(static_cast<size_t>(width) * static_cast<size_t>(height));

    // The blend weight only depends on color and S level, so each pow() is paid
    // once per ADU value instead of once per site.
    std::array<std::vector<float>, 3> blendTable;
    for (size_t color = 0; color < 3; ++color) {
        blendTable[color].resize(65536);
        const double clip = std::max(calibration.sClip[color], 1.0);
        for (size_t level = 0; level < 65536; ++level) {
            blendTable[color][level] = static_cast<float>(
                srBlendWeight(static_cast<double>(level) / clip, params));
        }
    }

    const float scale = static_cast<float>(1.0 / std::max(nominalWhite, 1.0));
    const float gains[3] = {
        static_cast<float>(calibration.gain[0]),
        static_cast<float>(calibration.gain[1]),
        static_cast<float>(calibration.gain[2])};
    const float intercepts[3] = {
        static_cast<float>(calibration.intercept[0]),
        static_cast<float>(calibration.intercept[1]),
        static_cast<float>(calibration.intercept[2])};

    SrMergeStats stats;
    std::mutex statsMutex;
    parallel::forRows(height, 16, [&](int row, unsigned) {
        const size_t rowOffset = static_cast<size_t>(row) * static_cast<size_t>(width);
        float localMaximum = 0.0f;
        size_t localBlended = 0;
        size_t localReplaced = 0;
        for (int column = 0; column < width; ++column) {
            const size_t index = rowOffset + static_cast<size_t>(column);
            const uint8_t channel = channels[index];
            if (channel == kOutsideDiamond) {
                merged[index] = 0.0f;
                continue;
            }
            const int color = srColorClass(channel);
            const uint16_t sLevel = sCfa[index];
            const float s = static_cast<float>(sLevel);
            const float weight = blendTable[static_cast<size_t>(color)][sLevel];
            float value = s;
            const float rRaw = projectedR[index];
            if (weight > 0.0f && rRaw > 0.0f) {
                const float r = std::max(rRaw * gains[color] + intercepts[color], 0.0f);
                value = s + (r - s) * weight;
                if (weight >= 1.0f) {
                    ++localReplaced;
                } else {
                    ++localBlended;
                }
            }
            value *= scale;
            merged[index] = value;
            localMaximum = std::max(localMaximum, value);
        }
        const std::lock_guard<std::mutex> lock(statsMutex);
        stats.maxValue = std::max(stats.maxValue, static_cast<double>(localMaximum));
        stats.blendedSites += localBlended;
        stats.replacedSites += localReplaced;
    });
    return stats;
}

} // namespace superccd
