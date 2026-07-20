#include "PreviewImageProcessing.h"
#include "ParallelProcessing.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {
constexpr int kToneLutMaxInput = 8192;

double shadowRangeMask(double linear, double shadowRange)
{
    // The shadow range slider controls how far up the tonal range the shadow
    // recovery effect reaches.
    //
    // shadowRange = 1.0 (max): effect spans the full tonal range (0..0.60),
    //                          gentle falloff — shadows blend into midtones.
    // shadowRange = 0.0 (min): effect is confined to the deepest shadows
    //                          (0..0.10) with a smooth cutoff.
    //
    // The mask uses a smoothstep falloff that scales with the pivot point,
    // ensuring a natural transition at all range values.

    constexpr double kMinimumPivot = 0.10;
    constexpr double kMaximumPivot = 0.60;

    const double pivot =
        kMinimumPivot + (kMaximumPivot - kMinimumPivot) * shadowRange;

    const double x = std::clamp(linear, 0.0, 1.0);

    // Transition width is proportional to the pivot, so low range values
    // have narrow transitions and high range values have wide transitions.
    const double fadeEnd = pivot + pivot * 1.5;

    if (x <= pivot) {
        return 1.0;
    } else if (x >= fadeEnd) {
        return 0.0;
    } else {
        const double t = (x - pivot) / (fadeEnd - pivot);
        // Smoothstep: 1 at t=0, 0 at t=1
        return (1.0 - t) * (1.0 - t) * (1.0 + 2.0 * t);
    }
}

double applyShadowRecovery(double linear, double shadowRecovery, double shadowRange)
{
    if (shadowRecovery <= 0.0) {
        return linear;
    }

    const double clamped = std::clamp(linear, 0.0, 1.0);
    const double exponent = 1.0 + shadowRecovery * 3.0;
    const double lifted = 1.0 - std::pow(1.0 - clamped, exponent);
    return clamped + (lifted - clamped) * shadowRangeMask(clamped, shadowRange);
}

// Applies a tone-mapping curve designed to preserve midtone contrast.
//
// * amount  (0..1): overall strength of the tone mapping effect.
//           0 = identity; 1 = maximum shadow lift + highlight compression.
// * bias   (-1..1): shifts the curve's pivot.
//           Negative values lean toward shadow preservation (compress
//           highlights more, lift shadows less).
//           Positive values lean toward highlight preservation (lift
//           shadows more, compress highlights less).
//
// The curve works in two stages:
//   1. A power-curve tone map with separate exponents for shadows and
//      highlights (split at a bias-dependent pivot). Shadows are lifted
//      and highlights are compressed.
//   2. A midtone contrast correction that applies a gentle S-curve
//      around the pivot to compensate for the slope reduction introduced
//      by stage 1, keeping the image looking punchy rather than flat.
double applyToneBalance(double linear, double amount, double bias)
{
    if (amount <= 0.0) {
        return linear;
    }

    const double x = std::clamp(linear, 0.0, 1.0);

    // Pivot point: where the shadow region ends and the highlight region
    // begins.  bias < 0 moves the pivot lower (more of the image is in
    // the highlight region → more highlight compression).  bias > 0 moves
    // it higher (more of the image is in the shadow region → more shadow
    // lift).
    const double pivot = std::clamp(0.5 + bias * 0.25, 0.2, 0.8);

    // Shadow exponent (> 1 lifts dark values).
    const double shadowStr = amount * (1.0 + std::max(bias, 0.0)) * 0.45;
    const double sExp = 1.0 / std::max(1.0 - shadowStr, 0.3);

    // Highlight exponent (> 1 compresses bright values).
    const double highlightStr = amount * (1.0 + std::max(-bias, 0.0)) * 0.45;
    const double hExp = 1.0 + highlightStr * 2.5;

    double y;
    if (x <= pivot) {
        const double t = x / std::max(pivot, 0.001);
        y = std::pow(t, sExp) * pivot;
    } else {
        const double t = (x - pivot) / std::max(1.0 - pivot, 0.001);
        y = pivot + (1.0 - std::pow(1.0 - t, hExp)) * (1.0 - pivot);
    }

    // Midtone contrast correction.  At the pivot the tone curve slope is
    // reduced by the shadow lift / highlight compression.  We compensate
    // with a sigmoid-shaped local contrast boost centred on the pivot.
    //
    // Estimate the actual slope at the pivot from the shadow side:
    const double slopeAtPivot = std::pow(1.0, sExp - 1.0) * sExp;
    const double contrastCompensation = std::clamp(
        1.0 / std::max(slopeAtPivot, 0.4), 1.0, 1.8);
    const double cc = (contrastCompensation - 1.0) * amount;

    if (cc > 0.0) {
        const double d = y - pivot;
        const double mask = std::exp(-d * d / 0.08);
        y += d * cc * mask;
    }

    return std::clamp(y, 0.0, 1.0);
}

