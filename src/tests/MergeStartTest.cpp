// Test that runs the exact same merge math as SuperCCDProcessor.cpp
// and prints statistics about the output for different start values.
// The merge function is copy-pasted verbatim from the processor so
// the test exercises the same code that runs in production.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <QString>
#include <QCoreApplication>

namespace {

// ---- BEGIN COPY of the merge function from SuperCCDProcessor.cpp ----
// This is a verbatim copy so the test exercises the same math that
// runs in production. If you change one, change both.

void mergePixels(const std::vector<uint16_t> &primary,
                  const std::vector<uint8_t> &primaryChannels,
                  const std::vector<uint16_t> &projectedSecondary,
                  int width, int height,
                  double rTransitionStart,
                  double rTransitionDelay,
                  double rTransitionSmoothness,
                  std::vector<uint16_t> &merged)
{
    merged.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
    const size_t pixelCount = primary.size();

    // gains assumed 1.0 for the synthetic test image
    double gains[4] = {1.0, 1.0, 1.0, 1.0};

    uint16_t maxPrimary[4] = {0, 0, 0, 0};
    for (size_t i = 0; i < pixelCount; ++i) {
        const uint8_t channel = primaryChannels[i];
        if (channel > 3) continue;
        if (primary[i] > maxPrimary[channel]) {
            maxPrimary[channel] = primary[i];
        }
    }

    for (size_t i = 0; i < pixelCount; ++i) {
        const uint8_t channel = primaryChannels[i];
        const uint16_t s = primary[i];
        if (channel > 3 || s == 0) continue;

        const uint16_t rProjected = projectedSecondary[i];
        if (rProjected == 0) {
            merged[i] = s;
            continue;
        }

        const double white = maxPrimary[channel] > 0
                                 ? static_cast<double>(maxPrimary[channel])
                                 : 1.0;
        const double scaledS = static_cast<double>(s);
        const double normalizedS = static_cast<double>(s) / white;
        const double scaledR = static_cast<double>(rProjected) * gains[channel];
        const double start = std::clamp(rTransitionStart, 0.0, 1.0);
        const double delayWidth = std::clamp(rTransitionDelay, 0.0, 1.0);
        const double smoothness = std::clamp(rTransitionSmoothness, 0.0, 1.0);
        double blendStart = 0.95;
        double blendEnd = 1.02;
        // !sDrivenHighlightsOnly branch
        constexpr double kMergeStartFullWidthCeiling = 0.9985;
        constexpr double kMaxWidth = 0.40;
        double blendScale = 1.0;
        if (start > kMergeStartFullWidthCeiling) {
            const double u = (start - kMergeStartFullWidthCeiling)
                             / (1.0 - kMergeStartFullWidthCeiling);
            const double smoothU = u * u * (3.0 - 2.0 * u);
            blendScale = 1.0 - smoothU;
        }
        if (blendScale < 1e-6) {
            blendStart = 1.0;
            blendEnd = 1.0;
        } else {
            const double width = kMaxWidth * delayWidth;
            blendStart = std::clamp(start, 0.0, 1.0);
            const double requestedEnd = blendStart + width;
            blendEnd = std::min(requestedEnd, 1.0);
            if (blendEnd - blendStart < 1e-6) {
                blendStart = 1.0;
                blendEnd = 1.0;
            }
        }

        if (normalizedS <= blendStart) {
            merged[i] = static_cast<uint16_t>(
                std::clamp<int>(static_cast<int>(scaledS + 0.5), 0, 65535));
            continue;
        }
        if (normalizedS >= blendEnd) {
            // Apply the fade-out scale to the R-only zone too
            const double rBlend = scaledR * blendScale + scaledS * (1.0 - blendScale);
            merged[i] = static_cast<uint16_t>(
                std::clamp<int>(static_cast<int>(rBlend + 0.5), 0, 65535));
            continue;
        }
        const double t = (normalizedS - blendStart) / (blendEnd - blendStart);
        const double smoothT = t * t * (3.0 - 2.0 * t);
        constexpr double kEaseInGamma = 2.0;
        const double baseBlendT = std::pow(std::max(t, 0.0),
                                         std::exp(kEaseInGamma * smoothness));
        const double blendT = baseBlendT * blendScale;
        const double mergedValue = (1.0 - blendT) * scaledS + blendT * scaledR;
        merged[i] = static_cast<uint16_t>(
            std::clamp<int>(static_cast<int>(mergedValue + 0.5), 0, 65535));
    }
}

// ---- END COPY ----

void buildSyntheticImage(int width, int height,
                         std::vector<uint16_t> &sPixels,
                         std::vector<uint8_t> &sChannels,
                         std::vector<uint16_t> &rProjected)
{
    sPixels.assign(static_cast<size_t>(width) * height, 0);
    sChannels.assign(sPixels.size(), 0);
    rProjected.assign(sPixels.size(), 0);

    auto channel = [](int x, int y) -> uint8_t {
        const int cx = x & 1;
        const int cy = y & 1;
        if (cx == 0 && cy == 0) return 0; // R
        if (cx == 1 && cy == 1) return 2; // B
        return 1;                          // G
    };

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t i = static_cast<size_t>(y) * width + x;
            const uint8_t ch = channel(x, y);
            sChannels[i] = ch;

            double sVal;
            if (y < height * 0.2) {
                sVal = 65535.0; // clipped
            } else {
                const double t = static_cast<double>(y - height * 0.2) / (height * 0.8);
                sVal = t * 65535.0;
            }
            sPixels[i] = static_cast<uint16_t>(std::min(65535.0, sVal));

            double rVal;
            if (y < height * 0.2) {
                rVal = 50000.0 + 5000.0 * std::sin(x * 0.1);
            } else {
                rVal = sVal * 0.9;
            }
            rProjected[i] = static_cast<uint16_t>(std::min(65535.0, rVal));
        }
    }
}

