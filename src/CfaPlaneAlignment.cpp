#include "CfaPlaneAlignment.h"
#include "ParallelProcessing.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace superccd {
namespace {

constexpr uint8_t kUnknownChannel = 0xff;

uint8_t colorClass(uint8_t channel)
{
    return channel == 3 ? 1 : channel;
}

bool inferCfaPattern(const std::vector<uint8_t> &channels,
                     int width,
                     int height,
                     std::array<uint8_t, 4> &pattern)
{
    using PatternCounts = std::array<std::array<uint64_t, 4>, 4>;
    std::vector<PatternCounts> partialCounts(parallel::availableWorkers());
    parallel::forRows(height, 16, [&](int y, unsigned workerIndex) {
        PatternCounts &counts = partialCounts[workerIndex];
        for (int x = 0; x < width; ++x) {
            const size_t index =
                static_cast<size_t>(y) * static_cast<size_t>(width) +
                static_cast<size_t>(x);
            const uint8_t channel = channels[index];
            if (channel <= 3) {
                const int parity = ((y & 1) << 1) | (x & 1);
                counts[parity][channel]++;
            }
        }
    });

    PatternCounts counts{};
    for (const PatternCounts &partial : partialCounts) {
        for (int parity = 0; parity < 4; ++parity) {
            for (int channel = 0; channel < 4; ++channel) {
                counts[parity][channel] += partial[parity][channel];
            }
        }
    }

    for (int parity = 0; parity < 4; ++parity) {
        uint64_t bestCount = 0;
        uint8_t bestChannel = kUnknownChannel;
        for (uint8_t channel = 0; channel < 4; ++channel) {
            if (counts[parity][channel] > bestCount) {
                bestCount = counts[parity][channel];
                bestChannel = channel;
            }
        }
        if (bestChannel == kUnknownChannel) {
            return false;
        }
        pattern[parity] = bestChannel;
    }
    return true;
}

struct AlignmentScore {
    int offsetX = 0;
    int offsetY = 0;
    uint64_t comparedSamples = 0;
    uint64_t matchingSamples = 0;
    double colorMatchRatio = 0.0;
    double sourceCoverage = 0.0;
    double correlation = 0.0;
    bool valid = false;
};

struct AlignmentAccumulator {
    uint64_t comparedSamples = 0;
    uint64_t matchingSamples = 0;
    std::array<long double, 4> sumPrimary{};
    std::array<long double, 4> sumSecondary{};
    std::array<long double, 4> sumPrimarySquared{};
    std::array<long double, 4> sumSecondarySquared{};
    std::array<long double, 4> sumProduct{};
    std::array<uint64_t, 4> correlationCounts{};
};

AlignmentScore scoreAlignment(const std::vector<uint16_t> &primary,
                              const std::array<uint8_t, 4> &primaryPattern,
                              int primaryWidth,
                              int primaryHeight,
                              const std::vector<uint16_t> &secondary,
                              const std::vector<uint8_t> &secondaryChannels,
                              int secondaryWidth,
                              int secondaryHeight,
                              int offsetX,
                              int offsetY)
{
    AlignmentScore score;
    score.offsetX = offsetX;
    score.offsetY = offsetY;

    const int overlapLeft = std::max(0, offsetX);
    const int overlapTop = std::max(0, offsetY);
    const int overlapRight = std::min(primaryWidth, offsetX + secondaryWidth);
    const int overlapBottom = std::min(primaryHeight, offsetY + secondaryHeight);
    if (overlapLeft >= overlapRight || overlapTop >= overlapBottom) {
        return score;
    }

    const uint64_t overlapArea =
        static_cast<uint64_t>(overlapRight - overlapLeft) *
        static_cast<uint64_t>(overlapBottom - overlapTop);
    const uint64_t sourceArea =
        static_cast<uint64_t>(secondaryWidth) *
        static_cast<uint64_t>(secondaryHeight);
    score.sourceCoverage = sourceArea > 0
        ? static_cast<double>(overlapArea) / static_cast<double>(sourceArea)
        : 0.0;

    std::vector<AlignmentAccumulator> partials(parallel::availableWorkers());
    parallel::forRows(secondaryHeight, 16, [&](int sourceY, unsigned workerIndex) {
        AlignmentAccumulator &partial = partials[workerIndex];
        const int targetY = sourceY + offsetY;
        if (targetY < 0 || targetY >= primaryHeight) {
            return;
        }
        for (int sourceX = 0; sourceX < secondaryWidth; ++sourceX) {
            const int targetX = sourceX + offsetX;
            if (targetX < 0 || targetX >= primaryWidth) {
                continue;
            }

            const size_t sourceIndex =
                static_cast<size_t>(sourceY) * static_cast<size_t>(secondaryWidth) +
                static_cast<size_t>(sourceX);
            const uint16_t secondaryValue = secondary[sourceIndex];
            const uint8_t secondaryChannel = secondaryChannels[sourceIndex];
            if (secondaryValue == 0 || secondaryChannel > 3) {
                continue;
            }

            const int targetParity = ((targetY & 1) << 1) | (targetX & 1);
            const uint8_t targetChannel = primaryPattern[targetParity];
            partial.comparedSamples++;
            if (colorClass(secondaryChannel) != colorClass(targetChannel)) {
                continue;
            }
            partial.matchingSamples++;

            const size_t targetIndex =
                static_cast<size_t>(targetY) * static_cast<size_t>(primaryWidth) +
                static_cast<size_t>(targetX);
            const uint16_t primaryValue = primary[targetIndex];
            if (primaryValue == 0) {
                continue;
            }

            const int channel = targetChannel;
            const long double primarySample = primaryValue;
            const long double secondarySample = secondaryValue;
            partial.sumPrimary[channel] += primarySample;
            partial.sumSecondary[channel] += secondarySample;
            partial.sumPrimarySquared[channel] += primarySample * primarySample;
            partial.sumSecondarySquared[channel] += secondarySample * secondarySample;
            partial.sumProduct[channel] += primarySample * secondarySample;
            partial.correlationCounts[channel]++;
        }
    });

    AlignmentAccumulator totals;
    for (const AlignmentAccumulator &partial : partials) {
        totals.comparedSamples += partial.comparedSamples;
        totals.matchingSamples += partial.matchingSamples;
        for (int channel = 0; channel < 4; ++channel) {
            totals.sumPrimary[channel] += partial.sumPrimary[channel];
            totals.sumSecondary[channel] += partial.sumSecondary[channel];
            totals.sumPrimarySquared[channel] += partial.sumPrimarySquared[channel];
            totals.sumSecondarySquared[channel] += partial.sumSecondarySquared[channel];
            totals.sumProduct[channel] += partial.sumProduct[channel];
            totals.correlationCounts[channel] += partial.correlationCounts[channel];
        }
    }
    score.comparedSamples = totals.comparedSamples;
    score.matchingSamples = totals.matchingSamples;

    if (score.comparedSamples > 0) {
        score.colorMatchRatio =
            static_cast<double>(score.matchingSamples) /
            static_cast<double>(score.comparedSamples);
    }

    long double weightedCorrelation = 0.0;
    uint64_t correlationWeight = 0;
    for (int channel = 0; channel < 4; ++channel) {
        const uint64_t count = totals.correlationCounts[channel];
        if (count < 2) {
            continue;
        }
        const long double sampleCount = static_cast<long double>(count);
        const long double covariance =
            totals.sumProduct[channel] / sampleCount -
            (totals.sumPrimary[channel] / sampleCount) *
                (totals.sumSecondary[channel] / sampleCount);
        const long double primaryVariance =
            totals.sumPrimarySquared[channel] / sampleCount -
            std::pow(totals.sumPrimary[channel] / sampleCount, 2);
        const long double secondaryVariance =
            totals.sumSecondarySquared[channel] / sampleCount -
            std::pow(totals.sumSecondary[channel] / sampleCount, 2);
        if (primaryVariance <= 0.0 || secondaryVariance <= 0.0) {
            continue;
        }
        const long double channelCorrelation =
            covariance / std::sqrt(primaryVariance * secondaryVariance);
        weightedCorrelation += channelCorrelation * sampleCount;
        correlationWeight += count;
    }
    if (correlationWeight > 0) {
        score.correlation =
            static_cast<double>(weightedCorrelation /
                                static_cast<long double>(correlationWeight));
    }

    score.valid =
        score.sourceCoverage >= 0.995 &&
        score.comparedSamples >= 8 &&
        score.colorMatchRatio >= 0.999;
    return score;
}

bool betterFallbackScore(const AlignmentScore &candidate,
                         const AlignmentScore &current,
                         int preferredOffsetX,
                         int preferredOffsetY)
{
    if (!current.valid) {
        return true;
    }
    if (std::abs(candidate.correlation - current.correlation) > 1e-6) {
        return candidate.correlation > current.correlation;
    }
    if (std::abs(candidate.sourceCoverage - current.sourceCoverage) > 1e-9) {
        return candidate.sourceCoverage > current.sourceCoverage;
    }
    const int candidateMetadataDistance =
        std::abs(candidate.offsetX - preferredOffsetX) +
        std::abs(candidate.offsetY - preferredOffsetY);
    const int currentMetadataDistance =
        std::abs(current.offsetX - preferredOffsetX) +
        std::abs(current.offsetY - preferredOffsetY);
    if (candidateMetadataDistance != currentMetadataDistance) {
        return candidateMetadataDistance < currentMetadataDistance;
    }
    return std::abs(candidate.offsetX) + std::abs(candidate.offsetY) <
           std::abs(current.offsetX) + std::abs(current.offsetY);
}

} // namespace

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
    std::string &error)
{
    info = CfaPlaneAlignmentInfo();
    error.clear();

    if (primaryWidth <= 0 || primaryHeight <= 0 ||
        secondaryWidth <= 0 || secondaryHeight <= 0) {
        error = "Invalid CFA plane dimensions.";
        return false;
    }

    const size_t primaryPixelCount =
        static_cast<size_t>(primaryWidth) * static_cast<size_t>(primaryHeight);
    const size_t secondaryPixelCount =
        static_cast<size_t>(secondaryWidth) * static_cast<size_t>(secondaryHeight);
    if (primary.size() < primaryPixelCount ||
        primaryChannels.size() < primaryPixelCount ||
        secondary.size() < secondaryPixelCount ||
        secondaryChannels.size() < secondaryPixelCount) {
        error = "CFA plane buffers are smaller than their declared dimensions.";
        return false;
    }

    std::array<uint8_t, 4> primaryPattern{};
    if (!inferCfaPattern(primaryChannels,
                         primaryWidth,
                         primaryHeight,
                         primaryPattern)) {
        error = "Unable to infer the primary CFA pattern.";
        return false;
    }

    const int preferredOffsetX = 0;
    // LibRaw's rotated Fuji canvas origin moves with fuji_width.
    const int preferredOffsetY =
        primaryFujiWidth > 0 && secondaryFujiWidth > 0
            ? primaryFujiWidth - secondaryFujiWidth
            : 0;

    AlignmentScore selected = scoreAlignment(primary,
                                             primaryPattern,
                                             primaryWidth,
                                             primaryHeight,
                                             secondary,
                                             secondaryChannels,
                                             secondaryWidth,
                                             secondaryHeight,
                                             preferredOffsetX,
                                             preferredOffsetY);
    bool usedMetadataOffset = selected.valid;

    if (!selected.valid) {
        AlignmentScore bestFallback;
        const int searchRadiusX =
            std::min(6, std::abs(primaryWidth - secondaryWidth) + 3);
        const int searchRadiusY =
            std::min(6, std::abs(primaryHeight - secondaryHeight) + 3);
        for (int offsetY = -searchRadiusY; offsetY <= searchRadiusY; ++offsetY) {
            for (int offsetX = -searchRadiusX; offsetX <= searchRadiusX; ++offsetX) {
                if (offsetX == preferredOffsetX &&
                    offsetY == preferredOffsetY) {
                    continue;
                }
                const AlignmentScore candidate =
                    scoreAlignment(primary,
                                   primaryPattern,
                                   primaryWidth,
                                   primaryHeight,
                                   secondary,
                                   secondaryChannels,
                                   secondaryWidth,
                                   secondaryHeight,
                                   offsetX,
                                   offsetY);
                if (candidate.valid &&
                    betterFallbackScore(candidate,
                                        bestFallback,
                                        preferredOffsetX,
                                        preferredOffsetY)) {
                    bestFallback = candidate;
                }
            }
        }
        if (!bestFallback.valid) {
            error =
                "Unable to align the secondary plane to the primary CFA phase.";
            return false;
        }
        selected = bestFallback;
        usedMetadataOffset = false;
    }

    alignedSecondary.assign(primaryPixelCount, 0);
    canonicalChannels.resize(primaryPixelCount);
    std::vector<uint8_t> covered(primaryPixelCount, 0);
    parallel::forRows(primaryHeight, 16, [&](int y, unsigned) {
        for (int x = 0; x < primaryWidth; ++x) {
            const size_t index =
                static_cast<size_t>(y) * static_cast<size_t>(primaryWidth) +
                static_cast<size_t>(x);
            canonicalChannels[index] =
                primaryPattern[((y & 1) << 1) | (x & 1)];
        }
    });

    std::vector<size_t> droppedCounts(parallel::availableWorkers(), 0);
    parallel::forRows(secondaryHeight, 16, [&](int sourceY, unsigned workerIndex) {
        const int targetY = sourceY + selected.offsetY;
        for (int sourceX = 0; sourceX < secondaryWidth; ++sourceX) {
            const int targetX = sourceX + selected.offsetX;
            const size_t sourceIndex =
                static_cast<size_t>(sourceY) * static_cast<size_t>(secondaryWidth) +
                static_cast<size_t>(sourceX);
            const uint16_t value = secondary[sourceIndex];
            if (targetX < 0 || targetX >= primaryWidth ||
                targetY < 0 || targetY >= primaryHeight) {
                if (value != 0) {
                    droppedCounts[workerIndex]++;
                }
                continue;
            }

            const size_t targetIndex =
                static_cast<size_t>(targetY) * static_cast<size_t>(primaryWidth) +
                static_cast<size_t>(targetX);
            covered[targetIndex] = 1;
            const uint8_t sourceChannel = secondaryChannels[sourceIndex];
            if (value == 0 || sourceChannel > 3 ||
                colorClass(sourceChannel) !=
                    colorClass(canonicalChannels[targetIndex])) {
                continue;
            }
            alignedSecondary[targetIndex] = value;
        }
    });
    size_t droppedSourceSamples = 0;
    for (size_t count : droppedCounts) {
        droppedSourceSamples += count;
    }

    // Fill only the border not covered by the translated source, using the
    // same primary CFA phase so the exported GBRG pattern stays unchanged.
    std::vector<size_t> extrapolatedCounts(parallel::availableWorkers(), 0);
    parallel::forRows(primaryHeight, 16, [&](int y, unsigned workerIndex) {
        for (int x = 0; x < primaryWidth; ++x) {
            const size_t index =
                static_cast<size_t>(y) * static_cast<size_t>(primaryWidth) +
                static_cast<size_t>(x);
            if (covered[index] || primary[index] == 0) {
                continue;
            }

            uint64_t sum = 0;
            int count = 0;
            for (int radius = 1; radius <= 4 && count == 0; ++radius) {
                for (int dy = -radius; dy <= radius; ++dy) {
                    for (int dx = -radius; dx <= radius; ++dx) {
                        if (std::max(std::abs(dx), std::abs(dy)) != radius) {
                            continue;
                        }
                        const int neighborX = x + dx;
                        const int neighborY = y + dy;
                        if (neighborX < 0 || neighborX >= primaryWidth ||
                            neighborY < 0 || neighborY >= primaryHeight) {
                            continue;
                        }
                        const size_t neighborIndex =
                            static_cast<size_t>(neighborY) *
                                static_cast<size_t>(primaryWidth) +
                            static_cast<size_t>(neighborX);
                        if (!covered[neighborIndex] ||
                            alignedSecondary[neighborIndex] == 0 ||
                            canonicalChannels[neighborIndex] !=
                                canonicalChannels[index]) {
                            continue;
                        }
                        sum += alignedSecondary[neighborIndex];
                        count++;
                    }
                }
            }
            if (count > 0) {
                alignedSecondary[index] =
                    static_cast<uint16_t>((sum + static_cast<uint64_t>(count / 2)) /
                                          static_cast<uint64_t>(count));
                extrapolatedCounts[workerIndex]++;
            }
        }
    });
    size_t extrapolatedSamples = 0;
    for (size_t count : extrapolatedCounts) {
        extrapolatedSamples += count;
    }

    info.offsetX = selected.offsetX;
    info.offsetY = selected.offsetY;
    info.comparedSamples = selected.comparedSamples;
    info.colorMatchRatio = selected.colorMatchRatio;
    info.correlation = selected.correlation;
    info.extrapolatedSamples = extrapolatedSamples;
    info.droppedSourceSamples = droppedSourceSamples;
    info.usedMetadataOffset = usedMetadataOffset;
    return true;
}

} // namespace superccd