template <typename FillLuma, typename ApplyPixel>
void applyLumaSharpening(int width,
                         int height,
                         int amount,
                         FillLuma fillLuma,
                         ApplyPixel applyPixel)
{
    if (amount <= 0 || width < 2 || height < 2) {
        return;
    }

    const double normalizedAmount = static_cast<double>(amount) / 100.0;
    // Preserve the feel of lower settings while giving the top of the slider
    // more headroom: 100% now reaches roughly 2x the previous effect.
    const double strength =
        normalizedAmount * (1.0 + normalizedAmount * normalizedAmount);
    std::vector<int> previous(static_cast<size_t>(width));
    std::vector<int> current(static_cast<size_t>(width));
    std::vector<int> next(static_cast<size_t>(width));

    fillLuma(0, previous);
    fillLuma(0, current);
    fillLuma(std::min(1, height - 1), next);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int left = current[static_cast<size_t>(std::max(0, x - 1))];
            const int right = current[static_cast<size_t>(std::min(width - 1, x + 1))];
            const int blurredLuma =
                (previous[static_cast<size_t>(x)] + next[static_cast<size_t>(x)]
                 + left + right) / 4;
            const double detail = static_cast<double>(
                current[static_cast<size_t>(x)] - blurredLuma) * strength;
            applyPixel(x, y, detail);
        }

        previous.swap(current);
        current.swap(next);
        fillLuma(std::min(height - 1, y + 2), next);
    }
}

std::vector<quint16> buildToneLut16(const PreviewAdjustmentValues &adjustments,
                                    double channelScale)
{
    const double exposureScale = std::pow(2.0, adjustments.exposureTenthsEv / 10.0);
    constexpr double kOriginalGamma = 2.2;
    const double newGamma = std::max(adjustments.gammaHundredths / 100.0, 0.01);
    const double contrastScale = 1.0 + adjustments.contrast / 100.0;
    const double shadowRecovery = adjustments.shadows / 100.0;
    const double shadowRange = adjustments.shadowRange / 100.0;
    const double invOriginalGamma = 1.0 / kOriginalGamma;
    const double invNewGamma = 1.0 / newGamma;
    const double highlightCompression = adjustments.highlightCompression / 100.0;
    const double compressionStart = 200.0 - highlightCompression * 152.0;
    const double compressionStrength = highlightCompression * 2.0
        + highlightCompression * highlightCompression * 14.0;
    const double toneBalanceAmount = adjustments.toneBalance / 100.0;
    const double toneBalanceBias = adjustments.balanceBias / 100.0;

    std::vector<quint16> lut(65536);
    for (int i = 0; i <= 65535; ++i) {
        double compressed = (static_cast<double>(i) / 257.0) * exposureScale * channelScale;
        if (highlightCompression > 0.0 && compressed > compressionStart) {
            const double excess = compressed - compressionStart;
            compressed = compressionStart + excess / (1.0 + excess * compressionStrength / 255.0);
        }

        double linear = std::pow(std::clamp(compressed / 255.0, 0.0, 1.0), invOriginalGamma);
        if (contrastScale != 1.0) {
            linear = (linear - 0.5) * contrastScale + 0.5;
        }
        linear = applyShadowRecovery(linear, shadowRecovery, shadowRange);
        linear = applyToneBalance(linear, toneBalanceAmount, toneBalanceBias);
        const double output =
            std::pow(std::clamp(linear, 0.0, 1.0), invNewGamma) * 65535.0;
        lut[static_cast<size_t>(i)] = static_cast<quint16>(
            std::clamp(static_cast<int>(std::lround(output)), 0, 65535));
    }

    return lut;
}
}