void printHistogram(const std::vector<uint16_t> &pixels, const char *label)
{
    std::vector<int> bins(10, 0);
    for (uint16_t p : pixels) {
        const int bin = std::min(9, static_cast<int>(p) * 10 / 65536);
        bins[bin]++;
    }
    std::printf("  %s histogram (10 bins, 0=dark, 9=clipped):\n", label);
    for (int i = 0; i < 10; ++i) {
        std::printf("    bin %d: %d\n", i, bins[i]);
    }
}

void printStats(const std::vector<uint16_t> &a,
                const std::vector<uint16_t> &b,
                const char *label,
                const std::vector<uint16_t> &sPixels,
                int width)
{
    if (a.size() != b.size()) {
        std::printf("  size mismatch: %zu vs %zu\n", a.size(), b.size());
        return;
    }
    int differing = 0;
    int64_t totalDiff = 0;
    int maxDiff = 0;
    int worstIdx = -1;
    int differingInClipped = 0;
    const int height = static_cast<int>(a.size() / width);
    const int clippedRows = height / 5;
    for (size_t i = 0; i < a.size(); ++i) {
        const int d = std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
        if (d > 0) {
            differing++;
            totalDiff += d;
            if (d > maxDiff) {
                maxDiff = d;
                worstIdx = static_cast<int>(i);
            }
            const int y = static_cast<int>(i / width);
            if (y < clippedRows) differingInClipped++;
        }
    }
    std::printf("  %s: %d / %zu differ (%.2f%%), %d in clipped top region, "
                "avg = %.1f, max = %d",
                label, differing, a.size(),
                100.0 * differing / a.size(),
                differingInClipped,
                differing > 0 ? static_cast<double>(totalDiff) / differing : 0.0,
                maxDiff);
    if (worstIdx >= 0) {
        std::printf(" (worst at y=%d x=%d, val %u -> %u, S=%u)",
                    worstIdx / width, worstIdx % width,
                    a[worstIdx], b[worstIdx], sPixels[worstIdx]);
    }
    std::printf("\n");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const int width = 128;
    const int height = 128;

    std::vector<uint16_t> sPixels;
    std::vector<uint8_t> sChannels;
    std::vector<uint16_t> rProjected;
    buildSyntheticImage(width, height, sPixels, sChannels, rProjected);

    std::printf("Synthetic image: %dx%d, top 20%% clipped to S=65535\n", width, height);
    printHistogram(sPixels, "S");
    printHistogram(rProjected, "R projected");

    // The slider-to-start mapping in MainWindow.cpp
    auto sliderToStart = [](int slider) -> double {
        if (slider <= 80) return slider * 0.9985 / 80;
        const double u = (slider - 80) / 20.0;
        const double smooth = u * u * (3.0 - 2.0 * u);
        return 0.9985 + 0.0015 * smooth;
    };

    struct TestCase {
        const char *label;
        int slider;
    };

    const TestCase cases[] = {
        {"slider 80",  80},
        {"slider 90",  90},
        {"slider 95",  95},
        {"slider 98",  98},
        {"slider 99",  99},
        {"slider 100", 100},
    };

    std::vector<std::vector<uint16_t>> merged(sizeof(cases) / sizeof(cases[0]));
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const double start = sliderToStart(cases[i].slider);
        mergePixels(sPixels, sChannels, rProjected,
                    width, height, start,
                    /*delay*/ 0.5, /*smoothness*/ 0.5, merged[i]);
    }

    std::printf("\nActual start values per slider:\n");
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        std::printf("  %s -> start = %.6f\n", cases[i].label,
                    sliderToStart(cases[i].slider));
    }

    std::printf("\n=== USER'S TEST: slider 99 vs 100 ===\n");
    printStats(merged[4], merged[5], "slider 99 vs 100", sPixels, width);

    std::printf("\n=== Adjacent slider comparisons ===\n");
    printStats(merged[3], merged[4], "slider 98 vs 99", sPixels, width);
    printStats(merged[2], merged[3], "slider 95 vs 98", sPixels, width);
    printStats(merged[1], merged[2], "slider 90 vs 95", sPixels, width);
    printStats(merged[0], merged[1], "slider 80 vs 90", sPixels, width);
    printStats(merged[0], merged[5], "slider 80 vs 100 (full range)", sPixels, width);

    std::printf("\nPixel values in clipped top region (y=0..3, x=64):\n");
    for (int y = 0; y < 4; ++y) {
        const size_t idx = static_cast<size_t>(y) * width + 64;
        std::printf("  y=%d: S=%5u R=%5u | "
                    "@80=%5u @90=%5u @95=%5u @98=%5u @99=%5u @100=%5u\n",
                    y, sPixels[idx], rProjected[idx],
                    merged[0][idx], merged[1][idx], merged[2][idx],
                    merged[3][idx], merged[4][idx], merged[5][idx]);
    }

    std::printf("\nJust below clipped region (y=30, x=64):\n");
    {
        const size_t idx = static_cast<size_t>(30) * width + 64;
        std::printf("  S=%5u R=%5u | "
                    "@80=%5u @90=%5u @95=%5u @98=%5u @99=%5u @100=%5u\n",
                    sPixels[idx], rProjected[idx],
                    merged[0][idx], merged[1][idx], merged[2][idx],
                    merged[3][idx], merged[4][idx], merged[5][idx]);
    }

    std::printf("\nIn clipped top region (y < %d), pixels that got R blended in:\n",
                height / 5);
    const int clippedRows = height / 5;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        int blended = 0;
        for (int y = 0; y < clippedRows; ++y) {
            for (int x = 0; x < width; ++x) {
                const size_t idx = static_cast<size_t>(y) * width + x;
                if (merged[i][idx] < sPixels[idx] - 1) {
                    blended++;
                }
            }
        }
        std::printf("  %s: %d / %d blended (%.1f%%)\n",
                    cases[i].label, blended, clippedRows * width,
                    100.0 * blended / (clippedRows * width));
    }

    return 0;
}
