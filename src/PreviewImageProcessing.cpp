#include "PreviewImageProcessing.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {
constexpr int kToneLutMaxInput = 8192;

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
    const double invOriginalGamma = 1.0 / kOriginalGamma;
    const double invNewGamma = 1.0 / newGamma;
    const double highlightCompression = adjustments.highlightCompression / 100.0;

    std::array<quint8, 256> gammaLut{};
    for (int i = 0; i < 256; ++i) {
        double linear = std::pow(static_cast<double>(i) / 255.0, invOriginalGamma);
        if (contrastScale != 1.0) {
            linear = (linear - 0.5) * contrastScale + 0.5;
        }
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
    for (int y = 0; y < displayImage.height(); ++y) {
        QRgb *scanLine = reinterpret_cast<QRgb *>(displayImage.scanLine(y));
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
    }

    return displayImage;
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