QImage PreviewImageProcessing::applyDisplayAdjustments(
    const QImage &scaledSource,
    const PreviewAdjustmentValues &adjustments)
{
    if (scaledSource.isNull()) {
        return QImage();
    }

    QImage displayImage = scaledSource.convertToFormat(QImage::Format_RGB32);

    const double exposureScale = std::pow(2.0, adjustments.exposureTenthsEv / 10.0);
    const double wbBias = adjustments.whiteBalance / 100.0;
    const double redScale = std::pow(2.0, wbBias);
    const double blueScale = std::pow(2.0, -wbBias);
    const double tintBias = adjustments.tint / 100.0;
    const double greenScale = std::pow(2.0, -tintBias);
    constexpr double kOriginalGamma = 2.2;
    const double newGamma = std::max(adjustments.gammaHundredths / 100.0, 0.01);
    const double contrastScale = 1.0 + adjustments.contrast / 100.0;
    const double shadowRecovery = adjustments.shadows / 100.0;
    const double shadowRange = adjustments.shadowRange / 100.0;
    const double invOriginalGamma = 1.0 / kOriginalGamma;
    const double invNewGamma = 1.0 / newGamma;
    const double highlightCompression = adjustments.highlightCompression / 100.0;
    const double toneBalanceAmount = adjustments.toneBalance / 100.0;
    const double toneBalanceBias = adjustments.balanceBias / 100.0;

    std::array<quint8, 256> gammaLut{};
    for (int i = 0; i < 256; ++i) {
        double linear = std::pow(static_cast<double>(i) / 255.0, invOriginalGamma);
        if (contrastScale != 1.0) {
            linear = (linear - 0.5) * contrastScale + 0.5;
        }
        linear = applyShadowRecovery(linear, shadowRecovery, shadowRange);
        linear = applyToneBalance(linear, toneBalanceAmount, toneBalanceBias);
        gammaLut[static_cast<size_t>(i)] = static_cast<quint8>(std::clamp(
            static_cast<int>(std::lround(
                std::pow(std::clamp(linear, 0.0, 1.0), invNewGamma) * 255.0)),
            0,
            255));
    }

    std::array<quint8, kToneLutMaxInput + 1> toneLut{};
    const double compressionStart = 200.0 - highlightCompression * 152.0;
    const double compressionStrength = highlightCompression * 2.0
        + highlightCompression * highlightCompression * 14.0;
    for (int i = 0; i <= kToneLutMaxInput; ++i) {
        double compressed = static_cast<double>(i);
        if (highlightCompression > 0.0 && compressed > compressionStart) {
            const double excess = compressed - compressionStart;
            compressed =
                compressionStart + excess / (1.0 + excess * compressionStrength / 255.0);
        }
        const int lutIndex = std::clamp(static_cast<int>(std::lround(compressed)), 0, 255);
        toneLut[static_cast<size_t>(i)] = gammaLut[static_cast<size_t>(lutIndex)];
    }

    const double saturationScale = 1.0 + adjustments.saturation / 100.0;
    uchar *displayBits = displayImage.bits();
    const qsizetype displayBytesPerLine = displayImage.bytesPerLine();
    superccd::parallel::forRows(displayImage.height(), 16, [&](int y, unsigned) {
        QRgb *scanLine = reinterpret_cast<QRgb *>(
            displayBits + static_cast<qsizetype>(y) * displayBytesPerLine);
        for (int x = 0; x < displayImage.width(); ++x) {
            const QRgb sourcePixel = scanLine[x];
            int r = toneLut[static_cast<size_t>(std::clamp(
                static_cast<int>(std::lround(qRed(sourcePixel) * exposureScale * redScale)),
                0,
                kToneLutMaxInput))];
            int g = toneLut[static_cast<size_t>(std::clamp(
                static_cast<int>(std::lround(qGreen(sourcePixel) * exposureScale * greenScale)),
                0,
                kToneLutMaxInput))];
            int b = toneLut[static_cast<size_t>(std::clamp(
                static_cast<int>(std::lround(qBlue(sourcePixel) * exposureScale * blueScale)),
                0,
                kToneLutMaxInput))];

            if (saturationScale != 1.0) {
                const double gray = (r + g + b) / 3.0;
                r = std::clamp(static_cast<int>(
                    std::lround(gray + (r - gray) * saturationScale)), 0, 255);
                g = std::clamp(static_cast<int>(
                    std::lround(gray + (g - gray) * saturationScale)), 0, 255);
                b = std::clamp(static_cast<int>(
                    std::lround(gray + (b - gray) * saturationScale)), 0, 255);
            }

            scanLine[x] = qRgb(r, g, b);
        }
    });

    return displayImage;
}

QImage PreviewImageProcessing::applyExportAdjustments16(
    const QImage &source,
    const PreviewAdjustmentValues &adjustments)
{
    if (source.isNull()) {
        return QImage();
    }

    QImage adjustedImage = source.convertToFormat(QImage::Format_RGBX64);

    const double wbBias = adjustments.whiteBalance / 100.0;
    const double redScale = std::pow(2.0, wbBias);
    const double blueScale = std::pow(2.0, -wbBias);
    const double tintBias = adjustments.tint / 100.0;
    const double greenScale = std::pow(2.0, -tintBias);
    const std::vector<quint16> redLut = buildToneLut16(adjustments, redScale);
    const std::vector<quint16> greenLut = buildToneLut16(adjustments, greenScale);
    const std::vector<quint16> blueLut = buildToneLut16(adjustments, blueScale);
    const double saturationScale = 1.0 + adjustments.saturation / 100.0;

    for (int y = 0; y < adjustedImage.height(); ++y) {
        QRgba64 *scanLine = reinterpret_cast<QRgba64 *>(adjustedImage.scanLine(y));
        for (int x = 0; x < adjustedImage.width(); ++x) {
            const QRgba64 sourcePixel = scanLine[x];
            int r = redLut[static_cast<size_t>(sourcePixel.red())];
            int g = greenLut[static_cast<size_t>(sourcePixel.green())];
            int b = blueLut[static_cast<size_t>(sourcePixel.blue())];

            if (saturationScale != 1.0) {
                const double gray = (r + g + b) / 3.0;
                r = std::clamp(static_cast<int>(
                    std::lround(gray + (r - gray) * saturationScale)), 0, 65535);
                g = std::clamp(static_cast<int>(
                    std::lround(gray + (g - gray) * saturationScale)), 0, 65535);
                b = std::clamp(static_cast<int>(
                    std::lround(gray + (b - gray) * saturationScale)), 0, 65535);
            }

            scanLine[x] = QRgba64::fromRgba64(
                static_cast<quint16>(r),
                static_cast<quint16>(g),
                static_cast<quint16>(b),
                65535);
        }
    }

    return adjustedImage;
}

void PreviewImageProcessing::suppressFalseColor16(QImage &image)
{
    if (image.isNull() || image.width() < 2 || image.height() < 2) {
        return;
    }
    if (image.format() != QImage::Format_RGBX64) {
        image = image.convertToFormat(QImage::Format_RGBX64);
    }

    constexpr int radius = 3;
    constexpr int diameter = radius * 2 + 1;
    constexpr int filterArea = diameter * diameter;
    const int width = image.width();
    const int height = image.height();
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<std::int32_t> horizontalDr(pixelCount);
    std::vector<std::int32_t> horizontalDb(pixelCount);
    uchar *imageBits = image.bits();
    const qsizetype imageBytesPerLine = image.bytesPerLine();

    // Blur only color differences so luma edges stay intact.
    superccd::parallel::forRows(height, 8, [&](int y, unsigned) {
        const QRgba64 *scanLine = reinterpret_cast<const QRgba64 *>(
            imageBits + static_cast<qsizetype>(y) * imageBytesPerLine);
        int sumDr = 0;
        int sumDb = 0;
        for (int dx = -radius; dx <= radius; ++dx) {
            const QRgba64 pixel = scanLine[std::clamp(dx, 0, width - 1)];
            sumDr += static_cast<int>(pixel.red()) - static_cast<int>(pixel.green());
            sumDb += static_cast<int>(pixel.blue()) - static_cast<int>(pixel.green());
        }

        const size_t rowOffset = static_cast<size_t>(y) * static_cast<size_t>(width);
        for (int x = 0; x < width; ++x) {
            horizontalDr[rowOffset + static_cast<size_t>(x)] = sumDr;
            horizontalDb[rowOffset + static_cast<size_t>(x)] = sumDb;
            if (x + 1 >= width) {
                continue;
            }

            const QRgba64 leaving = scanLine[std::clamp(x - radius, 0, width - 1)];
            const QRgba64 entering = scanLine[std::clamp(x + radius + 1, 0, width - 1)];
            sumDr += (static_cast<int>(entering.red()) - static_cast<int>(entering.green())) -
                (static_cast<int>(leaving.red()) - static_cast<int>(leaving.green()));
            sumDb += (static_cast<int>(entering.blue()) - static_cast<int>(entering.green())) -
                (static_cast<int>(leaving.blue()) - static_cast<int>(leaving.green()));
        }
    });

    superccd::parallel::forRanges(
        0,
        static_cast<size_t>(width),
        64,
        [&](size_t begin, size_t end, unsigned) {
            for (size_t x = begin; x < end; ++x) {
                int columnDr = 0;
                int columnDb = 0;
                for (int dy = -radius; dy <= radius; ++dy) {
                    const int sourceY = std::clamp(dy, 0, height - 1);
                    const size_t index =
                        static_cast<size_t>(sourceY) * static_cast<size_t>(width) + x;
                    columnDr += horizontalDr[index];
                    columnDb += horizontalDb[index];
                }

                for (int y = 0; y < height; ++y) {
                    QRgba64 *scanLine = reinterpret_cast<QRgba64 *>(
                        imageBits + static_cast<qsizetype>(y) * imageBytesPerLine);
                    const QRgba64 source = scanLine[x];
                    const double luma =
                        (source.red() + 2.0 * source.green() + source.blue()) * 0.25;
                    const double filteredDr = static_cast<double>(columnDr) / filterArea;
                    const double filteredDb = static_cast<double>(columnDb) / filterArea;
                    const double filteredG = luma - (filteredDr + filteredDb) * 0.25;
                    scanLine[x] = QRgba64::fromRgba64(
                        static_cast<quint16>(std::clamp(
                            static_cast<int>(std::lround(filteredG + filteredDr)), 0, 65535)),
                        static_cast<quint16>(std::clamp(
                            static_cast<int>(std::lround(filteredG)), 0, 65535)),
                        static_cast<quint16>(std::clamp(
                            static_cast<int>(std::lround(filteredG + filteredDb)), 0, 65535)),
                        65535);

                    if (y + 1 >= height) {
                        continue;
                    }
                    const int leavingY = std::clamp(y - radius, 0, height - 1);
                    const int enteringY = std::clamp(y + radius + 1, 0, height - 1);
                    const size_t leavingIndex =
                        static_cast<size_t>(leavingY) * static_cast<size_t>(width) + x;
                    const size_t enteringIndex =
                        static_cast<size_t>(enteringY) * static_cast<size_t>(width) + x;
                    columnDr += horizontalDr[enteringIndex] - horizontalDr[leavingIndex];
                    columnDb += horizontalDb[enteringIndex] - horizontalDb[leavingIndex];
                }
            }
        });
}

std::optional<PreviewWhiteBalanceEstimate>
PreviewImageProcessing::estimateNeutralWhiteBalance(
    const QImage &source,
    const QRect &sampleRect)
{
    const QRect clippedRect = sampleRect.intersected(source.rect());
    if (source.isNull() || clippedRect.isEmpty()) {
        return std::nullopt;
    }

    QImage converted;
    const QImage *sampleImage = &source;
    QRect iterationRect = clippedRect;
    if (source.format() != QImage::Format_RGBX64) {
        converted = source.copy(clippedRect).convertToFormat(QImage::Format_RGBX64);
        if (converted.isNull()) {
            return std::nullopt;
        }
        sampleImage = &converted;
        iterationRect = converted.rect();
    }

    std::uint64_t redSum = 0;
    std::uint64_t greenSum = 0;
    std::uint64_t blueSum = 0;
    std::uint64_t pixelCount = 0;
    for (int y = iterationRect.top(); y <= iterationRect.bottom(); ++y) {
        const QRgba64 *scanLine =
            reinterpret_cast<const QRgba64 *>(sampleImage->constScanLine(y));
        for (int x = iterationRect.left(); x <= iterationRect.right(); ++x) {
            const QRgba64 pixel = scanLine[x];
            redSum += pixel.red();
            greenSum += pixel.green();
            blueSum += pixel.blue();
            ++pixelCount;
        }
    }

    if (pixelCount == 0 || redSum == 0 || greenSum == 0 || blueSum == 0) {
        return std::nullopt;
    }

    const double redMean = static_cast<double>(redSum)
        / static_cast<double>(pixelCount);
    const double greenMean = static_cast<double>(greenSum)
        / static_cast<double>(pixelCount);
    const double blueMean = static_cast<double>(blueSum)
        / static_cast<double>(pixelCount);
    const double balancedRedBlue = std::sqrt(redMean * blueMean);

    PreviewWhiteBalanceEstimate estimate;
    estimate.whiteBalance = 50.0 * std::log2(blueMean / redMean);
    estimate.tint = 100.0 * std::log2(greenMean / balancedRedBlue);
    return estimate;
}

void PreviewImageProcessing::applyLumaSharpening8(QImage &image, int amount)
{
    if (amount <= 0 || image.width() < 2 || image.height() < 2) {
        return;
    }
    if (image.format() != QImage::Format_RGB32) {
        image = image.convertToFormat(QImage::Format_RGB32);
    }

    const auto fillLuma = [&](int y, std::vector<int> &row) {
        const QRgb *pixels = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            row[static_cast<size_t>(x)] =
                (77 * qRed(pixels[x]) + 150 * qGreen(pixels[x])
                 + 29 * qBlue(pixels[x]) + 128) >> 8;
        }
    };
    const auto applyPixel = [&](int x, int y, double detail) {
        QRgb *pixels = reinterpret_cast<QRgb *>(image.scanLine(y));
        const QRgb sourcePixel = pixels[x];
        pixels[x] = qRgb(
            std::clamp(static_cast<int>(std::lround(qRed(sourcePixel) + detail)), 0, 255),
            std::clamp(static_cast<int>(std::lround(qGreen(sourcePixel) + detail)), 0, 255),
            std::clamp(static_cast<int>(std::lround(qBlue(sourcePixel) + detail)), 0, 255));
    };
    applyLumaSharpening(image.width(), image.height(), amount, fillLuma, applyPixel);
}

void PreviewImageProcessing::applyLumaSharpening16(QImage &image, int amount)
{
    if (amount <= 0 || image.width() < 2 || image.height() < 2) {
        return;
    }
    if (image.format() != QImage::Format_RGBX64) {
        image = image.convertToFormat(QImage::Format_RGBX64);
    }

    const auto fillLuma = [&](int y, std::vector<int> &row) {
        const QRgba64 *pixels = reinterpret_cast<const QRgba64 *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            row[static_cast<size_t>(x)] =
                (77 * static_cast<int>(pixels[x].red())
                 + 150 * static_cast<int>(pixels[x].green())
                 + 29 * static_cast<int>(pixels[x].blue()) + 128) >> 8;
        }
    };
    const auto applyPixel = [&](int x, int y, double detail) {
        QRgba64 *pixels = reinterpret_cast<QRgba64 *>(image.scanLine(y));
        const QRgba64 sourcePixel = pixels[x];
        pixels[x] = QRgba64::fromRgba64(
            static_cast<quint16>(std::clamp(
                static_cast<int>(std::lround(sourcePixel.red() + detail)), 0, 65535)),
            static_cast<quint16>(std::clamp(
                static_cast<int>(std::lround(sourcePixel.green() + detail)), 0, 65535)),
            static_cast<quint16>(std::clamp(
                static_cast<int>(std::lround(sourcePixel.blue() + detail)), 0, 65535)),
            65535);
    };
    applyLumaSharpening(image.width(), image.height(), amount, fillLuma, applyPixel);
}
