#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "SuperCCDProcessor.h"
#include "DngWriter.h"

#include <libraw/libraw.h>
#include <QFileInfo>
#include <QImageReader>
#include <QProcess>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cstdarg>
#include <climits>
#include <cmath>
#include <algorithm>

namespace {
struct PairStats {
    uint64_t count = 0;
    double meanA = 0.0;
    double meanB = 0.0;
    double meanRatio = 0.0;
    double meanAbsDiff = 0.0;
    double corr = 0.0;
};

struct OffsetScore {
    int dx = 0;
    int dy = 0;
    uint64_t count = 0;
    double gain = 1.0;
    double meanRelativeError = 0.0;
    double corr = 0.0;
};

struct LinearShotPlanes {
    std::vector<uint16_t> g1;
    std::vector<uint16_t> r;
    std::vector<uint16_t> b;
    std::vector<uint16_t> g2;
    int width = 0;
    int height = 0;
    int expandedWidth = 0;
};

void logProcessing(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    time_t t = time(NULL);
    char timestr[32];
    strftime(timestr, sizeof(timestr), "%Y-%m-%dT%H:%M:%S", localtime(&t));
    FILE *f = fopen("processing.log", "a");
    if (f) {
        fprintf(f, "%s - ", timestr);
        vfprintf(f, format, args);
        fprintf(f, "\n");
        fclose(f);
    }
    va_end(args);
}

QImage qImageFromLibRawProcessedThumb(const libraw_processed_image_t *thumb)
{
    if (!thumb || !thumb->data || thumb->width <= 0 || thumb->height <= 0) {
        return QImage();
    }

    if (thumb->type == LIBRAW_IMAGE_JPEG) {
        return QImage::fromData(reinterpret_cast<const uchar *>(thumb->data),
                                thumb->data_size,
                                "JPEG");
    }

    if (thumb->type != LIBRAW_IMAGE_BITMAP) {
        return QImage();
    }

    const int width = static_cast<int>(thumb->width);
    const int height = static_cast<int>(thumb->height);
    const int colors = static_cast<int>(thumb->colors);
    const int bits = static_cast<int>(thumb->bits);
    if (colors <= 0 || (bits != 8 && bits != 16)) {
        return QImage();
    }

    QImage image(width, height, QImage::Format_RGB32);
    if (image.isNull()) {
        return QImage();
    }

    const uchar *src8 = reinterpret_cast<const uchar *>(thumb->data);
    const ushort *src16 = reinterpret_cast<const ushort *>(thumb->data);
    const int samplesPerPixel = colors;
    for (int y = 0; y < height; ++y) {
        QRgb *dst = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const int idx = (y * width + x) * samplesPerPixel;
            int r = 0;
            int g = 0;
            int b = 0;
            if (bits == 8) {
                if (colors >= 3) {
                    r = src8[idx + 0];
                    g = src8[idx + 1];
                    b = src8[idx + 2];
                } else {
                    r = g = b = src8[idx];
                }
            } else {
                if (colors >= 3) {
                    r = src16[idx + 0] >> 8;
                    g = src16[idx + 1] >> 8;
                    b = src16[idx + 2] >> 8;
                } else {
                    r = g = b = src16[idx] >> 8;
                }
            }
            dst[x] = qRgb(r, g, b);
        }
    }
    return image;
}

QImage extractThumbnailWithExifTool(const QString &inputPath)
{
    const QString exifTool = QStringLiteral("exiftool");
    const QStringList tags = {
        QStringLiteral("-b"),
        QStringLiteral("-PreviewImage"),
        inputPath
    };

    QProcess process;
    process.start(exifTool, tags);
    if (!process.waitForFinished(15000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        QProcess thumbProcess;
        thumbProcess.start(exifTool, {QStringLiteral("-b"), QStringLiteral("-ThumbnailImage"), inputPath});
        if (!thumbProcess.waitForFinished(15000) || thumbProcess.exitStatus() != QProcess::NormalExit || thumbProcess.exitCode() != 0) {
            return QImage();
        }
        return QImage::fromData(thumbProcess.readAllStandardOutput());
    }

    return QImage::fromData(process.readAllStandardOutput());
}

bool saveDebugPGM(const QString &path, const std::vector<uint16_t> &data, int w, int h)
{
    FILE *f = fopen(path.toUtf8().constData(), "wb");
    if (!f) return false;
    // PGM P5 header: magic, width, height, max value (65535 for 16-bit)
    fprintf(f, "P5\n%d %d\n65535\n", w, h);
    // PGM 16-bit data must be big-endian
    for (uint16_t val : data) {
        unsigned char bytes[2];
        bytes[0] = static_cast<unsigned char>((val >> 8) & 0xFF);
        bytes[1] = static_cast<unsigned char>(val & 0xFF);
        fwrite(bytes, 1, 2, f);
    }
    fclose(f);
    return true;
}

uint16_t planeSampleClamped(const std::vector<uint16_t> &plane,
                            int planeWidth,
                            int planeHeight,
                            int x,
                            int y)
{
    if (planeWidth <= 0 || planeHeight <= 0) {
        return 0;
    }

    if (x < 0) {
        x = 0;
    } else if (x >= planeWidth) {
        x = planeWidth - 1;
    }

    if (y < 0) {
        y = 0;
    } else if (y >= planeHeight) {
        y = planeHeight - 1;
    }

    return plane[static_cast<size_t>(y) * static_cast<size_t>(planeWidth) +
                 static_cast<size_t>(x)];
}

uint16_t averageSamples(const std::vector<uint16_t> &plane,
                        const std::vector<uint8_t> &mask,
                        int planeWidth,
                        int planeHeight,
                        const int offsets[][2],
                        int offsetCount,
                        int x,
                        int y)
{
    uint32_t sum = 0;
    int count = 0;
    for (int i = 0; i < offsetCount; ++i) {
        const int nx = x + offsets[i][0];
        const int ny = y + offsets[i][1];
        if (nx < 0 || nx >= planeWidth || ny < 0 || ny >= planeHeight) {
            continue;
        }
        const size_t idx = static_cast<size_t>(ny) * static_cast<size_t>(planeWidth) +
                           static_cast<size_t>(nx);
        if (!mask[idx]) {
            continue;
        }
        sum += plane[idx];
        count++;
    }

    if (count == 0) {
        return 0;
    }
    return static_cast<uint16_t>((sum + static_cast<uint32_t>(count / 2)) / static_cast<uint32_t>(count));
}

void fillSparsePlane(std::vector<uint16_t> &plane,
                     const std::vector<uint8_t> &mask,
                     int planeWidth,
                     int planeHeight);

void expandDiagonalPlane(const std::vector<uint16_t> &sourcePlane,
                         const std::vector<uint8_t> &sourceMask,
                         int sourceWidth,
                         int sourceHeight,
                         int phaseBase,
                         std::vector<uint16_t> &expandedPlane,
                         int expandedWidth)
{
    expandedPlane.assign(static_cast<size_t>(expandedWidth) * static_cast<size_t>(sourceHeight), 0);
    std::vector<uint8_t> mask(static_cast<size_t>(expandedWidth) * static_cast<size_t>(sourceHeight), 0);

    for (int y = 0; y < sourceHeight; ++y) {
        const int phase = ((y & 1) == 0) ? phaseBase : (1 - phaseBase);
        for (int x = 0; x < sourceWidth; ++x) {
            const size_t srcIdx = static_cast<size_t>(y) * static_cast<size_t>(sourceWidth) +
                                  static_cast<size_t>(x);
            if (!sourceMask.empty() && !sourceMask[srcIdx]) {
                continue;
            }
            const int outX = x * 2 + phase;
            const size_t dstIdx = static_cast<size_t>(y) * static_cast<size_t>(expandedWidth) +
                                  static_cast<size_t>(outX);
            expandedPlane[dstIdx] = sourcePlane[srcIdx];
            mask[dstIdx] = 1;
        }
    }

    const int diagonalOffsets[4][2] = {
        {-1, -1}, {1, -1}, {-1, 1}, {1, 1}
    };
    const int horizontalOffsets[2][2] = {
        {-1, 0}, {1, 0}
    };
    const int verticalOffsets[2][2] = {
        {0, -1}, {0, 1}
    };

    for (int y = 0; y < sourceHeight; ++y) {
        for (int x = 0; x < expandedWidth; ++x) {
            const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(expandedWidth) +
                               static_cast<size_t>(x);
            if (mask[idx]) {
                continue;
            }

            uint16_t value = averageSamples(expandedPlane, mask, expandedWidth, sourceHeight,
                                            horizontalOffsets, 2, x, y);
            if (value == 0) {
                value = averageSamples(expandedPlane, mask, expandedWidth, sourceHeight,
                                       diagonalOffsets, 4, x, y);
            }
            if (value == 0) {
                value = averageSamples(expandedPlane, mask, expandedWidth, sourceHeight,
                                       verticalOffsets, 2, x, y);
            }
            expandedPlane[idx] = value;
        }
    }
}

template <typename T>
double sampleCompactPhasePlane(const std::vector<T> &plane,
                               int compactWidth,
                               int planeHeight,
                               int phaseBase,
                               double sx,
                               double sy)
{
    if (compactWidth <= 0 || planeHeight <= 0 || plane.empty()) {
        return 0.0;
    }

    sy = std::clamp(sy, 0.0, static_cast<double>(planeHeight - 1));
    const int y0 = std::clamp(static_cast<int>(std::floor(sy)), 0, planeHeight - 1);
    const int y1 = std::clamp(y0 + 1, 0, planeHeight - 1);
    const double fy = sy - static_cast<double>(y0);

    auto sampleRow = [&](int row) -> double {
        const int phase = ((row & 1) == 0) ? phaseBase : (1 - phaseBase);
        double u = (sx - static_cast<double>(phase)) * 0.5;
        u = std::clamp(u, 0.0, static_cast<double>(compactWidth - 1));
        const int x0 = std::clamp(static_cast<int>(std::floor(u)), 0, compactWidth - 1);
        const int x1 = std::clamp(x0 + 1, 0, compactWidth - 1);
        const double fx = u - static_cast<double>(x0);
        const double v0 = static_cast<double>(plane[static_cast<size_t>(row) * static_cast<size_t>(compactWidth) +
                                                     static_cast<size_t>(x0)]);
        const double v1 = static_cast<double>(plane[static_cast<size_t>(row) * static_cast<size_t>(compactWidth) +
                                                     static_cast<size_t>(x1)]);
        return (1.0 - fx) * v0 + fx * v1;
    };

    const double row0 = sampleRow(y0);
    const double row1 = sampleRow(y1);
    return (1.0 - fy) * row0 + fy * row1;
}

template <typename T>
double sampleCompactPhasePlaneDirectional(const std::vector<T> &plane,
                                          int compactWidth,
                                          int planeHeight,
                                          int phaseBase,
                                          double sx,
                                          double sy,
                                          bool preferHorizontal)
{
    if (compactWidth <= 0 || planeHeight <= 0 || plane.empty()) {
        return 0.0;
    }

    sy = std::clamp(sy, 0.0, static_cast<double>(planeHeight - 1));
    const int y0 = std::clamp(static_cast<int>(std::floor(sy)), 0, planeHeight - 1);
    const int y1 = std::clamp(y0 + 1, 0, planeHeight - 1);
    const double fy = sy - static_cast<double>(y0);

    auto sampleHorizontal = [&](int row) -> double {
        const int phase = ((row & 1) == 0) ? phaseBase : (1 - phaseBase);
        double u = (sx - static_cast<double>(phase)) * 0.5;
        u = std::clamp(u, 0.0, static_cast<double>(compactWidth - 1));
        const int x0 = std::clamp(static_cast<int>(std::floor(u)), 0, compactWidth - 1);
        const int x1 = std::clamp(x0 + 1, 0, compactWidth - 1);
        const double fx = u - static_cast<double>(x0);
        const double v0 = static_cast<double>(plane[static_cast<size_t>(row) * static_cast<size_t>(compactWidth) +
                                                     static_cast<size_t>(x0)]);
        const double v1 = static_cast<double>(plane[static_cast<size_t>(row) * static_cast<size_t>(compactWidth) +
                                                     static_cast<size_t>(x1)]);
        return (1.0 - fx) * v0 + fx * v1;
    };

    if (preferHorizontal || y0 == y1) {
        const double row0 = sampleHorizontal(y0);
        const double row1 = sampleHorizontal(y1);
        return (1.0 - fy) * row0 + fy * row1;
    }

    const int phase0 = ((y0 & 1) == 0) ? phaseBase : (1 - phaseBase);
    const int phase1 = ((y1 & 1) == 0) ? phaseBase : (1 - phaseBase);
    int x0 = static_cast<int>(std::floor((sx - static_cast<double>(phase0)) * 0.5 + 0.5));
    int x1 = static_cast<int>(std::floor((sx - static_cast<double>(phase1)) * 0.5 + 0.5));
    x0 = std::clamp(x0, 0, compactWidth - 1);
    x1 = std::clamp(x1, 0, compactWidth - 1);
    const double v0 = static_cast<double>(plane[static_cast<size_t>(y0) * static_cast<size_t>(compactWidth) +
                                                 static_cast<size_t>(x0)]);
    const double v1 = static_cast<double>(plane[static_cast<size_t>(y1) * static_cast<size_t>(compactWidth) +
                                                 static_cast<size_t>(x1)]);
    return (1.0 - fy) * v0 + fy * v1;
}

std::vector<OffsetScore> deriveSameColorOffsets(const std::vector<uint16_t> &primary,
                                                const std::vector<uint8_t> &primaryChannels,
                                                const std::vector<uint16_t> &secondary,
                                                const std::vector<uint8_t> &secondaryChannels,
                                                int width,
                                                int height,
                                                uint8_t channel,
                                                int parityClass,
                                                int radius,
                                                int topCount)
{
    std::vector<OffsetScore> scores;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }

            long double sumPrimary = 0.0;
            long double sumSecondary = 0.0;
            long double sumPrimary2 = 0.0;
            long double sumSecondary2 = 0.0;
            long double sumProduct = 0.0;
            uint64_t count = 0;
            for (int y = 0; y < height; ++y) {
                const int ny = y + dy;
                if (ny < 0 || ny >= height) {
                    continue;
                }
                for (int x = 0; x < width; ++x) {
                    const int nx = x + dx;
                    if (nx < 0 || nx >= width) {
                        continue;
                    }
                    const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
                    const size_t nidx = static_cast<size_t>(ny) * static_cast<size_t>(width) + static_cast<size_t>(nx);
                    if (primary[idx] == 0 || secondary[nidx] == 0) {
                        continue;
                    }
                    const int thisParity = ((y & 1) << 1) | (x & 1);
                    if (thisParity != parityClass) {
                        continue;
                    }
                    if (primaryChannels[idx] != channel || secondaryChannels[nidx] != channel) {
                        continue;
                    }
                    const uint16_t primarySample = primary[idx];
                    const uint16_t secondarySample = secondary[nidx];
                    sumPrimary += primarySample;
                    sumSecondary += secondarySample;
                    sumPrimary2 += static_cast<long double>(primarySample) * static_cast<long double>(primarySample);
                    sumSecondary2 += static_cast<long double>(secondarySample) * static_cast<long double>(secondarySample);
                    sumProduct += static_cast<long double>(primarySample) * static_cast<long double>(secondarySample);
                    count++;
                }
            }

            if (count > 0) {
                OffsetScore score;
                score.dx = dx;
                score.dy = dy;
                score.count = count;
                if (sumSecondary > 0.0) {
                    score.gain = static_cast<double>(sumPrimary / sumSecondary);
                }

                const long double countLd = static_cast<long double>(count);
                const long double meanPrimary = sumPrimary / countLd;
                const long double meanSecondary = sumSecondary / countLd;
                const long double cov = (sumProduct / countLd) - (meanPrimary * meanSecondary);
                const long double varPrimary = (sumPrimary2 / countLd) - (meanPrimary * meanPrimary);
                const long double varSecondary = (sumSecondary2 / countLd) - (meanSecondary * meanSecondary);
                if (varPrimary > 0.0 && varSecondary > 0.0) {
                    score.corr = static_cast<double>(cov / std::sqrt(varPrimary * varSecondary));
                }

                long double relErrSum = 0.0;
                for (int y = 0; y < height; ++y) {
                    const int ny = y + dy;
                    if (ny < 0 || ny >= height) {
                        continue;
                    }
                    for (int x = 0; x < width; ++x) {
                        const int nx = x + dx;
                        if (nx < 0 || nx >= width) {
                            continue;
                        }
                        const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
                        const size_t nidx = static_cast<size_t>(ny) * static_cast<size_t>(width) + static_cast<size_t>(nx);
                        if (primary[idx] == 0 || secondary[nidx] == 0) {
                            continue;
                        }
                        const int thisParity = ((y & 1) << 1) | (x & 1);
                        if (thisParity != parityClass) {
                            continue;
                        }
                        if (primaryChannels[idx] != channel || secondaryChannels[nidx] != channel) {
                            continue;
                        }

                        const long double primarySample = primary[idx];
                        const long double mappedSecondary = static_cast<long double>(secondary[nidx]) * score.gain;
                        const long double denom = primarySample > 1.0 ? primarySample : 1.0;
                        relErrSum += std::abs(primarySample - mappedSecondary) / denom;
                    }
                }
                score.meanRelativeError = static_cast<double>(relErrSum / countLd);
                scores.push_back(score);
            }
        }
    }

    std::sort(scores.begin(), scores.end(), [](const OffsetScore &a, const OffsetScore &b) {
        if (std::abs(a.corr - b.corr) > 1e-9) {
            return a.corr > b.corr;
        }
        if (std::abs(a.meanRelativeError - b.meanRelativeError) > 1e-9) {
            return a.meanRelativeError < b.meanRelativeError;
        }
        if (a.count != b.count) {
            return a.count > b.count;
        }
        const int da = a.dx * a.dx + a.dy * a.dy;
        const int db = b.dx * b.dx + b.dy * b.dy;
        return da < db;
    });

    if (static_cast<int>(scores.size()) > topCount) {
        scores.resize(static_cast<size_t>(topCount));
    }
    return scores;
}

bool projectSecondaryOntoPrimary(const std::vector<uint16_t> &primary,
                                 const std::vector<uint8_t> &primaryChannels,
                                 const std::vector<uint16_t> &secondary,
                                 const std::vector<uint8_t> &secondaryChannels,
                                 int width,
                                 int height,
                                 std::vector<uint16_t> &projected,
                                 QString &logLabel)
{
    projected.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0);

    std::vector<OffsetScore> offsetsByChannelParity[4][4];
    for (uint8_t channel = 0; channel < 4; ++channel) {
        for (int parityClass = 0; parityClass < 4; ++parityClass) {
            offsetsByChannelParity[channel][parityClass] = deriveSameColorOffsets(primary,
                                                                                  primaryChannels,
                                                                                  secondary,
                                                                                  secondaryChannels,
                                                                                  width,
                                                                                  height,
                                                                                  channel,
                                                                                  parityClass,
                                                                                  3,
                                                                                  4);
        }
    }

    for (int channel = 0; channel < 4; ++channel) {
        QString summary = QStringLiteral("ch%1").arg(channel);
        for (int parityClass = 0; parityClass < 4; ++parityClass) {
            summary += QStringLiteral(" p%1").arg(parityClass);
            const std::vector<OffsetScore> &offsets = offsetsByChannelParity[channel][parityClass];
            if (offsets.empty()) {
                summary += QStringLiteral(" none");
                continue;
            }
            for (const OffsetScore &offset : offsets) {
                summary += QStringLiteral(" (%1,%2)c=%3 r=%4 e=%5 g=%6")
                               .arg(offset.dx)
                               .arg(offset.dy)
                               .arg(static_cast<qulonglong>(offset.count))
                               .arg(offset.corr, 0, 'f', 5)
                               .arg(offset.meanRelativeError, 0, 'f', 5)
                               .arg(offset.gain, 0, 'f', 5);
            }
        }
        logLabel += summary + QLatin1String("; ");
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
            if (primary[idx] == 0) {
                continue;
            }

            const uint8_t channel = primaryChannels[idx];
            if (channel > 3) {
                continue;
            }
            const int parityClass = ((y & 1) << 1) | (x & 1);

            for (const OffsetScore &offset : offsetsByChannelParity[channel][parityClass]) {
                const int nx = x + offset.dx;
                const int ny = y + offset.dy;
                if (nx < 0 || nx >= width || ny < 0 || ny >= height) {
                    continue;
                }
                const size_t nidx = static_cast<size_t>(ny) * static_cast<size_t>(width) + static_cast<size_t>(nx);
                const uint16_t sample = secondary[nidx];
                if (sample == 0 || secondaryChannels[nidx] != channel) {
                    continue;
                }
                projected[idx] = sample;
                break;
            }
        }
    }

    return true;
}

void estimateProjectedGain(const std::vector<uint16_t> &primary,
                           const std::vector<uint8_t> &primaryChannels,
                           const std::vector<uint16_t> &projectedSecondary,
                           int width,
                           int height,
                           double gains[4],
                           QString &logLabel)
{
    Q_UNUSED(width);
    Q_UNUSED(height);

    long double sumPrimary[4] = {0.0, 0.0, 0.0, 0.0};
    long double sumSecondary[4] = {0.0, 0.0, 0.0, 0.0};
    uint16_t maxPrimary[4] = {0, 0, 0, 0};

    const size_t pixelCount = primary.size();
    for (size_t i = 0; i < pixelCount; ++i) {
        const uint8_t channel = primaryChannels[i];
        if (channel > 3) {
            continue;
        }
        if (primary[i] > maxPrimary[channel]) {
            maxPrimary[channel] = primary[i];
        }
    }

    for (size_t i = 0; i < pixelCount; ++i) {
        const uint8_t channel = primaryChannels[i];
        if (channel > 3) {
            continue;
        }
        const uint16_t s = primary[i];
        const uint16_t r = projectedSecondary[i];
        if (s == 0 || r == 0) {
            continue;
        }

        const double white = maxPrimary[channel] > 0 ? static_cast<double>(maxPrimary[channel]) : 1.0;
        const double normalized = static_cast<double>(s) / white;
        if (normalized < 0.10 || normalized > 0.60) {
            continue;
        }

        sumPrimary[channel] += s;
        sumSecondary[channel] += r;
    }

    logLabel.clear();
    for (int channel = 0; channel < 4; ++channel) {
        gains[channel] = sumSecondary[channel] > 0.0
                             ? static_cast<double>(sumPrimary[channel] / sumSecondary[channel])
                             : 1.0;
        logLabel += QStringLiteral("ch%1 gain=%2 white=%3 ")
                        .arg(channel)
                        .arg(gains[channel], 0, 'f', 5)
                        .arg(maxPrimary[channel]);
    }
}

void mergePrimaryAndProjectedSecondary(const std::vector<uint16_t> &primary,
                                       const std::vector<uint8_t> &primaryChannels,
                                       const std::vector<uint16_t> &projectedSecondary,
                                       int width,
                                       int height,
                                       double rHeadroomScale,
                                       double rTransitionDelay,
                                       double rTransitionSmoothness,
                                       bool sDrivenHighlightsOnly,
                                       std::vector<uint16_t> &merged,
                                       QString &logLabel,
                                       double *appliedOutputScale = nullptr)
{
    merged.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
    const size_t pixelCount = primary.size();
    std::vector<double> desired(pixelCount, 0.0);

    double gains[4] = {1.0, 1.0, 1.0, 1.0};
    QString gainSummary;
    estimateProjectedGain(primary, primaryChannels, projectedSecondary, width, height, gains, gainSummary);

    uint16_t maxPrimary[4] = {0, 0, 0, 0};
    for (size_t i = 0; i < pixelCount; ++i) {
        const uint8_t channel = primaryChannels[i];
        if (channel > 3) {
            continue;
        }
        if (primary[i] > maxPrimary[channel]) {
            maxPrimary[channel] = primary[i];
        }
    }

    uint64_t blendedCount = 0;
    uint64_t replacedCount = 0;
    double maxDesired = 0.0;
    for (size_t i = 0; i < pixelCount; ++i) {
        const uint8_t channel = primaryChannels[i];
        const uint16_t s = primary[i];
        if (channel > 3 || s == 0) {
            continue;
        }

        const uint16_t rProjected = projectedSecondary[i];
        if (rProjected == 0) {
            merged[i] = s;
            desired[i] = static_cast<double>(s);
            continue;
        }

        const double white = maxPrimary[channel] > 0 ? static_cast<double>(maxPrimary[channel]) : 1.0;
        const double scaledS = static_cast<double>(s);
        const double scaledR = static_cast<double>(rProjected) * gains[channel];
        const double normalizedS = static_cast<double>(s) / white;
        const double delay = std::clamp(rTransitionDelay, 0.0, 1.0);
        const double smoothness = std::clamp(rTransitionSmoothness, 0.0, 1.0);
        double blendStart = 0.95;
        double blendEnd = 1.02;
        if (!sDrivenHighlightsOnly) {
            const double shoulderEnd = 0.90 + 0.14 * delay;
            const double shoulderWidth = 0.10 + 0.44 * smoothness;
            blendEnd = std::clamp(shoulderEnd, 0.75, 1.05);
            blendStart = std::clamp(blendEnd - shoulderWidth, 0.0, blendEnd - 0.002);
        }

        if (normalizedS <= blendStart) {
            merged[i] = static_cast<uint16_t>(std::clamp<int>(static_cast<int>(scaledS + 0.5), 0, 65535));
            desired[i] = scaledS;
            continue;
        }

        if (normalizedS >= blendEnd) {
            merged[i] = static_cast<uint16_t>(std::clamp<int>(static_cast<int>(scaledR + 0.5), 0, 65535));
            desired[i] = scaledR;
            replacedCount++;
            if (scaledR > maxDesired) {
                maxDesired = scaledR;
            }
            continue;
        }

        const double t = (normalizedS - blendStart) / (blendEnd - blendStart);
        const double delayHold = sDrivenHighlightsOnly ? 0.0 : (0.75 * delay);
        const double delayedT = (t <= delayHold)
                                  ? 0.0
                                  : ((t - delayHold) / std::max(1e-6, 1.0 - delayHold));
        const double smoothT = delayedT * delayedT * (3.0 - 2.0 * delayedT);
        const double earlyT = std::pow(delayedT, 0.60);
        const double blendT = sDrivenHighlightsOnly
                                ? smoothT
                                : ((1.0 - smoothness) * delayedT
                                   + smoothness * (0.35 * smoothT + 0.65 * earlyT));
        const double mergedValue = (1.0 - blendT) * scaledS + blendT * scaledR;
        desired[i] = mergedValue;
        merged[i] = static_cast<uint16_t>(std::clamp<int>(static_cast<int>(mergedValue + 0.5), 0, 65535));
        blendedCount++;
        if (mergedValue > maxDesired) {
            maxDesired = mergedValue;
        }
    }

    double outputScale = 1.0;
    if (maxDesired > 65535.0) {
        outputScale = 65535.0 / maxDesired;
    }
    outputScale *= rHeadroomScale;
    if (outputScale < 1.0) {
        for (size_t i = 0; i < pixelCount; ++i) {
            if (desired[i] <= 0.0) {
                merged[i] = 0;
                continue;
            }
            const double outputValue = desired[i] * outputScale;
            merged[i] = static_cast<uint16_t>(std::clamp<int>(static_cast<int>(outputValue + 0.5), 0, 65535));
        }
    }

    if (appliedOutputScale) {
        *appliedOutputScale = outputScale;
    }

    logLabel = QStringLiteral("%1userScale=%2 delay=%3 smoothness=%4 maxDesired=%5 outputScale=%6 highlightsOnly=%7 blended=%8 replaced=%9")
                   .arg(gainSummary)
                   .arg(rHeadroomScale, 0, 'f', 3)
                   .arg(rTransitionDelay, 0, 'f', 3)
                   .arg(rTransitionSmoothness, 0, 'f', 3)
                   .arg(maxDesired, 0, 'f', 2)
                   .arg(outputScale, 0, 'f', 6)
                   .arg(sDrivenHighlightsOnly ? 1 : 0)
                   .arg(static_cast<qulonglong>(blendedCount))
                   .arg(static_cast<qulonglong>(replacedCount));
}

bool buildPreviewImageFromCfa(const std::vector<uint16_t> &cfa,
                              int width,
                              int height,
                              const SuperCCDMetadata &metadata,
                              int maxSize,
                              int previewRotation,
                              QImage &preview,
                              QString &error)
{
    if (width < 2 || height < 2 || cfa.empty()) {
        error = QStringLiteral("Invalid CFA data for preview.");
        return false;
    }

    QImage image(width, height, QImage::Format_RGB32);
    if (image.isNull()) {
        error = QStringLiteral("Failed to allocate preview image.");
        return false;
    }

    auto sample = [&](int x, int y) -> double {
        x = std::clamp(x, 0, width - 1);
        y = std::clamp(y, 0, height - 1);
        return static_cast<double>(cfa[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)]);
    };

    enum class CfaColor { Red, Green, Blue };
    auto colorAt = [](int x, int y) -> CfaColor {
        const bool evenY = (y & 1) == 0;
        const bool evenX = (x & 1) == 0;
        if (evenY) {
            return evenX ? CfaColor::Green : CfaColor::Blue;
        }
        return evenX ? CfaColor::Red : CfaColor::Green;
    };

    double sumR = 0.0;
    double sumG = 0.0;
    double sumB = 0.0;
    uint32_t maxR = 1;
    uint32_t maxG = 1;
    uint32_t maxB = 1;

    std::vector<double> linearRgb(static_cast<size_t>(width) * static_cast<size_t>(height) * 3, 0.0);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double center = sample(x, y);
            double r = 0.0;
            double g = 0.0;
            double b = 0.0;

            switch (colorAt(x, y)) {
            case CfaColor::Red: {
                r = center;
                const double gradH = std::abs(sample(x - 1, y) - sample(x + 1, y));
                const double gradV = std::abs(sample(x, y - 1) - sample(x, y + 1));
                if (gradH < gradV) {
                    g = (sample(x - 1, y) + sample(x + 1, y)) * 0.5;
                } else if (gradV < gradH) {
                    g = (sample(x, y - 1) + sample(x, y + 1)) * 0.5;
                } else {
                    g = (sample(x - 1, y) + sample(x + 1, y) + sample(x, y - 1) + sample(x, y + 1)) * 0.25;
                }
                b = (sample(x - 1, y - 1) + sample(x + 1, y - 1) + sample(x - 1, y + 1) + sample(x + 1, y + 1)) * 0.25;
                break;
            }
            case CfaColor::Blue: {
                b = center;
                const double gradH = std::abs(sample(x - 1, y) - sample(x + 1, y));
                const double gradV = std::abs(sample(x, y - 1) - sample(x, y + 1));
                if (gradH < gradV) {
                    g = (sample(x - 1, y) + sample(x + 1, y)) * 0.5;
                } else if (gradV < gradH) {
                    g = (sample(x, y - 1) + sample(x, y + 1)) * 0.5;
                } else {
                    g = (sample(x - 1, y) + sample(x + 1, y) + sample(x, y - 1) + sample(x, y + 1)) * 0.25;
                }
                r = (sample(x - 1, y - 1) + sample(x + 1, y - 1) + sample(x - 1, y + 1) + sample(x + 1, y + 1)) * 0.25;
                break;
            }
            case CfaColor::Green: {
                g = center;
                const bool greenOnBlueRow = ((y & 1) == 0);
                if (greenOnBlueRow) {
                    b = (sample(x - 1, y) + sample(x + 1, y)) * 0.5;
                    r = (sample(x, y - 1) + sample(x, y + 1)) * 0.5;
                } else {
                    r = (sample(x - 1, y) + sample(x + 1, y)) * 0.5;
                    b = (sample(x, y - 1) + sample(x, y + 1)) * 0.5;
                }
                break;
            }
            }

            const size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 3;
            linearRgb[idx + 0] = r;
            linearRgb[idx + 1] = g;
            linearRgb[idx + 2] = b;

            sumR += r;
            sumG += g;
            sumB += b;
            maxR = std::max(maxR, static_cast<uint32_t>(std::clamp(r, 0.0, 65535.0)));
            maxG = std::max(maxG, static_cast<uint32_t>(std::clamp(g, 0.0, 65535.0)));
            maxB = std::max(maxB, static_cast<uint32_t>(std::clamp(b, 0.0, 65535.0)));
        }
    }

    const double pixelCount = static_cast<double>(width) * static_cast<double>(height);
    const double avgR = sumR / pixelCount;
    const double avgG = sumG / pixelCount;
    const double avgB = sumB / pixelCount;
    double gainR = avgR > 0.0 ? avgG / avgR : 1.0;
    double gainG = 1.0;
    double gainB = avgB > 0.0 ? avgG / avgB : 1.0;
    if (metadata.hasAsShotNeutral &&
        metadata.asShotNeutral[0] > 0.0 &&
        metadata.asShotNeutral[1] > 0.0 &&
        metadata.asShotNeutral[2] > 0.0) {
        gainR = metadata.asShotNeutral[1] / metadata.asShotNeutral[0];
        gainG = 1.0;
        gainB = metadata.asShotNeutral[1] / metadata.asShotNeutral[2];
    }
    const double scaleR = 65535.0 / static_cast<double>(maxR);
    const double scaleG = 65535.0 / static_cast<double>(maxG);
    const double scaleB = 65535.0 / static_cast<double>(maxB);

    for (int y = 0; y < height; ++y) {
        QRgb *scanLine = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 3;
            const double linearR = std::clamp((linearRgb[idx + 0] * gainR * scaleR) / 65535.0, 0.0, 1.0);
            const double linearG = std::clamp((linearRgb[idx + 1] * gainG * scaleG) / 65535.0, 0.0, 1.0);
            const double linearB = std::clamp((linearRgb[idx + 2] * gainB * scaleB) / 65535.0, 0.0, 1.0);

            const int outR = static_cast<int>(std::pow(linearR, 1.0 / 2.2) * 255.0 + 0.5);
            const int outG = static_cast<int>(std::pow(linearG, 1.0 / 2.2) * 255.0 + 0.5);
            const int outB = static_cast<int>(std::pow(linearB, 1.0 / 2.2) * 255.0 + 0.5);
            scanLine[x] = qRgb(std::clamp(outR, 0, 255),
                               std::clamp(outG, 0, 255),
                               std::clamp(outB, 0, 255));
        }
    }

    const int fujiWidth = metadata.fujiWidth > 0 ? metadata.fujiWidth : width / 2;
    const int rectWidth = fujiWidth * 2 + 1;
    const int rectHeight = (height - fujiWidth) * 2 + 1;
    if (rectWidth <= 0 || rectHeight <= 0) {
        error = QStringLiteral("Invalid Fuji preview geometry.");
        return false;
    }
    QImage rectified(rectWidth, rectHeight, QImage::Format_RGB32);
    std::vector<uint8_t> rectifiedMask(static_cast<size_t>(rectWidth) * static_cast<size_t>(rectHeight), 0);
    if (rectified.isNull()) {
        error = QStringLiteral("Failed to allocate rectified preview image.");
        return false;
    }
    rectified.fill(Qt::black);

    for (int row = 0; row < height; ++row) {
        const QRgb *srcLine = reinterpret_cast<const QRgb *>(image.constScanLine(row));
        const int start = std::abs(fujiWidth - row);
        const int end = std::min({width, rectHeight + fujiWidth - row, rectWidth - fujiWidth + row});
        for (int col = start; col < end; ++col) {
            const int y = row + col - fujiWidth;
            const int x = fujiWidth - row + col;
            if (x >= 0 && x < rectWidth && y >= 0 && y < rectHeight) {
                rectified.setPixel(x, y, srcLine[col]);
                rectifiedMask[static_cast<size_t>(y) * static_cast<size_t>(rectWidth) + static_cast<size_t>(x)] = 1;
            }
        }
    }

    QImage filled = rectified.copy();
    for (int y = 0; y < rectHeight; ++y) {
        QRgb *dstLine = reinterpret_cast<QRgb *>(filled.scanLine(y));
        for (int x = 0; x < rectWidth; ++x) {
            const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(rectWidth) + static_cast<size_t>(x);
            if (rectifiedMask[idx]) {
                continue;
            }

            int neighborSumR = 0;
            int neighborSumG = 0;
            int neighborSumB = 0;
            int count = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                const int yy = y + dy;
                if (yy < 0 || yy >= rectHeight) {
                    continue;
                }
                for (int dx = -1; dx <= 1; ++dx) {
                    const int xx = x + dx;
                    if (xx < 0 || xx >= rectWidth) {
                        continue;
                    }
                    const size_t neighborIdx = static_cast<size_t>(yy) * static_cast<size_t>(rectWidth) + static_cast<size_t>(xx);
                    if (!rectifiedMask[neighborIdx]) {
                        continue;
                    }
                    const QRgb pixel = rectified.pixel(xx, yy);
                    neighborSumR += qRed(pixel);
                    neighborSumG += qGreen(pixel);
                    neighborSumB += qBlue(pixel);
                    ++count;
                }
            }

            if (count > 0) {
                dstLine[x] = qRgb(neighborSumR / count, neighborSumG / count, neighborSumB / count);
            }
        }
    }

    QTransform transform;
    if (previewRotation == 90 || previewRotation == 180 || previewRotation == 270) {
        transform.rotate(previewRotation);
    }
    QImage display = transform.isIdentity() ? filled : filled.transformed(transform, Qt::FastTransformation);

    if (maxSize > 0 && (display.width() > maxSize || display.height() > maxSize)) {
        preview = display.scaled(maxSize,
                                 maxSize,
                                 Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation);
    } else {
        preview = display;
    }

    return true;
}

bool buildLinearRgbFromCfa(const std::vector<uint16_t> &cfa,
                           int width,
                           int height,
                           std::vector<uint16_t> &rgb,
                           int &rgbWidth,
                           int &rgbHeight,
                           QString &error)
{
    if (width < 2 || height < 2 || cfa.empty()) {
        error = QStringLiteral("Invalid CFA data for linear RGB conversion.");
        return false;
    }

    rgbWidth = width / 2;
    rgbHeight = height / 2;
    rgb.assign(static_cast<size_t>(rgbWidth) * static_cast<size_t>(rgbHeight) * 3, 0);

    double sumR = 0.0;
    double sumG = 0.0;
    double sumB = 0.0;
    uint32_t maxR = 1;
    uint32_t maxG = 1;
    uint32_t maxB = 1;

    for (int y = 0; y < rgbHeight; ++y) {
        const int srcY = y * 2;
        const size_t row0 = static_cast<size_t>(srcY) * static_cast<size_t>(width);
        const size_t row1 = static_cast<size_t>(srcY + 1) * static_cast<size_t>(width);
        for (int x = 0; x < rgbWidth; ++x) {
            const int srcX = x * 2;
            const uint16_t g1 = cfa[row0 + static_cast<size_t>(srcX)];
            const uint16_t b = cfa[row0 + static_cast<size_t>(srcX + 1)];
            const uint16_t r = cfa[row1 + static_cast<size_t>(srcX)];
            const uint16_t g2 = cfa[row1 + static_cast<size_t>(srcX + 1)];
            const uint32_t g = (static_cast<uint32_t>(g1) + static_cast<uint32_t>(g2)) / 2u;

            sumR += r;
            sumG += g;
            sumB += b;
            maxR = std::max(maxR, static_cast<uint32_t>(r));
            maxG = std::max(maxG, g);
            maxB = std::max(maxB, static_cast<uint32_t>(b));
        }
    }

    const double pixelCount = static_cast<double>(rgbWidth) * static_cast<double>(rgbHeight);
    const double avgR = sumR / pixelCount;
    const double avgG = sumG / pixelCount;
    const double avgB = sumB / pixelCount;
    const double gainR = avgR > 0.0 ? avgG / avgR : 1.0;
    const double gainB = avgB > 0.0 ? avgG / avgB : 1.0;

    for (int y = 0; y < rgbHeight; ++y) {
        const int srcY = y * 2;
        const size_t row0 = static_cast<size_t>(srcY) * static_cast<size_t>(width);
        const size_t row1 = static_cast<size_t>(srcY + 1) * static_cast<size_t>(width);
        for (int x = 0; x < rgbWidth; ++x) {
            const int srcX = x * 2;
            const uint16_t g1 = cfa[row0 + static_cast<size_t>(srcX)];
            const uint16_t b = cfa[row0 + static_cast<size_t>(srcX + 1)];
            const uint16_t r = cfa[row1 + static_cast<size_t>(srcX)];
            const uint16_t g2 = cfa[row1 + static_cast<size_t>(srcX + 1)];
            const double g = (static_cast<double>(g1) + static_cast<double>(g2)) * 0.5;

            const size_t dstIdx = (static_cast<size_t>(y) * static_cast<size_t>(rgbWidth) + static_cast<size_t>(x)) * 3;
            rgb[dstIdx + 0] = static_cast<uint16_t>(std::clamp<int>(static_cast<int>(r * gainR + 0.5), 0, 65535));
            rgb[dstIdx + 1] = static_cast<uint16_t>(std::clamp<int>(static_cast<int>(g + 0.5), 0, 65535));
            rgb[dstIdx + 2] = static_cast<uint16_t>(std::clamp<int>(static_cast<int>(b * gainB + 0.5), 0, 65535));
        }
    }

    return true;
}

double bilinearSamplePlane(const std::vector<uint16_t> &plane,
                           int width,
                           int height,
                           double x,
                           double y)
{
    if (plane.empty() || width <= 0 || height <= 0) {
        return 0.0;
    }

    x = std::clamp(x, 0.0, static_cast<double>(width - 1));
    y = std::clamp(y, 0.0, static_cast<double>(height - 1));

    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, width - 1);
    const int y1 = std::min(y0 + 1, height - 1);
    const double fx = x - static_cast<double>(x0);
    const double fy = y - static_cast<double>(y0);

    const double v00 = plane[static_cast<size_t>(y0) * static_cast<size_t>(width) + static_cast<size_t>(x0)];
    const double v10 = plane[static_cast<size_t>(y0) * static_cast<size_t>(width) + static_cast<size_t>(x1)];
    const double v01 = plane[static_cast<size_t>(y1) * static_cast<size_t>(width) + static_cast<size_t>(x0)];
    const double v11 = plane[static_cast<size_t>(y1) * static_cast<size_t>(width) + static_cast<size_t>(x1)];

    const double top = (1.0 - fx) * v00 + fx * v10;
    const double bottom = (1.0 - fx) * v01 + fx * v11;
    return (1.0 - fy) * top + fy * bottom;
}

double bilinearSamplePlaneD(const std::vector<double> &plane,
                            int width,
                            int height,
                            double x,
                            double y)
{
    if (plane.empty() || width <= 0 || height <= 0) {
        return 0.0;
    }

    x = std::clamp(x, 0.0, static_cast<double>(width - 1));
    y = std::clamp(y, 0.0, static_cast<double>(height - 1));

    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, width - 1);
    const int y1 = std::min(y0 + 1, height - 1);
    const double fx = x - static_cast<double>(x0);
    const double fy = y - static_cast<double>(y0);

    const double v00 = plane[static_cast<size_t>(y0) * static_cast<size_t>(width) + static_cast<size_t>(x0)];
    const double v10 = plane[static_cast<size_t>(y0) * static_cast<size_t>(width) + static_cast<size_t>(x1)];
    const double v01 = plane[static_cast<size_t>(y1) * static_cast<size_t>(width) + static_cast<size_t>(x0)];
    const double v11 = plane[static_cast<size_t>(y1) * static_cast<size_t>(width) + static_cast<size_t>(x1)];

    const double top = (1.0 - fx) * v00 + fx * v10;
    const double bottom = (1.0 - fx) * v01 + fx * v11;
    return (1.0 - fy) * top + fy * bottom;
}

bool buildPreviewImageFromRgb(const std::vector<uint16_t> &rgb,
                              int width,
                              int height,
                              const SuperCCDMetadata &metadata,
                              int maxSize,
                              QImage &preview,
                              QString &error)
{
    if (width <= 0 || height <= 0 || rgb.size() < static_cast<size_t>(width) * static_cast<size_t>(height) * 3) {
        error = QStringLiteral("Invalid RGB data for preview.");
        return false;
    }

    QImage image(width, height, QImage::Format_RGB32);
    if (image.isNull()) {
        error = QStringLiteral("Failed to allocate preview image.");
        return false;
    }

    double sumR = 0.0;
    double sumG = 0.0;
    double sumB = 0.0;
    uint32_t maxR = 1;
    uint32_t maxG = 1;
    uint32_t maxB = 1;
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    for (size_t i = 0; i < pixelCount; ++i) {
        const uint16_t r = rgb[i * 3 + 0];
        const uint16_t g = rgb[i * 3 + 1];
        const uint16_t b = rgb[i * 3 + 2];
        sumR += r;
        sumG += g;
        sumB += b;
        maxR = std::max(maxR, static_cast<uint32_t>(r));
        maxG = std::max(maxG, static_cast<uint32_t>(g));
        maxB = std::max(maxB, static_cast<uint32_t>(b));
    }

    const double avgR = sumR / static_cast<double>(pixelCount);
    const double avgG = sumG / static_cast<double>(pixelCount);
    const double avgB = sumB / static_cast<double>(pixelCount);
    double gainR = avgR > 0.0 ? avgG / avgR : 1.0;
    double gainG = 1.0;
    double gainB = avgB > 0.0 ? avgG / avgB : 1.0;
    if (metadata.hasAsShotNeutral &&
        metadata.asShotNeutral[0] > 0.0 &&
        metadata.asShotNeutral[1] > 0.0 &&
        metadata.asShotNeutral[2] > 0.0) {
        gainR = metadata.asShotNeutral[1] / metadata.asShotNeutral[0];
        gainG = 1.0;
        gainB = metadata.asShotNeutral[1] / metadata.asShotNeutral[2];
    }
    const double scaleR = 65535.0 / static_cast<double>(maxR);
    const double scaleG = 65535.0 / static_cast<double>(maxG);
    const double scaleB = 65535.0 / static_cast<double>(maxB);

    for (int y = 0; y < height; ++y) {
        QRgb *scanLine = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 3;
            const double linearR = std::clamp((static_cast<double>(rgb[idx + 0]) * gainR * scaleR) / 65535.0, 0.0, 1.0);
            const double linearG = std::clamp((static_cast<double>(rgb[idx + 1]) * gainG * scaleG) / 65535.0, 0.0, 1.0);
            const double linearB = std::clamp((static_cast<double>(rgb[idx + 2]) * gainB * scaleB) / 65535.0, 0.0, 1.0);
            const int outR = static_cast<int>(std::pow(linearR, 1.0 / 2.2) * 255.0 + 0.5);
            const int outG = static_cast<int>(std::pow(linearG, 1.0 / 2.2) * 255.0 + 0.5);
            const int outB = static_cast<int>(std::pow(linearB, 1.0 / 2.2) * 255.0 + 0.5);
            scanLine[x] = qRgb(std::clamp(outR, 0, 255),
                               std::clamp(outG, 0, 255),
                               std::clamp(outB, 0, 255));
        }
    }

    if (maxSize > 0 && (image.width() > maxSize || image.height() > maxSize)) {
        preview = image.scaled(maxSize, maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    } else {
        preview = image;
    }
    return true;
}

void rotateRgb90CW(std::vector<uint16_t> &rgb, int &width, int &height)
{
    if (width <= 0 || height <= 0 ||
        rgb.size() < static_cast<size_t>(width) * static_cast<size_t>(height) * 3) {
        return;
    }

    const int srcWidth = width;
    const int srcHeight = height;
    std::vector<uint16_t> rotated(static_cast<size_t>(srcWidth) * static_cast<size_t>(srcHeight) * 3, 0);

    for (int y = 0; y < srcHeight; ++y) {
        for (int x = 0; x < srcWidth; ++x) {
            const int dstX = srcHeight - 1 - y;
            const int dstY = x;
            const size_t srcIdx = (static_cast<size_t>(y) * static_cast<size_t>(srcWidth) + static_cast<size_t>(x)) * 3;
            const size_t dstIdx = (static_cast<size_t>(dstY) * static_cast<size_t>(srcHeight) + static_cast<size_t>(dstX)) * 3;
            rotated[dstIdx + 0] = rgb[srcIdx + 0];
            rotated[dstIdx + 1] = rgb[srcIdx + 1];
            rotated[dstIdx + 2] = rgb[srcIdx + 2];
        }
    }

    rgb.swap(rotated);
    width = srcHeight;
    height = srcWidth;
}

void mergeLinearPlane(const std::vector<uint16_t> &primary,
                      const std::vector<uint16_t> &secondary,
                      double rHeadroomScale,
                      double blendStart,
                      double blendEnd,
                      std::vector<double> &desired,
                      double &maxDesired,
                      double &gainOut)
{
    desired.assign(primary.size(), 0.0);
    maxDesired = 0.0;

    uint16_t white = 0;
    long double sumPrimary = 0.0;
    long double sumSecondary = 0.0;
    for (size_t i = 0; i < primary.size(); ++i) {
        white = std::max(white, primary[i]);
    }

    const double whiteScale = white > 0 ? static_cast<double>(white) : 1.0;
    for (size_t i = 0; i < primary.size(); ++i) {
        const uint16_t s = primary[i];
        const uint16_t r = secondary[i];
        if (s == 0 || r == 0) {
            continue;
        }
        const double normalized = static_cast<double>(s) / whiteScale;
        if (normalized < 0.10 || normalized > 0.60) {
            continue;
        }
        sumPrimary += s;
        sumSecondary += r;
    }

    gainOut = sumSecondary > 0.0 ? static_cast<double>(sumPrimary / sumSecondary) : 1.0;
    gainOut *= rHeadroomScale;

    for (size_t i = 0; i < primary.size(); ++i) {
        const double s = primary[i];
        const double r = secondary[i];
        if (s <= 0.0) {
            continue;
        }

        if (r <= 0.0) {
            desired[i] = s;
            if (s > maxDesired) {
                maxDesired = s;
            }
            continue;
        }

        const double normalized = s / whiteScale;
        const double scaledR = r * gainOut;
        double value = s;
        if (normalized >= blendEnd) {
            value = scaledR;
        } else if (normalized > blendStart) {
            const double t = (normalized - blendStart) / (blendEnd - blendStart);
            value = (1.0 - t) * s + t * scaledR;
        }
        desired[i] = value;
        if (value > maxDesired) {
            maxDesired = value;
        }
    }
}

void composeLinearRgbFromPlanes(const LinearShotPlanes &planes,
                                const std::vector<double> *mergedG1,
                                const std::vector<double> *mergedR,
                                const std::vector<double> *mergedB,
                                const std::vector<double> *mergedG2,
                                double outputScale,
                                std::vector<uint16_t> &rgb,
                                int &outWidth,
                                int &outHeight)
{
    outWidth = planes.expandedWidth * 2;
    outHeight = planes.height * 2;
    rgb.assign(static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight) * 3, 0);

    for (int y = 0; y < outHeight; ++y) {
        const double sy = static_cast<double>(y) * 0.5;
        for (int x = 0; x < outWidth; ++x) {
            const double sx = static_cast<double>(x) * 0.5;
            const double g1 = (mergedG1 ? sampleCompactPhasePlane(*mergedG1, planes.width, planes.height, 1, sx, sy)
                                        : sampleCompactPhasePlane(planes.g1, planes.width, planes.height, 1, sx, sy)) * outputScale;
            const double g2 = (mergedG2 ? sampleCompactPhasePlane(*mergedG2, planes.width, planes.height, 0, sx, sy)
                                        : sampleCompactPhasePlane(planes.g2, planes.width, planes.height, 0, sx, sy)) * outputScale;

            const double gLeft = ((mergedG1 ? sampleCompactPhasePlane(*mergedG1, planes.width, planes.height, 1, sx - 1.0, sy)
                                            : sampleCompactPhasePlane(planes.g1, planes.width, planes.height, 1, sx - 1.0, sy)) +
                                  (mergedG2 ? sampleCompactPhasePlane(*mergedG2, planes.width, planes.height, 0, sx - 1.0, sy)
                                            : sampleCompactPhasePlane(planes.g2, planes.width, planes.height, 0, sx - 1.0, sy))) * 0.5;
            const double gRight = ((mergedG1 ? sampleCompactPhasePlane(*mergedG1, planes.width, planes.height, 1, sx + 1.0, sy)
                                             : sampleCompactPhasePlane(planes.g1, planes.width, planes.height, 1, sx + 1.0, sy)) +
                                   (mergedG2 ? sampleCompactPhasePlane(*mergedG2, planes.width, planes.height, 0, sx + 1.0, sy)
                                             : sampleCompactPhasePlane(planes.g2, planes.width, planes.height, 0, sx + 1.0, sy))) * 0.5;
            const double gUp = ((mergedG1 ? sampleCompactPhasePlane(*mergedG1, planes.width, planes.height, 1, sx, sy - 1.0)
                                          : sampleCompactPhasePlane(planes.g1, planes.width, planes.height, 1, sx, sy - 1.0)) +
                                (mergedG2 ? sampleCompactPhasePlane(*mergedG2, planes.width, planes.height, 0, sx, sy - 1.0)
                                          : sampleCompactPhasePlane(planes.g2, planes.width, planes.height, 0, sx, sy - 1.0))) * 0.5;
            const double gDown = ((mergedG1 ? sampleCompactPhasePlane(*mergedG1, planes.width, planes.height, 1, sx, sy + 1.0)
                                            : sampleCompactPhasePlane(planes.g1, planes.width, planes.height, 1, sx, sy + 1.0)) +
                                  (mergedG2 ? sampleCompactPhasePlane(*mergedG2, planes.width, planes.height, 0, sx, sy + 1.0)
                                            : sampleCompactPhasePlane(planes.g2, planes.width, planes.height, 0, sx, sy + 1.0))) * 0.5;
            const double gradH = std::abs(gRight - gLeft);
            const double gradV = std::abs(gDown - gUp);
            const bool preferHorizontal = gradV > gradH;

            const double r = (mergedR ? sampleCompactPhasePlaneDirectional(*mergedR, planes.width, planes.height, 1, sx, sy, preferHorizontal)
                                      : sampleCompactPhasePlaneDirectional(planes.r, planes.width, planes.height, 1, sx, sy, preferHorizontal)) * outputScale;
            const double b = (mergedB ? sampleCompactPhasePlaneDirectional(*mergedB, planes.width, planes.height, 0, sx, sy, preferHorizontal)
                                      : sampleCompactPhasePlaneDirectional(planes.b, planes.width, planes.height, 0, sx, sy, preferHorizontal)) * outputScale;

            const size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(outWidth) + static_cast<size_t>(x)) * 3;
            rgb[idx + 0] = static_cast<uint16_t>(std::clamp<int>(static_cast<int>(r + 0.5), 0, 65535));
            rgb[idx + 1] = static_cast<uint16_t>(std::clamp<int>(static_cast<int>(((g1 + g2) * 0.5) + 0.5), 0, 65535));
            rgb[idx + 2] = static_cast<uint16_t>(std::clamp<int>(static_cast<int>(b + 0.5), 0, 65535));
        }
    }
}

void suppressLinearRgbColorFringing(std::vector<uint16_t> &rgb,
                                    int width,
                                    int height,
                                    double strength)
{
    if (strength <= 0.0) {
        return;
    }
    strength = std::clamp(strength, 0.0, 1.0);
    if (width <= 2 || height <= 2 || rgb.size() < static_cast<size_t>(width) * static_cast<size_t>(height) * 3) {
        return;
    }

    std::vector<uint16_t> source = rgb;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 3;
            const double centerG = static_cast<double>(source[idx + 1]);
            const double centerDr = static_cast<double>(source[idx + 0]) - centerG;
            const double centerDb = static_cast<double>(source[idx + 2]) - centerG;
            const double edgeThreshold = std::max(512.0, centerG * 0.025);

            double sumDr = 0.0;
            double sumDb = 0.0;
            double sumW = 0.0;

            static const int horizontalWeights[5] = {1, 3, 6, 3, 1};
            for (int kx = -2; kx <= 2; ++kx) {
                const int nx = std::clamp(x + kx, 0, width - 1);
                const size_t nidx = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(nx)) * 3;
                const double neighborG = static_cast<double>(source[nidx + 1]);
                if (std::abs(neighborG - centerG) > edgeThreshold) {
                    continue;
                }

                const double weight = static_cast<double>(horizontalWeights[kx + 2]);
                const double neighborDr = static_cast<double>(source[nidx + 0]) - neighborG;
                const double neighborDb = static_cast<double>(source[nidx + 2]) - neighborG;
                sumDr += neighborDr * weight;
                sumDb += neighborDb * weight;
                sumW += weight;
            }

            for (int ky = -1; ky <= 1; ++ky) {
                if (ky == 0) {
                    continue;
                }
                const int ny = std::clamp(y + ky, 0, height - 1);
                const size_t nidx = (static_cast<size_t>(ny) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 3;
                const double neighborG = static_cast<double>(source[nidx + 1]);
                if (std::abs(neighborG - centerG) > edgeThreshold) {
                    continue;
                }

                const double weight = 1.0;
                const double neighborDr = static_cast<double>(source[nidx + 0]) - neighborG;
                const double neighborDb = static_cast<double>(source[nidx + 2]) - neighborG;
                sumDr += neighborDr * weight;
                sumDb += neighborDb * weight;
                sumW += weight;
            }

            const double filteredDr = sumW > 0.0 ? (sumDr / sumW) : centerDr;
            const double filteredDb = sumW > 0.0 ? (sumDb / sumW) : centerDb;
            const double mixedDr = (1.0 - strength) * centerDr + strength * filteredDr;
            const double mixedDb = (1.0 - strength) * centerDb + strength * filteredDb;
            rgb[idx + 0] = static_cast<uint16_t>(std::clamp<int>(static_cast<int>(centerG + mixedDr + 0.5), 0, 65535));
            rgb[idx + 2] = static_cast<uint16_t>(std::clamp<int>(static_cast<int>(centerG + mixedDb + 0.5), 0, 65535));
        }
    }
}

bool readSelectedShotLinearPlanes12MP(const QString &inputPath,
                                      int shotSelect,
                                      LinearShotPlanes &planes,
                                      SuperCCDMetadata &metadata,
                                      QString &error)
{
    LibRaw raw;
    raw.imgdata.rawparams.shot_select = static_cast<unsigned>(shotSelect);

    int result = raw.open_file(inputPath.toUtf8().constData());
    if (result != LIBRAW_SUCCESS) {
        error = QString::fromUtf8(libraw_strerror(result));
        return false;
    }

    metadata.make = QString::fromUtf8(raw.imgdata.idata.make);
    metadata.model = QString::fromUtf8(raw.imgdata.idata.model);
    metadata.software = QString::fromUtf8(raw.imgdata.idata.software);
    metadata.dateTime = QString();
    metadata.iso = raw.imgdata.other.iso_speed;
    metadata.aperture = raw.imgdata.other.aperture;
    metadata.shutter = raw.imgdata.other.shutter;
    metadata.fujiWidth = raw.get_internal_data_pointer()->internal_output_params.fuji_width;
    if (raw.imgdata.other.timestamp > 0) {
        char dateBuf[32];
        struct tm *tm_info = localtime(&raw.imgdata.other.timestamp);
        strftime(dateBuf, sizeof(dateBuf), "%Y:%m:%d %H:%M:%S", tm_info);
        metadata.dateTime = QString::fromLatin1(dateBuf);
    }

    result = raw.unpack();
    if (result != LIBRAW_SUCCESS) {
        error = QString::fromUtf8(libraw_strerror(result));
        return false;
    }

    result = raw.raw2image();
    if (result != LIBRAW_SUCCESS) {
        error = QString::fromUtf8(libraw_strerror(result));
        return false;
    }

    if (raw.imgdata.image == nullptr) {
        error = QStringLiteral("Unable to access Fuji-rotated image buffer.");
        return false;
    }

    const int activeW = raw.imgdata.sizes.width;
    const int activeH = raw.imgdata.sizes.height;
    const int analysisWidth = activeW - (activeW % 2);
    const int analysisHeight = activeH - (activeH % 2);
    const int fujiWidth = raw.get_internal_data_pointer()->internal_output_params.fuji_width;

    int minDiagX = INT_MAX;
    int minDiagY = INT_MAX;
    int maxDiagX = -1;
    int maxDiagY = -1;

    for (int y = 0; y < analysisHeight; ++y) {
        for (int x = 0; x < analysisWidth; ++x) {
            const size_t srcIdx = static_cast<size_t>(y) * static_cast<size_t>(activeW) + static_cast<size_t>(x);
            for (int c = 0; c < 4; ++c) {
                if (raw.imgdata.image[srcIdx][c] == 0) {
                    continue;
                }
                const int diagX = (x + y) >> 1;
                const int diagY = ((y - x) >> 1) + fujiWidth;
                if (diagX < minDiagX) minDiagX = diagX;
                if (diagY < minDiagY) minDiagY = diagY;
                if (diagX > maxDiagX) maxDiagX = diagX;
                if (diagY > maxDiagY) maxDiagY = diagY;
            }
        }
    }

    if (minDiagX > maxDiagX || minDiagY > maxDiagY) {
        error = QStringLiteral("Unable to derive 12MP linear bounds.");
        return false;
    }

    const int diagWidth = maxDiagX - minDiagX + 1;
    const int diagHeight = maxDiagY - minDiagY + 1;
    const int planeWidth = ((diagWidth + 1) / 2) * 2;
    const int compactPlaneWidth = planeWidth / 2;

    std::vector<uint16_t> compactG1(static_cast<size_t>(compactPlaneWidth) * static_cast<size_t>(diagHeight));
    std::vector<uint16_t> compactR(static_cast<size_t>(compactPlaneWidth) * static_cast<size_t>(diagHeight));
    std::vector<uint16_t> compactB(static_cast<size_t>(compactPlaneWidth) * static_cast<size_t>(diagHeight));
    std::vector<uint16_t> compactG2(static_cast<size_t>(compactPlaneWidth) * static_cast<size_t>(diagHeight));
    std::vector<uint8_t> maskG1(static_cast<size_t>(compactPlaneWidth) * static_cast<size_t>(diagHeight));
    std::vector<uint8_t> maskR(static_cast<size_t>(compactPlaneWidth) * static_cast<size_t>(diagHeight));
    std::vector<uint8_t> maskB(static_cast<size_t>(compactPlaneWidth) * static_cast<size_t>(diagHeight));
    std::vector<uint8_t> maskG2(static_cast<size_t>(compactPlaneWidth) * static_cast<size_t>(diagHeight));

    for (int y = 0; y < analysisHeight; ++y) {
        for (int x = 0; x < analysisWidth; ++x) {
            const size_t srcIdx = static_cast<size_t>(y) * static_cast<size_t>(activeW) + static_cast<size_t>(x);
            const int diagX = ((x + y) >> 1) - minDiagX;
            const int diagY = (((y - x) >> 1) + fujiWidth) - minDiagY;
            if (diagX < 0 || diagX >= diagWidth || diagY < 0 || diagY >= diagHeight) {
                continue;
            }

            for (int c = 0; c < 4; ++c) {
                const uint16_t sample = raw.imgdata.image[srcIdx][c];
                if (sample == 0) {
                    continue;
                }

                std::vector<uint16_t> *plane = nullptr;
                std::vector<uint8_t> *mask = nullptr;
                int phaseBase = 0;
                switch (c) {
                case 3:
                    plane = &compactG1;
                    mask = &maskG1;
                    phaseBase = 1;
                    break;
                case 2:
                    plane = &compactB;
                    mask = &maskB;
                    phaseBase = 0;
                    break;
                case 0:
                    plane = &compactR;
                    mask = &maskR;
                    phaseBase = 1;
                    break;
                case 1:
                    plane = &compactG2;
                    mask = &maskG2;
                    phaseBase = 0;
                    break;
                default:
                    continue;
                }

                const int rowParity = diagY & 1;
                const int expectedParity = ((rowParity & 1) == 0) ? phaseBase : (1 - phaseBase);
                if ((diagX & 1) != expectedParity) {
                    continue;
                }

                const int compactX = (diagX - expectedParity) / 2;
                if (compactX < 0 || compactX >= compactPlaneWidth) {
                    continue;
                }

                const size_t dstIdx = static_cast<size_t>(diagY) * static_cast<size_t>(compactPlaneWidth) +
                                      static_cast<size_t>(compactX);
                (*plane)[dstIdx] = sample;
                (*mask)[dstIdx] = 1;
            }
        }
    }

    fillSparsePlane(compactG1, maskG1, compactPlaneWidth, diagHeight);
    fillSparsePlane(compactR, maskR, compactPlaneWidth, diagHeight);
    fillSparsePlane(compactB, maskB, compactPlaneWidth, diagHeight);
    fillSparsePlane(compactG2, maskG2, compactPlaneWidth, diagHeight);

    planes.g1 = std::move(compactG1);
    planes.r = std::move(compactR);
    planes.b = std::move(compactB);
    planes.g2 = std::move(compactG2);
    planes.width = compactPlaneWidth;
    planes.height = diagHeight;
    planes.expandedWidth = planeWidth;

    logProcessing("readSelectedShotLinearPlanes12MP completed: shot=%d planeWidth=%d planeHeight=%d outWidth=%d outHeight=%d",
                  shotSelect,
                  planeWidth,
                  diagHeight,
                  planeWidth * 2,
                  diagHeight * 2);
    return true;
}

void downsampleCfaAndChannels(const std::vector<uint16_t> &sourceCfa,
                              const std::vector<uint8_t> &sourceChannels,
                              int sourceWidth,
                              int sourceHeight,
                              int factor,
                              std::vector<uint16_t> &downCfa,
                              std::vector<uint8_t> &downChannels,
                              int &downWidth,
                              int &downHeight)
{
    if (factor < 2) {
        downCfa = sourceCfa;
        downChannels = sourceChannels;
        downWidth = sourceWidth;
        downHeight = sourceHeight;
        return;
    }

    downWidth = sourceWidth / factor;
    downHeight = sourceHeight / factor;
    downCfa.assign(static_cast<size_t>(downWidth) * static_cast<size_t>(downHeight), 0);
    downChannels.assign(static_cast<size_t>(downWidth) * static_cast<size_t>(downHeight), 0xFF);

    for (int y = 0; y < downHeight; ++y) {
        const int srcY = std::min(sourceHeight - 1, y * factor + (y & 1));
        for (int x = 0; x < downWidth; ++x) {
            const int srcX = std::min(sourceWidth - 1, x * factor + (x & 1));
            const size_t srcIdx = static_cast<size_t>(srcY) * static_cast<size_t>(sourceWidth) + static_cast<size_t>(srcX);
            const size_t dstIdx = static_cast<size_t>(y) * static_cast<size_t>(downWidth) + static_cast<size_t>(x);
            downCfa[dstIdx] = sourceCfa[srcIdx];
            downChannels[dstIdx] = sourceChannels[srcIdx];
        }
    }
}

PairStats computePairStats(const ushort *rawImage,
                           int rawWidth,
                           int leftMargin,
                           int topMargin,
                           int visibleWidth,
                           int visibleHeight,
                           int dx,
                           int dy)
{
    long double sumA = 0.0;
    long double sumB = 0.0;
    long double sumAA = 0.0;
    long double sumBB = 0.0;
    long double sumAB = 0.0;
    long double sumRatio = 0.0;
    long double sumAbsDiff = 0.0;
    uint64_t count = 0;

    for (int y = 0; y < visibleHeight; ++y) {
        const int ny = y + dy;
        if (ny < 0 || ny >= visibleHeight) {
            continue;
        }
        for (int x = 0; x < visibleWidth; ++x) {
            const int nx = x + dx;
            if (nx < 0 || nx >= visibleWidth) {
                continue;
            }

            const int rawRowA = y + topMargin;
            const int rawColA = x + leftMargin;
            const int rawRowB = ny + topMargin;
            const int rawColB = nx + leftMargin;
            const uint16_t a = rawImage[static_cast<size_t>(rawRowA) * static_cast<size_t>(rawWidth) +
                                        static_cast<size_t>(rawColA)];
            const uint16_t b = rawImage[static_cast<size_t>(rawRowB) * static_cast<size_t>(rawWidth) +
                                        static_cast<size_t>(rawColB)];
            if (a == 0 || b == 0) {
                continue;
            }

            count++;
            sumA += a;
            sumB += b;
            sumAA += static_cast<long double>(a) * static_cast<long double>(a);
            sumBB += static_cast<long double>(b) * static_cast<long double>(b);
            sumAB += static_cast<long double>(a) * static_cast<long double>(b);
            const uint16_t hi = a > b ? a : b;
            const uint16_t lo = a > b ? b : a;
            if (lo > 0) {
                sumRatio += static_cast<long double>(hi) / static_cast<long double>(lo);
            }
            sumAbsDiff += std::abs(static_cast<int>(a) - static_cast<int>(b));
        }
    }

    PairStats stats;
    stats.count = count;
    if (count == 0) {
        return stats;
    }

    const long double countLd = static_cast<long double>(count);
    stats.meanA = static_cast<double>(sumA / countLd);
    stats.meanB = static_cast<double>(sumB / countLd);
    stats.meanRatio = static_cast<double>(sumRatio / countLd);
    stats.meanAbsDiff = static_cast<double>(sumAbsDiff / countLd);

    const long double cov = (sumAB / countLd) - (sumA / countLd) * (sumB / countLd);
    const long double varA = (sumAA / countLd) - (sumA / countLd) * (sumA / countLd);
    const long double varB = (sumBB / countLd) - (sumB / countLd) * (sumB / countLd);
    if (varA > 0.0 && varB > 0.0) {
        stats.corr = static_cast<double>(cov / std::sqrt(varA * varB));
    }
    return stats;
}

PairStats computePairStatsSameColor(LibRaw &raw,
                                    const ushort *rawImage,
                                    int rawWidth,
                                    int leftMargin,
                                    int topMargin,
                                    int visibleWidth,
                                    int visibleHeight,
                                    int dx,
                                    int dy,
                                    int colorFilter)
{
    long double sumA = 0.0;
    long double sumB = 0.0;
    long double sumAA = 0.0;
    long double sumBB = 0.0;
    long double sumAB = 0.0;
    long double sumRatio = 0.0;
    long double sumAbsDiff = 0.0;
    uint64_t count = 0;

    for (int y = 0; y < visibleHeight; ++y) {
        const int ny = y + dy;
        if (ny < 0 || ny >= visibleHeight) {
            continue;
        }
        for (int x = 0; x < visibleWidth; ++x) {
            const int nx = x + dx;
            if (nx < 0 || nx >= visibleWidth) {
                continue;
            }

            const int rawRowA = y + topMargin;
            const int rawColA = x + leftMargin;
            const int rawRowB = ny + topMargin;
            const int rawColB = nx + leftMargin;
            const int colorA = raw.COLOR(rawRowA, rawColA);
            const int colorB = raw.COLOR(rawRowB, rawColB);
            if (colorA != colorB) {
                continue;
            }
            if (colorFilter >= 0 && colorA != colorFilter) {
                continue;
            }

            const uint16_t a = rawImage[static_cast<size_t>(rawRowA) * static_cast<size_t>(rawWidth) +
                                        static_cast<size_t>(rawColA)];
            const uint16_t b = rawImage[static_cast<size_t>(rawRowB) * static_cast<size_t>(rawWidth) +
                                        static_cast<size_t>(rawColB)];
            if (a == 0 || b == 0) {
                continue;
            }

            count++;
            sumA += a;
            sumB += b;
            sumAA += static_cast<long double>(a) * static_cast<long double>(a);
            sumBB += static_cast<long double>(b) * static_cast<long double>(b);
            sumAB += static_cast<long double>(a) * static_cast<long double>(b);
            const uint16_t hi = a > b ? a : b;
            const uint16_t lo = a > b ? b : a;
            if (lo > 0) {
                sumRatio += static_cast<long double>(hi) / static_cast<long double>(lo);
            }
            sumAbsDiff += std::abs(static_cast<int>(a) - static_cast<int>(b));
        }
    }

    PairStats stats;
    stats.count = count;
    if (count == 0) {
        return stats;
    }

    const long double countLd = static_cast<long double>(count);
    stats.meanA = static_cast<double>(sumA / countLd);
    stats.meanB = static_cast<double>(sumB / countLd);
    stats.meanRatio = static_cast<double>(sumRatio / countLd);
    stats.meanAbsDiff = static_cast<double>(sumAbsDiff / countLd);

    const long double cov = (sumAB / countLd) - (sumA / countLd) * (sumB / countLd);
    const long double varA = (sumAA / countLd) - (sumA / countLd) * (sumA / countLd);
    const long double varB = (sumBB / countLd) - (sumB / countLd) * (sumB / countLd);
    if (varA > 0.0 && varB > 0.0) {
        stats.corr = static_cast<double>(cov / std::sqrt(varA * varB));
    }
    return stats;
}

void logNativePairDirectionStats(const ushort *rawImage,
                                 int rawWidth,
                                 int leftMargin,
                                 int topMargin,
                                 int visibleWidth,
                                 int visibleHeight)
{
    struct DirectionSpec {
        const char *name;
        int dx;
        int dy;
    };
    const DirectionSpec directions[] = {
        {"right", 1, 0},
        {"down", 0, 1},
        {"down_right", 1, 1},
        {"down_left", -1, 1},
        {"right2", 2, 0},
        {"down2", 0, 2},
        {"diag_far_right", 2, 1},
        {"diag_far_left", -2, 1},
    };

    for (const DirectionSpec &direction : directions) {
        const PairStats stats = computePairStats(rawImage,
                                                 rawWidth,
                                                 leftMargin,
                                                 topMargin,
                                                 visibleWidth,
                                                 visibleHeight,
                                                 direction.dx,
                                                 direction.dy);
        logProcessing("Native pair stats %s dx=%d dy=%d count=%llu meanA=%.2f meanB=%.2f meanRatio=%.4f meanAbsDiff=%.2f corr=%.6f",
                      direction.name,
                      direction.dx,
                      direction.dy,
                      static_cast<unsigned long long>(stats.count),
                      stats.meanA,
                      stats.meanB,
                      stats.meanRatio,
                      stats.meanAbsDiff,
                      stats.corr);
    }
}

void logNativeSameColorPairDirectionStats(LibRaw &raw,
                                          const ushort *rawImage,
                                          int rawWidth,
                                          int leftMargin,
                                          int topMargin,
                                          int visibleWidth,
                                          int visibleHeight)
{
    struct DirectionSpec {
        const char *name;
        int dx;
        int dy;
    };
    const DirectionSpec directions[] = {
        {"right", 1, 0},
        {"down", 0, 1},
        {"down_right", 1, 1},
        {"down_left", -1, 1},
        {"right2", 2, 0},
        {"down2", 0, 2},
        {"diag_far_right", 2, 1},
        {"diag_far_left", -2, 1},
    };
    const char *colorNames[] = {"R", "G1", "B", "G2"};

    for (const DirectionSpec &direction : directions) {
        const PairStats allStats = computePairStatsSameColor(raw,
                                                             rawImage,
                                                             rawWidth,
                                                             leftMargin,
                                                             topMargin,
                                                             visibleWidth,
                                                             visibleHeight,
                                                             direction.dx,
                                                             direction.dy,
                                                             -1);
        logProcessing("Same-color pair stats all %s dx=%d dy=%d count=%llu meanRatio=%.4f meanAbsDiff=%.2f corr=%.6f",
                      direction.name,
                      direction.dx,
                      direction.dy,
                      static_cast<unsigned long long>(allStats.count),
                      allStats.meanRatio,
                      allStats.meanAbsDiff,
                      allStats.corr);

        for (int color = 0; color < 4; ++color) {
            const PairStats colorStats = computePairStatsSameColor(raw,
                                                                   rawImage,
                                                                   rawWidth,
                                                                   leftMargin,
                                                                   topMargin,
                                                                   visibleWidth,
                                                                   visibleHeight,
                                                                   direction.dx,
                                                                   direction.dy,
                                                                   color);
            logProcessing("Same-color pair stats %s %s dx=%d dy=%d count=%llu meanRatio=%.4f meanAbsDiff=%.2f corr=%.6f",
                          colorNames[color],
                          direction.name,
                          direction.dx,
                          direction.dy,
                          static_cast<unsigned long long>(colorStats.count),
                          colorStats.meanRatio,
                          colorStats.meanAbsDiff,
                          colorStats.corr);
        }
    }
}

void fillSparsePlane(std::vector<uint16_t> &plane,
                     const std::vector<uint8_t> &mask,
                     int planeWidth,
                     int planeHeight)
{
    const int diagonalOffsets[4][2] = {
        {-1, -1}, {1, -1}, {-1, 1}, {1, 1}
    };
    const int horizontalOffsets[2][2] = {
        {-1, 0}, {1, 0}
    };
    const int verticalOffsets[2][2] = {
        {0, -1}, {0, 1}
    };

    for (int y = 0; y < planeHeight; ++y) {
        for (int x = 0; x < planeWidth; ++x) {
            const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(planeWidth) +
                               static_cast<size_t>(x);
            if (mask[idx]) {
                continue;
            }

            uint16_t value = averageSamples(plane, mask, planeWidth, planeHeight,
                                            diagonalOffsets, 4, x, y);
            if (value == 0) {
                value = averageSamples(plane, mask, planeWidth, planeHeight,
                                       horizontalOffsets, 2, x, y);
            }
            if (value == 0) {
                value = averageSamples(plane, mask, planeWidth, planeHeight,
                                       verticalOffsets, 2, x, y);
            }
            plane[idx] = value;
        }
    }
}
}

bool SuperCCDProcessor::extractEmbeddedThumbnail(const QString &inputPath,
                                                 QImage &thumbnail,
                                                 QString *error)
{
    thumbnail = QImage();

    LibRaw raw;
    int ret = raw.open_file(inputPath.toUtf8().constData());
    if (ret != LIBRAW_SUCCESS) {
        if (error) {
            *error = QStringLiteral("Unable to open RAF for thumbnail extraction: %1")
                         .arg(QString::fromUtf8(libraw_strerror(ret)));
        }
        return false;
    }

    const int thumbCount = std::max(1, raw.imgdata.thumbs_list.thumbcount);
    int bestArea = -1;
    for (int i = 0; i < thumbCount; ++i) {
        ret = (i == 0) ? raw.unpack_thumb() : raw.unpack_thumb_ex(i);
        if (ret != LIBRAW_SUCCESS) {
            continue;
        }

        int thumbErr = LIBRAW_SUCCESS;
        libraw_processed_image_t *thumb = raw.dcraw_make_mem_thumb(&thumbErr);
        if (!thumb || thumbErr != LIBRAW_SUCCESS) {
            if (thumb) {
                LibRaw::dcraw_clear_mem(thumb);
            }
            continue;
        }

        QImage candidate = qImageFromLibRawProcessedThumb(thumb);
        LibRaw::dcraw_clear_mem(thumb);
        if (!candidate.isNull()) {
            const int area = candidate.width() * candidate.height();
            if (area > bestArea) {
                bestArea = area;
                thumbnail = candidate;
            }
        }
    }

    raw.recycle();

    if (thumbnail.isNull()) {
        thumbnail = extractThumbnailWithExifTool(inputPath);
        if (!thumbnail.isNull()) {
            return true;
        }
        if (error) {
            *error = QStringLiteral("RAF thumbnail extraction failed.");
        }
        return false;
    }

    return true;
}

bool SuperCCDProcessor::process(const QString &inputPath,
                                const QString &outputPath,
                                const ConversionSettings &settings,
                                QString &error)
{
    logProcessing("process start: %s -> %s", inputPath.toUtf8().constData(), outputPath.toUtf8().constData());
    const char *modeLabel = "6MP CFA";
    switch (settings.exportMode) {
    case ExportMode::RawCfa6MP:
        modeLabel = "6MP CFA";
        break;
    case ExportMode::Linear12MPExperimental:
        modeLabel = "12MP linear experimental";
        break;
    }
    logProcessing("Selected-shot export mode: %s", modeLabel);

    if (settings.exportMode == ExportMode::Linear12MPExperimental) {
        LinearShotPlanes sPlanes;
        LinearShotPlanes rPlanes;
        SuperCCDMetadata metadata;
        SuperCCDMetadata rMetadata;
        if (!readSelectedShotLinearPlanes12MP(inputPath, 0, sPlanes, metadata, error)) {
            return false;
        }
        if (!readSelectedShotLinearPlanes12MP(inputPath, 1, rPlanes, rMetadata, error)) {
            return false;
        }
        if (metadata.embeddedThumbnail.isNull()) {
            extractEmbeddedThumbnail(inputPath, metadata.embeddedThumbnail, nullptr);
        }
        if (sPlanes.width != rPlanes.width || sPlanes.height != rPlanes.height) {
            error = QStringLiteral("S and R linear plane dimensions do not match.");
            return false;
        }

        std::vector<double> mergedG1;
        std::vector<double> mergedR;
        std::vector<double> mergedB;
        std::vector<double> mergedG2;
        double gainG1 = 1.0;
        double gainR = 1.0;
        double gainB = 1.0;
        double gainG2 = 1.0;
        double maxG1 = 0.0;
        double maxR = 0.0;
        double maxB = 0.0;
        double maxG2 = 0.0;
        mergeLinearPlane(sPlanes.g1, rPlanes.g1, settings.rHeadroomScale, 0.95, 0.995, mergedG1, maxG1, gainG1);
        mergeLinearPlane(sPlanes.r, rPlanes.r, settings.rHeadroomScale, 0.95, 0.995, mergedR, maxR, gainR);
        mergeLinearPlane(sPlanes.b, rPlanes.b, settings.rHeadroomScale, 0.95, 0.995, mergedB, maxB, gainB);
        mergeLinearPlane(sPlanes.g2, rPlanes.g2, settings.rHeadroomScale, 0.95, 0.995, mergedG2, maxG2, gainG2);

        double outputScale = 1.0;
        const double maxDesired = std::max(std::max(maxG1, maxR), std::max(maxB, maxG2));
        if (maxDesired > 65535.0) {
            outputScale = 65535.0 / maxDesired;
        }
        logProcessing("12MP linear merge gains: g1=%0.5f r=%0.5f b=%0.5f g2=%0.5f maxDesired=%0.2f outputScale=%0.6f",
                      gainG1, gainR, gainB, gainG2, maxDesired, outputScale);

        std::vector<uint16_t> sRgb;
        std::vector<uint16_t> rRgb;
        std::vector<uint16_t> mergedRgb;
        int sWidth = 0;
        int sHeight = 0;
        int rWidth = 0;
        int rHeight = 0;
        int mergedWidth = 0;
        int mergedHeight = 0;
        composeLinearRgbFromPlanes(sPlanes, nullptr, nullptr, nullptr, nullptr, 1.0, sRgb, sWidth, sHeight);
        composeLinearRgbFromPlanes(rPlanes, nullptr, nullptr, nullptr, nullptr, 1.0, rRgb, rWidth, rHeight);
        if (sWidth != rWidth || sHeight != rHeight) {
            error = QStringLiteral("Linear RGB output dimensions do not match.");
            return false;
        }
        composeLinearRgbFromPlanes(sPlanes, &mergedG1, &mergedR, &mergedB, &mergedG2, outputScale, mergedRgb, mergedWidth, mergedHeight);
        if (sWidth != mergedWidth || sHeight != mergedHeight) {
            error = QStringLiteral("Merged linear RGB output dimensions do not match.");
            return false;
        }
        suppressLinearRgbColorFringing(sRgb, sWidth, sHeight, settings.linearChromaSuppression);
        suppressLinearRgbColorFringing(rRgb, rWidth, rHeight, settings.linearChromaSuppression);
        suppressLinearRgbColorFringing(mergedRgb, mergedWidth, mergedHeight, settings.linearChromaSuppression);
        rotateRgb90CW(sRgb, sWidth, sHeight);
        rotateRgb90CW(rRgb, rWidth, rHeight);
        rotateRgb90CW(mergedRgb, mergedWidth, mergedHeight);
        if (sWidth != rWidth || sHeight != rHeight || sWidth != mergedWidth || sHeight != mergedHeight) {
            error = QStringLiteral("Rotated linear RGB output dimensions do not match.");
            return false;
        }

        struct OutputSpec {
            const char *suffix;
            const std::vector<uint16_t> *buffer;
        };
        const OutputSpec outputs[] = {
            {"_s_pixels.dng", &sRgb},
            {"_r_pixels.dng", &rRgb},
            {"_sr_merged.dng", &mergedRgb},
        };

        for (const OutputSpec &outputSpec : outputs) {
            QString planeOutputPath = outputPath;
            if (planeOutputPath.endsWith(QLatin1String(".dng"), Qt::CaseInsensitive)) {
                planeOutputPath.chop(4);
                planeOutputPath += QString::fromLatin1(outputSpec.suffix);
            } else {
                planeOutputPath += QString::fromLatin1(outputSpec.suffix);
            }
            if (!DngWriter::writeLinearDng(planeOutputPath, *outputSpec.buffer, sWidth, sHeight, 16, metadata, error)) {
                return false;
            }
        }
        return true;
    }

    if (!ensure6MPCache(inputPath, m_cfaPreviewCache, error)) {
        return false;
    }

    std::vector<uint16_t> mergedSr;
    QString mergeSummary;
    double appliedOutputScale = 1.0;
    mergePrimaryAndProjectedSecondary(m_cfaPreviewCache.sCfa,
                                     m_cfaPreviewCache.sChannels,
                                     m_cfaPreviewCache.projectedR,
                                     m_cfaPreviewCache.width,
                                     m_cfaPreviewCache.height,
                                     settings.rHeadroomScale,
                                     settings.rTransitionDelay,
                                     settings.rTransitionSmoothness,
                                     false,
                                     mergedSr,
                                     mergeSummary,
                                     &appliedOutputScale);
    logProcessing("S+R merge summary: %s", mergeSummary.toUtf8().constData());

    QString firstError;
    bool wroteAny = false;

    struct OutputSpec {
        const char *label;
        const char *suffix;
        const std::vector<uint16_t> *buffer;
    };

    const OutputSpec outputs[] = {
        {"S", "_s_pixels.dng", &m_cfaPreviewCache.sCfa},
        {"R", "_r_pixels.dng", &m_cfaPreviewCache.rCfa},
        {"SR", "_sr_merged.dng", &mergedSr},
    };

    for (const OutputSpec &outputSpec : outputs) {
        QString planeOutputPath = outputPath;
        if (planeOutputPath.endsWith(QLatin1String(".dng"), Qt::CaseInsensitive)) {
            planeOutputPath.chop(4);
            planeOutputPath += QString::fromLatin1(outputSpec.suffix);
        } else {
            planeOutputPath += QString::fromLatin1(outputSpec.suffix);
        }

        SuperCCDMetadata outputMetadata = m_cfaPreviewCache.metadata;
        uint16_t outputWhiteLevel = 0;
        for (uint16_t sample : *outputSpec.buffer) {
            if (sample > outputWhiteLevel) {
                outputWhiteLevel = sample;
            }
        }
        outputMetadata.whiteLevel = outputWhiteLevel;
        if (std::strcmp(outputSpec.label, "SR") == 0) {
            const double sourceWhiteLevel = m_cfaPreviewCache.metadata.whiteLevel > 0
                ? static_cast<double>(m_cfaPreviewCache.metadata.whiteLevel)
                : 1.0;
            outputMetadata.baselineExposure = 0.0;
            outputMetadata.hasBaselineExposure = false;
            logProcessing("merged CFA metadata before write: sourceWhite=%.6f outputWhite=%u appliedScale=%.9f baselineExposure=%.6f hasBaselineExposure=%d",
                          sourceWhiteLevel,
                          outputWhiteLevel,
                          appliedOutputScale,
                          outputMetadata.baselineExposure,
                          outputMetadata.hasBaselineExposure ? 1 : 0);
        }

        logProcessing("starting DngWriter::writeDng: %s (%dx%d)",
                      planeOutputPath.toUtf8().constData(),
                      m_cfaPreviewCache.width,
                      m_cfaPreviewCache.height);
        if (!DngWriter::writeDng(planeOutputPath,
                                 *outputSpec.buffer,
                                 m_cfaPreviewCache.width,
                                 m_cfaPreviewCache.height,
                                 m_cfaPreviewCache.bitDepth,
                                 outputMetadata,
                                 error)) {
            logProcessing("writeDng failed for %s: %s",
                          outputSpec.label,
                          error.toUtf8().constData());
            if (firstError.isEmpty()) {
                firstError = error;
            }
            continue;
        }

        wroteAny = true;
        logProcessing("DngWriter::writeDng returned successfully for %s", planeOutputPath.toUtf8().constData());
    }

    if (!wroteAny) {
        error = firstError.isEmpty() ? QStringLiteral("No plane exports succeeded.") : firstError;
        return false;
    }

    return true;
}

bool SuperCCDProcessor::renderPreview(const QString &inputPath,
                                      const ConversionSettings &settings,
                                      QImage &preview,
                                      QString &error)
{
    if (settings.exportMode == ExportMode::Linear12MPExperimental) {
        LinearShotPlanes sPlanes;
        LinearShotPlanes rPlanes;
        SuperCCDMetadata metadata;
        SuperCCDMetadata rMetadata;
        if (!readSelectedShotLinearPlanes12MP(inputPath, 0, sPlanes, metadata, error)) {
            return false;
        }
        if (!readSelectedShotLinearPlanes12MP(inputPath, 1, rPlanes, rMetadata, error)) {
            return false;
        }
        if (sPlanes.width != rPlanes.width || sPlanes.height != rPlanes.height) {
            error = QStringLiteral("S and R linear plane dimensions do not match.");
            return false;
        }

        std::vector<double> mergedG1;
        std::vector<double> mergedR;
        std::vector<double> mergedB;
        std::vector<double> mergedG2;
        double gainG1 = 1.0;
        double gainR = 1.0;
        double gainB = 1.0;
        double gainG2 = 1.0;
        double maxG1 = 0.0;
        double maxR = 0.0;
        double maxB = 0.0;
        double maxG2 = 0.0;
        mergeLinearPlane(sPlanes.g1, rPlanes.g1, settings.rHeadroomScale, 0.95, 0.995, mergedG1, maxG1, gainG1);
        mergeLinearPlane(sPlanes.r, rPlanes.r, settings.rHeadroomScale, 0.95, 0.995, mergedR, maxR, gainR);
        mergeLinearPlane(sPlanes.b, rPlanes.b, settings.rHeadroomScale, 0.95, 0.995, mergedB, maxB, gainB);
        mergeLinearPlane(sPlanes.g2, rPlanes.g2, settings.rHeadroomScale, 0.95, 0.995, mergedG2, maxG2, gainG2);

        const double maxDesired = std::max(std::max(maxG1, maxR), std::max(maxB, maxG2));
        const double outputScale = maxDesired > 65535.0 ? (65535.0 / maxDesired) : 1.0;

        std::vector<uint16_t> mergedRgb;
        int width = 0;
        int height = 0;
        composeLinearRgbFromPlanes(sPlanes, &mergedG1, &mergedR, &mergedB, &mergedG2, outputScale, mergedRgb, width, height);
        suppressLinearRgbColorFringing(mergedRgb, width, height, settings.linearChromaSuppression);
        rotateRgb90CW(mergedRgb, width, height);
        return buildPreviewImageFromRgb(mergedRgb, width, height, metadata, settings.previewMaxSize, preview, error);
    }

    if (!ensure6MPCache(inputPath, m_cfaPreviewCache, error)) {
        return false;
    }

    std::vector<uint16_t> mergedSr;
    QString mergeSummary;
    mergePrimaryAndProjectedSecondary(m_cfaPreviewCache.sCfa,
                                      m_cfaPreviewCache.sChannels,
                                      m_cfaPreviewCache.projectedR,
                                      m_cfaPreviewCache.width,
                                      m_cfaPreviewCache.height,
                                      settings.rHeadroomScale,
                                      settings.rTransitionDelay,
                                      settings.rTransitionSmoothness,
                                      false,
                                      mergedSr,
                                      mergeSummary);

    return buildPreviewImageFromCfa(mergedSr,
                                    m_cfaPreviewCache.width,
                                    m_cfaPreviewCache.height,
                                    m_cfaPreviewCache.metadata,
                                    settings.previewMaxSize,
                                    settings.previewRotation,
                                    preview,
                                    error);
}

bool SuperCCDProcessor::ensure6MPCache(const QString &inputPath,
                                       CfaPreviewCache &cache,
                                       QString &error)
{
    const QFileInfo fileInfo(inputPath);
    if (!fileInfo.exists()) {
        error = QStringLiteral("Input file does not exist.");
        return false;
    }

    const QDateTime lastModifiedUtc = fileInfo.lastModified().toUTC();
    const qint64 fileSize = fileInfo.size();
    if (cache.valid &&
        cache.inputPath == inputPath &&
        cache.lastModifiedUtc == lastModifiedUtc &&
        cache.fileSize == fileSize) {
        logProcessing("ensure6MPCache hit: %s", inputPath.toUtf8().constData());
        return true;
    }

    logProcessing("ensure6MPCache miss: %s", inputPath.toUtf8().constData());

    CfaPreviewCache rebuilt;
    rebuilt.inputPath = inputPath;
    rebuilt.lastModifiedUtc = lastModifiedUtc;
    rebuilt.fileSize = fileSize;

    int rWidth = 0;
    int rHeight = 0;
    int rBitDepth = 0;
    SuperCCDMetadata rMetadata;
    if (!readSelectedShotCfa(inputPath,
                             0,
                             false,
                             rebuilt.sCfa,
                             rebuilt.width,
                             rebuilt.height,
                             rebuilt.bitDepth,
                             rebuilt.metadata,
                             &rebuilt.sChannels,
                             error)) {
        return false;
    }
    if (!readSelectedShotCfa(inputPath,
                             1,
                             false,
                             rebuilt.rCfa,
                             rWidth,
                             rHeight,
                             rBitDepth,
                             rMetadata,
                             &rebuilt.rChannels,
                             error)) {
        return false;
    }
    if (rebuilt.width != rWidth || rebuilt.height != rHeight || rebuilt.bitDepth != rBitDepth) {
        error = QStringLiteral("S and R canvas dimensions do not match.");
        return false;
    }

    QString offsetSummary;
    projectSecondaryOntoPrimary(rebuilt.sCfa,
                                rebuilt.sChannels,
                                rebuilt.rCfa,
                                rebuilt.rChannels,
                                rebuilt.width,
                                rebuilt.height,
                                rebuilt.projectedR,
                                offsetSummary);
    logProcessing("ensure6MPCache projection offsets: %s", offsetSummary.toUtf8().constData());

    QImage embeddedThumb;
    if (extractEmbeddedThumbnail(inputPath, embeddedThumb, nullptr)) {
        rebuilt.metadata.embeddedThumbnail = embeddedThumb;
    }

    rebuilt.valid = true;
    cache = std::move(rebuilt);
    return true;
}

bool SuperCCDProcessor::readSelectedShotCfa(const QString &inputPath,
                                            int shotSelect,
                                            bool output12MP,
                                            std::vector<uint16_t> &cfaData,
                                            int &width,
                                            int &height,
                                            int &bitDepth,
                                            SuperCCDMetadata &metadata,
                                            std::vector<uint8_t> *channelMap,
                                            QString &error)
{
    logProcessing("readSelectedShotCfa open_file: %s shot=%d",
                  inputPath.toUtf8().constData(),
                  shotSelect);

    LibRaw raw;
    raw.imgdata.rawparams.shot_select = static_cast<unsigned>(shotSelect);

    int result = raw.open_file(inputPath.toUtf8().constData());
    if (result != LIBRAW_SUCCESS) {
        error = QString::fromUtf8(libraw_strerror(result));
        return false;
    }

    logProcessing("readSelectedShotCfa identified: shot=%d raw_count=%u width=%u height=%u maximum=%u",
                  shotSelect,
                  raw.imgdata.idata.raw_count,
                  raw.imgdata.sizes.width,
                  raw.imgdata.sizes.height,
                  raw.imgdata.color.maximum);

    metadata.make = QString::fromUtf8(raw.imgdata.idata.make);
    metadata.model = QString::fromUtf8(raw.imgdata.idata.model);
    metadata.software = QString::fromUtf8(raw.imgdata.idata.software);
    metadata.dateTime = QString();
    metadata.iso = raw.imgdata.other.iso_speed;
    metadata.aperture = raw.imgdata.other.aperture;
    metadata.shutter = raw.imgdata.other.shutter;
    metadata.fujiWidth = raw.get_internal_data_pointer()->internal_output_params.fuji_width;
    if (raw.imgdata.other.timestamp > 0) {
        char dateBuf[32];
        struct tm *tm_info = localtime(&raw.imgdata.other.timestamp);
        strftime(dateBuf, sizeof(dateBuf), "%Y:%m:%d %H:%M:%S", tm_info);
        metadata.dateTime = QString::fromLatin1(dateBuf);
    }

    result = raw.unpack();
    if (result != LIBRAW_SUCCESS) {
        error = QString::fromUtf8(libraw_strerror(result));
        return false;
    }

    if (raw.imgdata.rawdata.raw_image == nullptr) {
        error = QStringLiteral("Unable to access native raw image buffer.");
        return false;
    }

    metadata.whiteLevel = static_cast<uint16_t>(std::min<unsigned>(raw.imgdata.color.maximum, 65535u));

    const auto &color = raw.imgdata.color;
    const auto &dngLevels = raw.imgdata.color.dng_levels;
    const double blackR = dngLevels.dng_black ? static_cast<double>(dngLevels.dng_black)
                                              : static_cast<double>(color.cblack[0] ? color.cblack[0] : color.black);
    const double blackG2 = dngLevels.dng_black ? static_cast<double>(dngLevels.dng_black)
                                               : static_cast<double>(color.cblack[1] ? color.cblack[1] : color.black);
    const double blackB = dngLevels.dng_black ? static_cast<double>(dngLevels.dng_black)
                                              : static_cast<double>(color.cblack[2] ? color.cblack[2] : color.black);
    const double blackG1 = dngLevels.dng_black ? static_cast<double>(dngLevels.dng_black)
                                               : static_cast<double>(color.cblack[3] ? color.cblack[3] : color.black);
    metadata.blackLevels = {blackG1, blackB, blackR, blackG2};
    metadata.hasBlackLevels = (blackG1 > 0.0 || blackB > 0.0 || blackR > 0.0 || blackG2 > 0.0);

    double neutralR = 0.0;
    double neutralG = 0.0;
    double neutralB = 0.0;
    if (color.cam_mul[0] > 0.0f && color.cam_mul[1] > 0.0f && color.cam_mul[2] > 0.0f) {
        neutralR = 1.0 / static_cast<double>(color.cam_mul[0]);
        const double neutralG1 = 1.0 / static_cast<double>(color.cam_mul[1]);
        neutralB = 1.0 / static_cast<double>(color.cam_mul[2]);
        const double g2Mul = color.cam_mul[3] > 0.0f ? color.cam_mul[3] : color.cam_mul[1];
        const double neutralG2 = 1.0 / static_cast<double>(g2Mul);
        neutralG = 0.5 * (neutralG1 + neutralG2);
    }
    if (neutralR > 0.0 && neutralG > 0.0 && neutralB > 0.0) {
        const double scale = 1.0 / neutralG;
        metadata.asShotNeutral = {neutralR * scale, 1.0, neutralB * scale};
        metadata.hasAsShotNeutral = true;
    }

    bool hasColorMatrix = false;
    std::array<double, 9> collapsedColorMatrix = {
        1.0, 0.0, 0.0,
        0.0, 1.0, 0.0,
        0.0, 0.0, 1.0
    };
    const bool haveDngColor = raw.imgdata.color.dng_color[0].parsedfields != 0;
    if (haveDngColor) {
        for (int xyz = 0; xyz < 3; ++xyz) {
            collapsedColorMatrix[xyz + 0] = static_cast<double>(raw.imgdata.color.dng_color[0].colormatrix[0][xyz]);
            collapsedColorMatrix[xyz + 3] = 0.5 * (
                static_cast<double>(raw.imgdata.color.dng_color[0].colormatrix[1][xyz]) +
                static_cast<double>(raw.imgdata.color.dng_color[0].colormatrix[3][xyz]));
            collapsedColorMatrix[xyz + 6] = static_cast<double>(raw.imgdata.color.dng_color[0].colormatrix[2][xyz]);
        }
        hasColorMatrix = true;
    } else {
        bool anyCamXyz = false;
        for (int row = 0; row < 4; ++row) {
            for (int xyz = 0; xyz < 3; ++xyz) {
                if (std::abs(static_cast<double>(color.cam_xyz[row][xyz])) > 1e-6) {
                    anyCamXyz = true;
                    break;
                }
            }
            if (anyCamXyz) {
                break;
            }
        }
        if (anyCamXyz) {
            for (int xyz = 0; xyz < 3; ++xyz) {
                collapsedColorMatrix[xyz + 0] = static_cast<double>(color.cam_xyz[0][xyz]);
                collapsedColorMatrix[xyz + 3] = 0.5 * (
                    static_cast<double>(color.cam_xyz[1][xyz]) +
                    static_cast<double>(color.cam_xyz[3][xyz]));
                collapsedColorMatrix[xyz + 6] = static_cast<double>(color.cam_xyz[2][xyz]);
            }
            hasColorMatrix = true;
        }
    }
    metadata.colorMatrix1 = collapsedColorMatrix;
    metadata.hasColorMatrix1 = hasColorMatrix;

    logProcessing("selected shot metadata: shot=%d white=%u black=[%.2f %.2f %.2f %.2f] neutral=[%.5f %.5f %.5f] colorMatrix=%d",
                  shotSelect,
                  metadata.whiteLevel,
                  metadata.blackLevels[0],
                  metadata.blackLevels[1],
                  metadata.blackLevels[2],
                  metadata.blackLevels[3],
                  metadata.asShotNeutral[0],
                  metadata.asShotNeutral[1],
                  metadata.asShotNeutral[2],
                  metadata.hasColorMatrix1 ? 1 : 0);

    result = raw.raw2image();
    if (result != LIBRAW_SUCCESS) {
        error = QString::fromUtf8(libraw_strerror(result));
        return false;
    }

    if (raw.imgdata.image == nullptr) {
        error = QStringLiteral("Unable to access Fuji-rotated image buffer.");
        return false;
    }

    bitDepth = 16;

    if (!output12MP) {
        width = raw.imgdata.sizes.iwidth;
        height = raw.imgdata.sizes.iheight;
        if (width <= 0 || height <= 0) {
            error = QStringLiteral("Invalid rotated image dimensions for selected shot.");
            return false;
        }

        cfaData.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
        if (channelMap) {
            channelMap->assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0xFF);
        }

        uint16_t minValue = 65535;
        uint16_t maxValue = 0;
        uint64_t nonZero = 0;
        const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
        for (size_t i = 0; i < pixelCount; ++i) {
            uint16_t value = 0;
            for (int c = 0; c < 4; ++c) {
                const uint16_t sample = raw.imgdata.image[i][c];
                if (sample != 0) {
                    value = sample;
                    if (channelMap) {
                        (*channelMap)[i] = static_cast<uint8_t>(c);
                    }
                    break;
                }
            }

            if (value != 0) {
                nonZero++;
                if (value < minValue) minValue = value;
                if (value > maxValue) maxValue = value;
            }
            cfaData[i] = value;
        }

        logProcessing("readSelectedShotCfa completed: shot=%d mode=6MP width=%d height=%d nonZero=%llu range=%u..%u",
                      shotSelect,
                      width,
                      height,
                      static_cast<unsigned long long>(nonZero),
                      minValue,
                      maxValue);
        return true;
    }

    const int activeW = raw.imgdata.sizes.width;
    const int activeH = raw.imgdata.sizes.height;
    const int analysisWidth = activeW - (activeW % 2);
    const int analysisHeight = activeH - (activeH % 2);
    const int fujiWidth = raw.get_internal_data_pointer()->internal_output_params.fuji_width;
    int minDiagX = INT_MAX;
    int minDiagY = INT_MAX;
    int maxDiagX = -1;
    int maxDiagY = -1;

    for (int y = 0; y < analysisHeight; ++y) {
        for (int x = 0; x < analysisWidth; ++x) {
            const size_t srcIdx = static_cast<size_t>(y) * static_cast<size_t>(activeW) + static_cast<size_t>(x);
            for (int c = 0; c < 4; ++c) {
                if (raw.imgdata.image[srcIdx][c] == 0) {
                    continue;
                }
                const int diagX = (x + y) >> 1;
                const int diagY = ((y - x) >> 1) + fujiWidth;
                if (diagX < minDiagX) minDiagX = diagX;
                if (diagY < minDiagY) minDiagY = diagY;
                if (diagX > maxDiagX) maxDiagX = diagX;
                if (diagY > maxDiagY) maxDiagY = diagY;
            }
        }
    }

    if (minDiagX > maxDiagX || minDiagY > maxDiagY) {
        error = QStringLiteral("Unable to derive 12MP SuperCCD bounds.");
        return false;
    }

    const int diagWidth = maxDiagX - minDiagX + 1;
    const int diagHeight = maxDiagY - minDiagY + 1;
    const int planeWidth = ((diagWidth + 1) / 2) * 2;
    const int compactPlaneWidth = planeWidth / 2;
    std::vector<uint16_t> compactG1(static_cast<size_t>(compactPlaneWidth) * static_cast<size_t>(diagHeight));
    std::vector<uint16_t> compactR(static_cast<size_t>(compactPlaneWidth) * static_cast<size_t>(diagHeight));
    std::vector<uint16_t> compactB(static_cast<size_t>(compactPlaneWidth) * static_cast<size_t>(diagHeight));
    std::vector<uint16_t> compactG2(static_cast<size_t>(compactPlaneWidth) * static_cast<size_t>(diagHeight));
    std::vector<uint8_t> maskG1(static_cast<size_t>(compactPlaneWidth) * static_cast<size_t>(diagHeight));
    std::vector<uint8_t> maskR(static_cast<size_t>(compactPlaneWidth) * static_cast<size_t>(diagHeight));
    std::vector<uint8_t> maskB(static_cast<size_t>(compactPlaneWidth) * static_cast<size_t>(diagHeight));
    std::vector<uint8_t> maskG2(static_cast<size_t>(compactPlaneWidth) * static_cast<size_t>(diagHeight));
    uint64_t packed = 0;
    uint64_t collisions = 0;
    uint64_t paritySkips = 0;

    for (int y = 0; y < analysisHeight; ++y) {
        for (int x = 0; x < analysisWidth; ++x) {
            const size_t srcIdx = static_cast<size_t>(y) * static_cast<size_t>(activeW) + static_cast<size_t>(x);
            const int diagX = ((x + y) >> 1) - minDiagX;
            const int diagY = (((y - x) >> 1) + fujiWidth) - minDiagY;
            if (diagX < 0 || diagX >= diagWidth || diagY < 0 || diagY >= diagHeight) {
                continue;
            }

            for (int c = 0; c < 4; ++c) {
                const uint16_t sample = raw.imgdata.image[srcIdx][c];
                if (sample == 0) {
                    continue;
                }

                const int rowParity = diagY & 1;
                std::vector<uint16_t> *plane = nullptr;
                std::vector<uint8_t> *mask = nullptr;
                int phaseBase = 0;
                switch (c) {
                case 3:
                    plane = &compactG1;
                    mask = &maskG1;
                    phaseBase = 1;
                    break;
                case 2:
                    plane = &compactB;
                    mask = &maskB;
                    phaseBase = 0;
                    break;
                case 0:
                    plane = &compactR;
                    mask = &maskR;
                    phaseBase = 1;
                    break;
                case 1:
                    plane = &compactG2;
                    mask = &maskG2;
                    phaseBase = 0;
                    break;
                default:
                    continue;
                }

                const int expectedParity = ((rowParity & 1) == 0) ? phaseBase : (1 - phaseBase);
                if ((diagX & 1) != expectedParity) {
                    paritySkips++;
                    continue;
                }

                const int compactX = (diagX - expectedParity) / 2;
                if (compactX < 0 || compactX >= compactPlaneWidth) {
                    continue;
                }

                const size_t dstIdx = static_cast<size_t>(diagY) * static_cast<size_t>(compactPlaneWidth) +
                                      static_cast<size_t>(compactX);
                if ((*mask)[dstIdx]) {
                    collisions++;
                }
                (*plane)[dstIdx] = sample;
                (*mask)[dstIdx] = 1;
                packed++;
            }
        }
    }

    std::vector<uint16_t> planeG1;
    std::vector<uint16_t> planeR;
    std::vector<uint16_t> planeB;
    std::vector<uint16_t> planeG2;
    expandDiagonalPlane(compactG1, maskG1, compactPlaneWidth, diagHeight, 1, planeG1, planeWidth);
    expandDiagonalPlane(compactR, maskR, compactPlaneWidth, diagHeight, 1, planeR, planeWidth);
    expandDiagonalPlane(compactB, maskB, compactPlaneWidth, diagHeight, 0, planeB, planeWidth);
    expandDiagonalPlane(compactG2, maskG2, compactPlaneWidth, diagHeight, 0, planeG2, planeWidth);

    width = planeWidth * 2;
    height = diagHeight * 2;
    cfaData.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
    if (channelMap) {
        channelMap->assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0xFF);
    }

    uint16_t minValue = 65535;
    uint16_t maxValue = 0;
    for (int py = 0; py < diagHeight; ++py) {
        for (int px = 0; px < planeWidth; ++px) {
            const size_t planeIdx = static_cast<size_t>(py) * static_cast<size_t>(planeWidth) +
                                    static_cast<size_t>(px);
            const size_t row0 = static_cast<size_t>(py * 2) * static_cast<size_t>(width);
            const size_t row1 = static_cast<size_t>(py * 2 + 1) * static_cast<size_t>(width);
            cfaData[row0 + static_cast<size_t>(px * 2)] = planeG1[planeIdx];
            cfaData[row0 + static_cast<size_t>(px * 2 + 1)] = planeB[planeIdx];
            cfaData[row1 + static_cast<size_t>(px * 2)] = planeR[planeIdx];
            cfaData[row1 + static_cast<size_t>(px * 2 + 1)] = planeG2[planeIdx];
            if (channelMap) {
                (*channelMap)[row0 + static_cast<size_t>(px * 2)] = 3;
                (*channelMap)[row0 + static_cast<size_t>(px * 2 + 1)] = 2;
                (*channelMap)[row1 + static_cast<size_t>(px * 2)] = 0;
                (*channelMap)[row1 + static_cast<size_t>(px * 2 + 1)] = 1;
            }
            const uint16_t blockValues[4] = {
                planeG1[planeIdx], planeB[planeIdx], planeR[planeIdx], planeG2[planeIdx]
            };
            for (uint16_t value : blockValues) {
                if (value == 0) {
                    continue;
                }
                if (value < minValue) minValue = value;
                if (value > maxValue) maxValue = value;
            }
        }
    }

    logProcessing("readSelectedShotCfa completed: shot=%d mode=12MP width=%d height=%d packed=%llu collisions=%llu paritySkips=%llu range=%u..%u",
                  shotSelect,
                  width,
                  height,
                  static_cast<unsigned long long>(packed),
                  static_cast<unsigned long long>(collisions),
                  static_cast<unsigned long long>(paritySkips),
                  minValue,
                  maxValue);
    return true;
}

bool SuperCCDProcessor::readNativeRawPartitionRgb(const QString &inputPath,
                                                  int partitionMode,
                                                  int partitionClass,
                                                  std::vector<uint16_t> &rgbData,
                                                  int &width,
                                                  int &height,
                                                  int &bitDepth,
                                                  SuperCCDMetadata &metadata,
                                                  QString &error)
{
    logProcessing("readNativeRawPartitionRgb open_file: %s mode=%d class=%d",
                  inputPath.toUtf8().constData(),
                  partitionMode,
                  partitionClass);

    LibRaw raw;
    int result = raw.open_file(inputPath.toUtf8().constData());
    if (result != LIBRAW_SUCCESS) {
        error = QString::fromUtf8(libraw_strerror(result));
        return false;
    }

    metadata.make = QString::fromUtf8(raw.imgdata.idata.make);
    metadata.model = QString::fromUtf8(raw.imgdata.idata.model);
    metadata.software = QString::fromUtf8(raw.imgdata.idata.software);
    metadata.dateTime = QString();
    metadata.iso = raw.imgdata.other.iso_speed;
    metadata.aperture = raw.imgdata.other.aperture;
    metadata.shutter = raw.imgdata.other.shutter;
    if (raw.imgdata.other.timestamp > 0) {
        char dateBuf[32];
        struct tm *tm_info = localtime(&raw.imgdata.other.timestamp);
        strftime(dateBuf, sizeof(dateBuf), "%Y:%m:%d %H:%M:%S", tm_info);
        metadata.dateTime = QString::fromLatin1(dateBuf);
    }

    result = raw.unpack();
    if (result != LIBRAW_SUCCESS) {
        error = QString::fromUtf8(libraw_strerror(result));
        return false;
    }

    if (raw.imgdata.rawdata.raw_image == nullptr) {
        error = QStringLiteral("Unable to access native raw image buffer.");
        return false;
    }

    const int rawWidth = raw.imgdata.rawdata.sizes.raw_width;
    const int rawHeight = raw.imgdata.rawdata.sizes.raw_height;
    const int topMargin = raw.imgdata.sizes.top_margin;
    const int leftMargin = raw.imgdata.sizes.left_margin;
    const int visibleWidth = rawWidth - (leftMargin * 2);
    const int visibleHeight = rawHeight - (topMargin * 2);
    if (visibleWidth <= 0 || visibleHeight <= 0) {
        error = QStringLiteral("Invalid visible raw dimensions.");
        return false;
    }

    if (partitionMode == 0 && partitionClass == 0) {
        logNativePairDirectionStats(raw.imgdata.rawdata.raw_image,
                                    rawWidth,
                                    leftMargin,
                                    topMargin,
                                    visibleWidth,
                                    visibleHeight);
        logProcessing("Entering same-color pair stats");
        logNativeSameColorPairDirectionStats(raw,
                                             raw.imgdata.rawdata.raw_image,
                                             rawWidth,
                                             leftMargin,
                                             topMargin,
                                             visibleWidth,
                                             visibleHeight);
        logProcessing("Leaving same-color pair stats");
    }

    std::vector<uint16_t> plane(static_cast<size_t>(visibleWidth) * static_cast<size_t>(visibleHeight));
    std::vector<uint8_t> mask(static_cast<size_t>(visibleWidth) * static_cast<size_t>(visibleHeight));
    uint64_t packed = 0;
    uint64_t nonZero = 0;

    for (int y = 0; y < visibleHeight; ++y) {
        for (int x = 0; x < visibleWidth; ++x) {
            const int rawRow = y + topMargin;
            const int rawCol = x + leftMargin;
            const size_t rawIdx = static_cast<size_t>(rawRow) * static_cast<size_t>(rawWidth) +
                                  static_cast<size_t>(rawCol);
            const uint16_t sample = raw.imgdata.rawdata.raw_image[rawIdx];
            if (sample == 0) {
                continue;
            }
            nonZero++;

            bool selected = false;
            if (partitionMode == 0) {
                selected = ((y & 1) == partitionClass);
            } else {
                selected = (((x + y) & 1) == partitionClass);
            }

            if (!selected) {
                continue;
            }

            const size_t dstIdx = static_cast<size_t>(y) * static_cast<size_t>(visibleWidth) +
                                  static_cast<size_t>(x);
            plane[dstIdx] = sample;
            mask[dstIdx] = 1;
            packed++;
        }
    }

    width = visibleWidth;
    height = visibleHeight;
    bitDepth = 16;
    rgbData.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 3);
    for (size_t i = 0; i < static_cast<size_t>(width) * static_cast<size_t>(height); ++i) {
        const uint16_t value = plane[i];
        rgbData[i * 3 + 0] = value;
        rgbData[i * 3 + 1] = value;
        rgbData[i * 3 + 2] = value;
    }

    logProcessing("readNativeRawPartitionRgb completed: mode=%d class=%d width=%d height=%d packed=%llu nonZero=%llu",
                  partitionMode,
                  partitionClass,
                  width,
                  height,
                  static_cast<unsigned long long>(packed),
                  static_cast<unsigned long long>(nonZero));
    return true;
}

bool SuperCCDProcessor::readNativeVerticalPairRgb(const QString &inputPath,
                                                  bool brighterSample,
                                                  int columnParity,
                                                  std::vector<uint16_t> &rgbData,
                                                  int &width,
                                                  int &height,
                                                  int &bitDepth,
                                                  SuperCCDMetadata &metadata,
                                                  QString &error)
{
    logProcessing("readNativeVerticalPairRgb open_file: %s brighter=%d columnParity=%d",
                  inputPath.toUtf8().constData(),
                  brighterSample ? 1 : 0,
                  columnParity);

    LibRaw raw;
    int result = raw.open_file(inputPath.toUtf8().constData());
    if (result != LIBRAW_SUCCESS) {
        error = QString::fromUtf8(libraw_strerror(result));
        return false;
    }

    metadata.make = QString::fromUtf8(raw.imgdata.idata.make);
    metadata.model = QString::fromUtf8(raw.imgdata.idata.model);
    metadata.software = QString::fromUtf8(raw.imgdata.idata.software);
    metadata.dateTime = QString();
    metadata.iso = raw.imgdata.other.iso_speed;
    metadata.aperture = raw.imgdata.other.aperture;
    metadata.shutter = raw.imgdata.other.shutter;
    if (raw.imgdata.other.timestamp > 0) {
        char dateBuf[32];
        struct tm *tm_info = localtime(&raw.imgdata.other.timestamp);
        strftime(dateBuf, sizeof(dateBuf), "%Y:%m:%d %H:%M:%S", tm_info);
        metadata.dateTime = QString::fromLatin1(dateBuf);
    }

    result = raw.unpack();
    if (result != LIBRAW_SUCCESS) {
        error = QString::fromUtf8(libraw_strerror(result));
        return false;
    }

    if (raw.imgdata.rawdata.raw_image == nullptr) {
        error = QStringLiteral("Unable to access native raw image buffer.");
        return false;
    }

    const int rawWidth = raw.imgdata.rawdata.sizes.raw_width;
    const int rawHeight = raw.imgdata.rawdata.sizes.raw_height;
    const int topMargin = raw.imgdata.sizes.top_margin;
    const int leftMargin = raw.imgdata.sizes.left_margin;
    const int visibleWidth = rawWidth - (leftMargin * 2);
    const int visibleHeight = rawHeight - (topMargin * 2);
    if (visibleWidth <= 0 || visibleHeight < 2) {
        error = QStringLiteral("Invalid visible raw dimensions for vertical pairing.");
        return false;
    }

    if (columnParity < 0 || columnParity > 1) {
        error = QStringLiteral("Invalid column parity for vertical pairing.");
        return false;
    }

    width = visibleWidth / 2;
    height = visibleHeight / 2;
    bitDepth = 16;
    std::vector<uint16_t> plane(static_cast<size_t>(width) * static_cast<size_t>(height));
    uint64_t pairCount = 0;

    for (int outY = 0; outY < height; ++outY) {
        const int y0 = outY * 2;
        const int y1 = y0 + 1;
        for (int outX = 0; outX < width; ++outX) {
            const int x = outX * 2 + columnParity;
            const int rawRowA = y0 + topMargin;
            const int rawRowB = y1 + topMargin;
            const int rawCol = x + leftMargin;
            const uint16_t a = raw.imgdata.rawdata.raw_image[static_cast<size_t>(rawRowA) * static_cast<size_t>(rawWidth) +
                                                             static_cast<size_t>(rawCol)];
            const uint16_t b = raw.imgdata.rawdata.raw_image[static_cast<size_t>(rawRowB) * static_cast<size_t>(rawWidth) +
                                                             static_cast<size_t>(rawCol)];
            if (a == 0 || b == 0) {
                continue;
            }

            plane[static_cast<size_t>(outY) * static_cast<size_t>(width) + static_cast<size_t>(outX)] =
                brighterSample ? (a > b ? a : b) : (a < b ? a : b);
            pairCount++;
        }
    }

    rgbData.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 3);
    for (size_t i = 0; i < static_cast<size_t>(width) * static_cast<size_t>(height); ++i) {
        const uint16_t value = plane[i];
        rgbData[i * 3 + 0] = value;
        rgbData[i * 3 + 1] = value;
        rgbData[i * 3 + 2] = value;
    }

    logProcessing("readNativeVerticalPairRgb completed: brighter=%d columnParity=%d width=%d height=%d pairs=%llu",
                  brighterSample ? 1 : 0,
                  columnParity,
                  width,
                  height,
                  static_cast<unsigned long long>(pairCount));
    return true;
}

bool SuperCCDProcessor::readNativeSameColorPairRgb(const QString &inputPath,
                                                   int dx,
                                                   int dy,
                                                   bool brighterSample,
                                                   std::vector<uint16_t> &rgbData,
                                                   int &width,
                                                   int &height,
                                                   int &bitDepth,
                                                   SuperCCDMetadata &metadata,
                                                   QString &error)
{
    logProcessing("readNativeSameColorPairRgb open_file: %s dx=%d dy=%d brighter=%d",
                  inputPath.toUtf8().constData(),
                  dx,
                  dy,
                  brighterSample ? 1 : 0);

    LibRaw raw;
    int result = raw.open_file(inputPath.toUtf8().constData());
    if (result != LIBRAW_SUCCESS) {
        error = QString::fromUtf8(libraw_strerror(result));
        return false;
    }

    metadata.make = QString::fromUtf8(raw.imgdata.idata.make);
    metadata.model = QString::fromUtf8(raw.imgdata.idata.model);
    metadata.software = QString::fromUtf8(raw.imgdata.idata.software);
    metadata.dateTime = QString();
    metadata.iso = raw.imgdata.other.iso_speed;
    metadata.aperture = raw.imgdata.other.aperture;
    metadata.shutter = raw.imgdata.other.shutter;
    if (raw.imgdata.other.timestamp > 0) {
        char dateBuf[32];
        struct tm *tm_info = localtime(&raw.imgdata.other.timestamp);
        strftime(dateBuf, sizeof(dateBuf), "%Y:%m:%d %H:%M:%S", tm_info);
        metadata.dateTime = QString::fromLatin1(dateBuf);
    }

    result = raw.unpack();
    if (result != LIBRAW_SUCCESS) {
        error = QString::fromUtf8(libraw_strerror(result));
        return false;
    }

    if (raw.imgdata.rawdata.raw_image == nullptr) {
        error = QStringLiteral("Unable to access native raw image buffer.");
        return false;
    }

    const int rawWidth = raw.imgdata.rawdata.sizes.raw_width;
    const int rawHeight = raw.imgdata.rawdata.sizes.raw_height;
    const int topMargin = raw.imgdata.sizes.top_margin;
    const int leftMargin = raw.imgdata.sizes.left_margin;
    const int visibleWidth = rawWidth - (leftMargin * 2);
    const int visibleHeight = rawHeight - (topMargin * 2);
    const int outWidth = visibleWidth - std::abs(dx);
    const int outHeight = visibleHeight - std::abs(dy);
    if (outWidth <= 0 || outHeight <= 0) {
        error = QStringLiteral("Invalid same-color pair output dimensions.");
        return false;
    }

    width = outWidth;
    height = outHeight;
    bitDepth = 16;
    std::vector<uint16_t> plane(static_cast<size_t>(width) * static_cast<size_t>(height));
    uint64_t pairCount = 0;

    const int startX = dx < 0 ? -dx : 0;
    const int startY = dy < 0 ? -dy : 0;
    for (int outY = 0; outY < outHeight; ++outY) {
        const int y = startY + outY;
        const int ny = y + dy;
        for (int outX = 0; outX < outWidth; ++outX) {
            const int x = startX + outX;
            const int nx = x + dx;

            const int rawRowA = y + topMargin;
            const int rawColA = x + leftMargin;
            const int rawRowB = ny + topMargin;
            const int rawColB = nx + leftMargin;
            const int colorA = raw.COLOR(rawRowA, rawColA);
            const int colorB = raw.COLOR(rawRowB, rawColB);
            if (colorA != colorB) {
                continue;
            }

            const uint16_t a = raw.imgdata.rawdata.raw_image[static_cast<size_t>(rawRowA) * static_cast<size_t>(rawWidth) +
                                                             static_cast<size_t>(rawColA)];
            const uint16_t b = raw.imgdata.rawdata.raw_image[static_cast<size_t>(rawRowB) * static_cast<size_t>(rawWidth) +
                                                             static_cast<size_t>(rawColB)];
            if (a == 0 || b == 0) {
                continue;
            }

            plane[static_cast<size_t>(outY) * static_cast<size_t>(width) + static_cast<size_t>(outX)] =
                brighterSample ? (a > b ? a : b) : (a < b ? a : b);
            pairCount++;
        }
    }

    rgbData.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 3);
    for (size_t i = 0; i < static_cast<size_t>(width) * static_cast<size_t>(height); ++i) {
        const uint16_t value = plane[i];
        rgbData[i * 3 + 0] = value;
        rgbData[i * 3 + 1] = value;
        rgbData[i * 3 + 2] = value;
    }

    logProcessing("readNativeSameColorPairRgb completed: dx=%d dy=%d brighter=%d width=%d height=%d pairs=%llu",
                  dx,
                  dy,
                  brighterSample ? 1 : 0,
                  width,
                  height,
                  static_cast<unsigned long long>(pairCount));
    return true;
}

bool SuperCCDProcessor::readProcessedRgb(const QString &inputPath,
                                         std::vector<uint16_t> &rgbData,
                                         int &width,
                                         int &height,
                                         int &bitDepth,
                                         SuperCCDMetadata &metadata,
                                         QString &error)
{
    logProcessing("readProcessedRgb open_file: %s", inputPath.toUtf8().constData());

    LibRaw raw;
    int result = raw.open_file(inputPath.toUtf8().constData());
    if (result != LIBRAW_SUCCESS) {
        error = QString::fromUtf8(libraw_strerror(result));
        return false;
    }

    metadata.make = QString::fromUtf8(raw.imgdata.idata.make);
    metadata.model = QString::fromUtf8(raw.imgdata.idata.model);
    metadata.software = QString::fromUtf8(raw.imgdata.idata.software);
    metadata.dateTime = QString();
    metadata.iso = raw.imgdata.other.iso_speed;
    metadata.aperture = raw.imgdata.other.aperture;
    metadata.shutter = raw.imgdata.other.shutter;
    if (raw.imgdata.other.timestamp > 0) {
        char dateBuf[32];
        struct tm *tm_info = localtime(&raw.imgdata.other.timestamp);
        strftime(dateBuf, sizeof(dateBuf), "%Y:%m:%d %H:%M:%S", tm_info);
        metadata.dateTime = QString::fromLatin1(dateBuf);
    }

    raw.imgdata.params.output_bps = 16;
    raw.imgdata.params.use_camera_wb = 1;
    raw.imgdata.params.no_auto_bright = 1;
    raw.imgdata.params.no_auto_scale = 1;
    raw.imgdata.params.bright = 1.0f;
    raw.imgdata.params.gamm[0] = 1.0;
    raw.imgdata.params.gamm[1] = 1.0;
    raw.imgdata.params.output_color = 1;

    result = raw.unpack();
    if (result != LIBRAW_SUCCESS) {
        error = QString::fromUtf8(libraw_strerror(result));
        logProcessing("processed unpack error %d: %s", result, error.toUtf8().constData());
        return false;
    }

    result = raw.dcraw_process();
    if (result != LIBRAW_SUCCESS) {
        error = QString::fromUtf8(libraw_strerror(result));
        logProcessing("dcraw_process error %d: %s", result, error.toUtf8().constData());
        return false;
    }

    int imageError = LIBRAW_SUCCESS;
    libraw_processed_image_t *image = raw.dcraw_make_mem_image(&imageError);
    if (!image) {
        error = QString::fromUtf8(libraw_strerror(imageError));
        logProcessing("dcraw_make_mem_image failed %d: %s", imageError, error.toUtf8().constData());
        return false;
    }

    width = static_cast<int>(image->width);
    height = static_cast<int>(image->height);
    bitDepth = 16;

    if (image->type != LIBRAW_IMAGE_BITMAP || image->colors < 3 || width <= 0 || height <= 0) {
        raw.dcraw_clear_mem(image);
        error = QStringLiteral("LibRaw did not return a usable RGB image.");
        return false;
    }

    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    rgbData.resize(pixelCount * 3);

    if (image->bits == 16) {
        const uint16_t *src = reinterpret_cast<const uint16_t *>(image->data);
        for (size_t i = 0; i < pixelCount; ++i) {
            rgbData[i * 3 + 0] = src[i * image->colors + 0];
            rgbData[i * 3 + 1] = src[i * image->colors + 1];
            rgbData[i * 3 + 2] = src[i * image->colors + 2];
        }
    } else if (image->bits == 8) {
        const uint8_t *src = image->data;
        for (size_t i = 0; i < pixelCount; ++i) {
            rgbData[i * 3 + 0] = static_cast<uint16_t>(src[i * image->colors + 0]) * 257u;
            rgbData[i * 3 + 1] = static_cast<uint16_t>(src[i * image->colors + 1]) * 257u;
            rgbData[i * 3 + 2] = static_cast<uint16_t>(src[i * image->colors + 2]) * 257u;
        }
    } else {
        raw.dcraw_clear_mem(image);
        error = QStringLiteral("Unsupported LibRaw RGB bit depth.");
        return false;
    }

    uint16_t minValue[3] = {65535, 65535, 65535};
    uint16_t maxValue[3] = {0, 0, 0};
    for (size_t i = 0; i < pixelCount; ++i) {
        for (int c = 0; c < 3; ++c) {
            const uint16_t value = rgbData[i * 3 + static_cast<size_t>(c)];
            if (value < minValue[c]) minValue[c] = value;
            if (value > maxValue[c]) maxValue[c] = value;
        }
    }

    logProcessing("readProcessedRgb completed: width=%d height=%d colors=%d bits=%d",
                  width, height, image->colors, image->bits);
    logProcessing("readProcessedRgb channel range: R=%u..%u G=%u..%u B=%u..%u",
                  minValue[0], maxValue[0],
                  minValue[1], maxValue[1],
                  minValue[2], maxValue[2]);
    raw.dcraw_clear_mem(image);
    return true;
}

bool SuperCCDProcessor::readLinearPlaneRgb(const QString &inputPath,
                                           int planeChannel,
                                           std::vector<uint16_t> &rgbData,
                                           int &width,
                                           int &height,
                                           int &bitDepth,
                                           SuperCCDMetadata &metadata,
                                           QString &error)
{
    logProcessing("readLinearPlaneRgb open_file: %s plane=%d", inputPath.toUtf8().constData(), planeChannel);

    LibRaw raw;
    int result = raw.open_file(inputPath.toUtf8().constData());
    if (result != LIBRAW_SUCCESS) {
        error = QString::fromUtf8(libraw_strerror(result));
        return false;
    }

    metadata.make = QString::fromUtf8(raw.imgdata.idata.make);
    metadata.model = QString::fromUtf8(raw.imgdata.idata.model);
    metadata.software = QString::fromUtf8(raw.imgdata.idata.software);
    metadata.dateTime = QString();
    metadata.iso = raw.imgdata.other.iso_speed;
    metadata.aperture = raw.imgdata.other.aperture;
    metadata.shutter = raw.imgdata.other.shutter;
    if (raw.imgdata.other.timestamp > 0) {
        char dateBuf[32];
        struct tm *tm_info = localtime(&raw.imgdata.other.timestamp);
        strftime(dateBuf, sizeof(dateBuf), "%Y:%m:%d %H:%M:%S", tm_info);
        metadata.dateTime = QString::fromLatin1(dateBuf);
    }

    result = raw.unpack();
    if (result != LIBRAW_SUCCESS) {
        error = QString::fromUtf8(libraw_strerror(result));
        return false;
    }

    if (raw.imgdata.image == nullptr) {
        result = raw.raw2image();
        if (result != LIBRAW_SUCCESS) {
            error = QString::fromUtf8(libraw_strerror(result));
            return false;
        }
    }

    if (raw.imgdata.image == nullptr) {
        error = QStringLiteral("Unable to access raw image buffer.");
        return false;
    }

    const int activeW = raw.imgdata.sizes.width;
    const int activeH = raw.imgdata.sizes.height;
    const int analysisWidth = activeW - (activeW % 2);
    const int analysisHeight = activeH - (activeH % 2);
    const int fujiWidth = raw.get_internal_data_pointer()->internal_output_params.fuji_width;

    int minDiagX = INT_MAX;
    int minDiagY = INT_MAX;
    int maxDiagX = -1;
    int maxDiagY = -1;

    for (int y = 0; y < analysisHeight; ++y) {
        for (int x = 0; x < analysisWidth; ++x) {
            const size_t srcIdx = static_cast<size_t>(y) * static_cast<size_t>(analysisWidth) + static_cast<size_t>(x);
            if (srcIdx >= static_cast<size_t>(activeW) * static_cast<size_t>(activeH)) {
                continue;
            }
            if (raw.imgdata.image[srcIdx][planeChannel] == 0) {
                continue;
            }

            const int diagX = (x + y) >> 1;
            const int diagY = ((y - x) >> 1) + fujiWidth;
            if (diagX < minDiagX) minDiagX = diagX;
            if (diagY < minDiagY) minDiagY = diagY;
            if (diagX > maxDiagX) maxDiagX = diagX;
            if (diagY > maxDiagY) maxDiagY = diagY;
        }
    }

    if (minDiagX > maxDiagX || minDiagY > maxDiagY) {
        error = QStringLiteral("Selected raw plane is empty.");
        return false;
    }

    const int planeWidth = maxDiagX - minDiagX + 1;
    const int planeHeight = maxDiagY - minDiagY + 1;
    std::vector<uint16_t> plane(static_cast<size_t>(planeWidth) * static_cast<size_t>(planeHeight));
    std::vector<uint8_t> mask(static_cast<size_t>(planeWidth) * static_cast<size_t>(planeHeight));
    uint64_t packed = 0;
    uint64_t collisions = 0;

    for (int y = 0; y < analysisHeight; ++y) {
        for (int x = 0; x < analysisWidth; ++x) {
            const size_t srcIdx = static_cast<size_t>(y) * static_cast<size_t>(analysisWidth) + static_cast<size_t>(x);
            if (srcIdx >= static_cast<size_t>(activeW) * static_cast<size_t>(activeH)) {
                continue;
            }

            const uint16_t sample = raw.imgdata.image[srcIdx][planeChannel];
            if (sample == 0) {
                continue;
            }

            const int diagX = ((x + y) >> 1) - minDiagX;
            const int diagY = (((y - x) >> 1) + fujiWidth) - minDiagY;
            const size_t dstIdx = static_cast<size_t>(diagY) * static_cast<size_t>(planeWidth) +
                                  static_cast<size_t>(diagX);
            if (mask[dstIdx]) {
                collisions++;
            }
            plane[dstIdx] = sample;
            mask[dstIdx] = 1;
            packed++;
        }
    }

    fillSparsePlane(plane, mask, planeWidth, planeHeight);

    width = planeWidth;
    height = planeHeight;
    bitDepth = 16;
    rgbData.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 3);
    for (size_t i = 0; i < static_cast<size_t>(width) * static_cast<size_t>(height); ++i) {
        const uint16_t value = plane[i];
        rgbData[i * 3 + 0] = value;
        rgbData[i * 3 + 1] = value;
        rgbData[i * 3 + 2] = value;
    }

    logProcessing("readLinearPlaneRgb completed: plane=%d width=%d height=%d packed=%llu collisions=%llu",
                  planeChannel,
                  width,
                  height,
                  static_cast<unsigned long long>(packed),
                  static_cast<unsigned long long>(collisions));
    return true;
}

bool SuperCCDProcessor::readRaw(const QString &inputPath,
                                bool output12MP,
                                std::vector<uint16_t> &rawData,
                                int &width,
                                int &height,
                                int &bitDepth,
                                SuperCCDMetadata &metadata,
                                QString &error)
{
    logProcessing("readRaw open_file: %s", inputPath.toUtf8().constData());

    LibRaw raw;
    int result = raw.open_file(inputPath.toUtf8().constData());
    if (result != LIBRAW_SUCCESS) {
        error = QString::fromUtf8(libraw_strerror(result));
        logProcessing("open_file error %d: %s", result, error.toUtf8().constData());
        return false;
    }

    // Fill metadata early to use it for camera-specific dimension logic
    metadata.make = QString::fromUtf8(raw.imgdata.idata.make);
    metadata.model = QString::fromUtf8(raw.imgdata.idata.model);
    metadata.software = QString::fromUtf8(raw.imgdata.idata.software);
    metadata.dateTime = QString();
    metadata.iso = raw.imgdata.other.iso_speed;
    metadata.aperture = raw.imgdata.other.aperture;
    metadata.shutter = raw.imgdata.other.shutter;
    // Extract date/time if available
    if (raw.imgdata.other.timestamp > 0) {
        char dateBuf[32];
        struct tm *tm_info = localtime(&raw.imgdata.other.timestamp);
        strftime(dateBuf, sizeof(dateBuf), "%Y:%m:%d %H:%M:%S", tm_info);
        metadata.dateTime = QString::fromLatin1(dateBuf);
    }

    result = raw.unpack();
    if (result != LIBRAW_SUCCESS) {
        error = QString::fromUtf8(libraw_strerror(result));
        logProcessing("unpack error %d: %s", result, error.toUtf8().constData());
        return false;
    }

    auto &rawdata = raw.imgdata.rawdata;
    // Trust LibRaw's calculated active dimensions (after margins)
    int activeW = raw.imgdata.sizes.width;
    int activeH = raw.imgdata.sizes.height;
    bitDepth = 16; // LibRaw API changed; assume 16-bit raw samples by default

    logProcessing("sizes: rawW=%d rawH=%d activeW=%d activeH=%d", 
                  rawdata.sizes.raw_width, rawdata.sizes.raw_height, activeW, activeH);
    logProcessing("image sizes: iwidth=%d iheight=%d top=%d left=%d fujiWidth=%d isFujiRotated=%d",
                  raw.imgdata.sizes.iwidth,
                  raw.imgdata.sizes.iheight,
                  raw.imgdata.sizes.top_margin,
                  raw.imgdata.sizes.left_margin,
                  raw.get_internal_data_pointer()->internal_output_params.fuji_width,
                  raw.is_fuji_rotated());

    int availW = activeW;
    int availH = activeH;

    // For Fujifilm SuperCCD SR sensors (S3 Pro, S5 Pro), LibRaw typically provides S and R pixels 
    // as separate channels in the 4-channel image buffer.
    if (metadata.model.contains(QLatin1String("S3Pro"), Qt::CaseInsensitive) || 
        metadata.model.contains(QLatin1String("S5Pro"), Qt::CaseInsensitive)) {
        if (rawdata.sizes.raw_height < 2000) { 
            // We will stack S and R pixels vertically in our local rawData vector
            availH = activeH * 2;
        }
    }

    if (availW <= 0 || availH <= 0) {
        error = QStringLiteral("Invalid RAF raw dimensions or margins.");
        logProcessing("available area invalid: availW=%d availH=%d", availW, availH);
        return false;
    }

    // Use the full available sensor width/height while we inspect the unpacked
    // SuperCCD sample placement. We later remap this to a Bayer-like plane.
    width = availW;
    height = availH;
    if (width % 2 != 0) width--;
    if (height % 2 != 0) height--;

    if (width <= 0 || height <= 0) {
        error = QStringLiteral("Invalid RAF raw dimensions.");
        return false;
    }

    rawData.resize(static_cast<size_t>(width) * static_cast<size_t>(height));

    // For SuperCCD SR, we need the 4-channel image buffer to get S and R pixels
    if (raw.imgdata.image == nullptr) {
        result = raw.raw2image();
        if (result != LIBRAW_SUCCESS) {
            error = QString::fromUtf8(libraw_strerror(result));
            return false;
        }
    }

    if (raw.imgdata.image == nullptr) {
        error = QStringLiteral("Unable to access raw image buffer.");
        return false;
    }

    logProcessing("LibRaw image buffer ready; filling rawData vector");

    const int analysisHeight = activeH - (activeH % 2);
    uint64_t nonZeroByChannel[4] = {0, 0, 0, 0};
    uint64_t colorByCfa[4] = {0, 0, 0, 0};
    uint64_t nonZeroSelected = 0;
    const int fujiWidth = raw.get_internal_data_pointer()->internal_output_params.fuji_width;
    const int fujiLayout = raw.get_internal_data_pointer()->unpacker_data.fuji_layout;
    int minX[4] = {INT_MAX, INT_MAX, INT_MAX, INT_MAX};
    int minY[4] = {INT_MAX, INT_MAX, INT_MAX, INT_MAX};
    int maxX[4] = {-1, -1, -1, -1};
    int maxY[4] = {-1, -1, -1, -1};
    int minDenseX = INT_MAX;
    int minDenseY = INT_MAX;
    int maxDenseX = -1;
    int maxDenseY = -1;
    int minDiagX = INT_MAX;
    int minDiagY = INT_MAX;
    int maxDiagX = -1;
    int maxDiagY = -1;
    int minHybridX = INT_MAX;
    int minHybridY = INT_MAX;
    int maxHybridX = -1;
    int maxHybridY = -1;
    uint64_t candidateMismatch[4] = {};
    uint64_t modCounts[4][4][4] = {};
    char occupancyMap[9][9] = {};
    for (int my = 0; my < 8; ++my) {
        for (int mx = 0; mx < 8; ++mx) {
            occupancyMap[my][mx] = '.';
        }
        occupancyMap[my][8] = '\0';
    }
    for (int y = 0; y < analysisHeight; ++y) {
        for (int x = 0; x < width; ++x) {
            // Use compact indexing for the de-margined LibRaw image buffer
            size_t srcIdx = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
            int color = raw.COLOR(y, x);
            if (color < 0 || color > 3) {
                color = 0;
            }
            if (srcIdx < (size_t)activeW * activeH) {
                colorByCfa[color]++;
                for (int c = 0; c < 4; ++c) {
                    if (raw.imgdata.image[srcIdx][c] != 0) {
                        nonZeroByChannel[c]++;
                        if (x < minX[c]) minX[c] = x;
                        if (y < minY[c]) minY[c] = y;
                        if (x > maxX[c]) maxX[c] = x;
                        if (y > maxY[c]) maxY[c] = y;
                        int denseY = 0;
                        int denseX = 0;
                        if (fujiLayout) {
                            denseY = fujiWidth - 1 - x + (y >> 1);
                            denseX = x + ((y + 1) >> 1);
                        } else {
                            denseY = fujiWidth - 1 + y - (x >> 1);
                            denseX = y + ((x + 1) >> 1);
                        }
                        if (denseX < minDenseX) minDenseX = denseX;
                        if (denseY < minDenseY) minDenseY = denseY;
                        if (denseX > maxDenseX) maxDenseX = denseX;
                        if (denseY > maxDenseY) maxDenseY = denseY;
                        int diagX = (x + y) >> 1;
                        int diagY = ((y - x) >> 1) + fujiWidth;
                        if (diagX < minDiagX) minDiagX = diagX;
                        if (diagY < minDiagY) minDiagY = diagY;
                        if (diagX > maxDiagX) maxDiagX = diagX;
                        if (diagY > maxDiagY) maxDiagY = diagY;
                        int hybridX = denseX;
                        int hybridY = diagY;
                        if (hybridX < minHybridX) minHybridX = hybridX;
                        if (hybridY < minHybridY) minHybridY = hybridY;
                        if (hybridX > maxHybridX) maxHybridX = hybridX;
                        if (hybridY > maxHybridY) maxHybridY = hybridY;
                        const int outputColor = (c == 3) ? 1 : c;
                        const int parityOptions[4] = {x & 1, y & 1, (x + y) & 1, c & 1};
                        for (int p = 0; p < 4; ++p) {
                            int outYParity = diagY & 1;
                            int outXParity = parityOptions[p];
                            int cfaColor = (outYParity == 0)
                                               ? (outXParity == 0 ? 0 : 1)
                                               : (outXParity == 0 ? 1 : 2);
                            if (cfaColor != outputColor) {
                                candidateMismatch[p]++;
                            }
                        }
                        modCounts[c][y & 3][x & 3]++;
                        if (y < 8 && x < 8) {
                            occupancyMap[y][x] = static_cast<char>('0' + c);
                        }
                    }
                }
                if (raw.imgdata.image[srcIdx][color] != 0) {
                    nonZeroSelected++;
                }
            }

        }
    }

    logProcessing("LibRaw color stats: cdesc=%s filters=0x%x colors=%d",
                  raw.imgdata.idata.cdesc,
                  raw.imgdata.idata.filters,
                  raw.imgdata.idata.colors);
    logProcessing("CFA color counts: c0=%llu c1=%llu c2=%llu c3=%llu selectedNonZero=%llu",
                  static_cast<unsigned long long>(colorByCfa[0]),
                  static_cast<unsigned long long>(colorByCfa[1]),
                  static_cast<unsigned long long>(colorByCfa[2]),
                  static_cast<unsigned long long>(colorByCfa[3]),
                  static_cast<unsigned long long>(nonZeroSelected));
    logProcessing("Image channel nonzero counts: ch0=%llu ch1=%llu ch2=%llu ch3=%llu",
                  static_cast<unsigned long long>(nonZeroByChannel[0]),
                  static_cast<unsigned long long>(nonZeroByChannel[1]),
                  static_cast<unsigned long long>(nonZeroByChannel[2]),
                  static_cast<unsigned long long>(nonZeroByChannel[3]));
    logProcessing("Fuji dense bbox: layout=%d x=%d..%d y=%d..%d size=%dx%d",
                  fujiLayout,
                  minDenseX,
                  maxDenseX,
                  minDenseY,
                  maxDenseY,
                  maxDenseX >= minDenseX ? (maxDenseX - minDenseX + 1) : 0,
                  maxDenseY >= minDenseY ? (maxDenseY - minDenseY + 1) : 0);
    logProcessing("Fuji diagonal bbox: x=%d..%d y=%d..%d size=%dx%d",
                  minDiagX,
                  maxDiagX,
                  minDiagY,
                  maxDiagY,
                  maxDiagX >= minDiagX ? (maxDiagX - minDiagX + 1) : 0,
                  maxDiagY >= minDiagY ? (maxDiagY - minDiagY + 1) : 0);
    logProcessing("Fuji hybrid bbox: x=%d..%d y=%d..%d size=%dx%d",
                  minHybridX,
                  maxHybridX,
                  minHybridY,
                  maxHybridY,
                  maxHybridX >= minHybridX ? (maxHybridX - minHybridX + 1) : 0,
                  maxHybridY >= minHybridY ? (maxHybridY - minHybridY + 1) : 0);
    logProcessing("Fuji pack CFA mismatches: xParity=%llu yParity=%llu xyParity=%llu channelParity=%llu",
                  static_cast<unsigned long long>(candidateMismatch[0]),
                  static_cast<unsigned long long>(candidateMismatch[1]),
                  static_cast<unsigned long long>(candidateMismatch[2]),
                  static_cast<unsigned long long>(candidateMismatch[3]));
    logProcessing("Nonzero channel 8x8 map: %s/%s/%s/%s/%s/%s/%s/%s",
                  occupancyMap[0], occupancyMap[1], occupancyMap[2], occupancyMap[3],
                  occupancyMap[4], occupancyMap[5], occupancyMap[6], occupancyMap[7]);
    for (int c = 0; c < 4; ++c) {
        logProcessing("Channel %d bbox: x=%d..%d y=%d..%d", c, minX[c], maxX[c], minY[c], maxY[c]);
        logProcessing("Channel %d mod4 rows: [%llu,%llu,%llu,%llu] [%llu,%llu,%llu,%llu] [%llu,%llu,%llu,%llu] [%llu,%llu,%llu,%llu]",
                      c,
                      static_cast<unsigned long long>(modCounts[c][0][0]), static_cast<unsigned long long>(modCounts[c][0][1]),
                      static_cast<unsigned long long>(modCounts[c][0][2]), static_cast<unsigned long long>(modCounts[c][0][3]),
                      static_cast<unsigned long long>(modCounts[c][1][0]), static_cast<unsigned long long>(modCounts[c][1][1]),
                      static_cast<unsigned long long>(modCounts[c][1][2]), static_cast<unsigned long long>(modCounts[c][1][3]),
                      static_cast<unsigned long long>(modCounts[c][2][0]), static_cast<unsigned long long>(modCounts[c][2][1]),
                      static_cast<unsigned long long>(modCounts[c][2][2]), static_cast<unsigned long long>(modCounts[c][2][3]),
                      static_cast<unsigned long long>(modCounts[c][3][0]), static_cast<unsigned long long>(modCounts[c][3][1]),
                      static_cast<unsigned long long>(modCounts[c][3][2]), static_cast<unsigned long long>(modCounts[c][3][3]));
    }

    if (output12MP && fujiWidth > 0 && minDiagX <= maxDiagX && minDiagY <= maxDiagY) {
        const int diagWidth = maxDiagX - minDiagX + 1;
        const int diagHeight = maxDiagY - minDiagY + 1;
        const int planeWidth = ((diagWidth + 1) / 2) * 2;
        std::vector<uint16_t> planeG1(static_cast<size_t>(planeWidth) * static_cast<size_t>(diagHeight));
        std::vector<uint16_t> planeR(static_cast<size_t>(planeWidth) * static_cast<size_t>(diagHeight));
        std::vector<uint16_t> planeB(static_cast<size_t>(planeWidth) * static_cast<size_t>(diagHeight));
        std::vector<uint16_t> planeG2(static_cast<size_t>(planeWidth) * static_cast<size_t>(diagHeight));
        std::vector<uint8_t> maskG1(static_cast<size_t>(planeWidth) * static_cast<size_t>(diagHeight));
        std::vector<uint8_t> maskR(static_cast<size_t>(planeWidth) * static_cast<size_t>(diagHeight));
        std::vector<uint8_t> maskB(static_cast<size_t>(planeWidth) * static_cast<size_t>(diagHeight));
        std::vector<uint8_t> maskG2(static_cast<size_t>(planeWidth) * static_cast<size_t>(diagHeight));
        uint64_t packed = 0;
        uint64_t collisions = 0;

        for (int y = 0; y < analysisHeight; ++y) {
            for (int x = 0; x < width; ++x) {
                const size_t srcIdx = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
                if (srcIdx >= static_cast<size_t>(activeW) * static_cast<size_t>(activeH)) {
                    continue;
                }

                const int diagX = ((x + y) >> 1) - minDiagX;
                const int diagY = (((y - x) >> 1) + fujiWidth) - minDiagY;
                if (diagX < 0 || diagX >= diagWidth || diagY < 0 || diagY >= diagHeight) {
                    continue;
                }

                for (int c = 0; c < 4; ++c) {
                    const uint16_t sample = raw.imgdata.image[srcIdx][c];
                    if (sample == 0) {
                        continue;
                    }

                    std::vector<uint16_t> *plane = nullptr;
                    std::vector<uint8_t> *mask = nullptr;
                    switch (c) {
                    case 3:
                        plane = &planeG1;
                        mask = &maskG1;
                        break;
                    case 0:
                        plane = &planeR;
                        mask = &maskR;
                        break;
                    case 2:
                        plane = &planeB;
                        mask = &maskB;
                        break;
                    case 1:
                        plane = &planeG2;
                        mask = &maskG2;
                        break;
                    default:
                        continue;
                    }

                    const size_t dstIdx = static_cast<size_t>(diagY) * static_cast<size_t>(planeWidth) +
                                          static_cast<size_t>(diagX);
                    if ((*mask)[dstIdx]) {
                        collisions++;
                    }
                    (*plane)[dstIdx] = sample;
                    (*mask)[dstIdx] = 1;
                    packed++;
                }
            }
        }

        fillSparsePlane(planeG1, maskG1, planeWidth, diagHeight);
        fillSparsePlane(planeR, maskR, planeWidth, diagHeight);
        fillSparsePlane(planeB, maskB, planeWidth, diagHeight);
        fillSparsePlane(planeG2, maskG2, planeWidth, diagHeight);

        const int outWidth = planeWidth * 2;
        const int outHeight = diagHeight * 2;
        std::vector<uint16_t> denseData(static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight));
        for (int py = 0; py < diagHeight; ++py) {
            for (int px = 0; px < planeWidth; ++px) {
                const size_t planeIdx = static_cast<size_t>(py) * static_cast<size_t>(planeWidth) +
                                        static_cast<size_t>(px);
                denseData[static_cast<size_t>(py * 2) * static_cast<size_t>(outWidth) +
                          static_cast<size_t>(px * 2)] = planeG1[planeIdx];
                denseData[static_cast<size_t>(py * 2) * static_cast<size_t>(outWidth) +
                          static_cast<size_t>(px * 2 + 1)] = planeR[planeIdx];
                denseData[static_cast<size_t>(py * 2 + 1) * static_cast<size_t>(outWidth) +
                          static_cast<size_t>(px * 2)] = planeB[planeIdx];
                denseData[static_cast<size_t>(py * 2 + 1) * static_cast<size_t>(outWidth) +
                          static_cast<size_t>(px * 2 + 1)] = planeG2[planeIdx];
            }
        }

        width = outWidth;
        height = outHeight;
        rawData.swap(denseData);
        logProcessing("Direct SuperCCD remap to Bayer-like CFA: planeWidth=%d planeHeight=%d outWidth=%d outHeight=%d packed=%llu collisions=%llu",
                      planeWidth,
                      diagHeight,
                      width,
                      height,
                      static_cast<unsigned long long>(packed),
                      static_cast<unsigned long long>(collisions));
    } else if (fujiWidth > 0 && minDiagX <= maxDiagX && minDiagY <= maxDiagY) {
        const int diagWidth = maxDiagX - minDiagX + 1;
        const int diagHeight = maxDiagY - minDiagY + 1;
        const int denseWidth = ((diagWidth + 1) / 2) * 2;
        const int denseHeight = diagHeight * 2;
        std::vector<uint16_t> denseData(static_cast<size_t>(denseWidth) * static_cast<size_t>(denseHeight));
        uint64_t packed = 0;
        uint64_t collisions = 0;
        uint64_t parityViolations = 0;

        for (int y = 0; y < analysisHeight; ++y) {
            for (int x = 0; x < width; ++x) {
                const size_t srcIdx = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
                if (srcIdx >= static_cast<size_t>(activeW) * static_cast<size_t>(activeH)) {
                    continue;
                }

                const int diagX = ((x + y) >> 1) - minDiagX;
                const int diagY = (((y - x) >> 1) + fujiWidth) - minDiagY;
                if (diagX < 0 || diagX >= diagWidth || diagY < 0 || diagY >= diagHeight) {
                    continue;
                }

                const int rowParity = diagY & 1;

                for (int c = 0; c < 4; ++c) {
                    const uint16_t sample = raw.imgdata.image[srcIdx][c];
                    if (sample == 0) {
                        continue;
                    }

                    int blockX = 0;
                    int rowOffset = 0;
                    int xOffset = 0;
                    switch (c) {
                    case 3: // G for the first row of a GRBG block
                        if ((diagX & 1) == rowParity) {
                            parityViolations++;
                        }
                        blockX = (diagX - (1 - rowParity)) / 2;
                        rowOffset = 0;
                        xOffset = 0;
                        break;
                    case 0: // R
                        if ((diagX & 1) == rowParity) {
                            parityViolations++;
                        }
                        blockX = (diagX - (1 - rowParity)) / 2;
                        rowOffset = 0;
                        xOffset = 1;
                        break;
                    case 2: // B
                        if ((diagX & 1) != rowParity) {
                            parityViolations++;
                        }
                        blockX = (diagX - rowParity) / 2;
                        rowOffset = 1;
                        xOffset = 0;
                        break;
                    case 1: // G for the second row of a GRBG block
                        if ((diagX & 1) != rowParity) {
                            parityViolations++;
                        }
                        blockX = (diagX - rowParity) / 2;
                        rowOffset = 1;
                        xOffset = 1;
                        break;
                    default:
                        continue;
                    }

                    if (blockX < 0 || blockX >= (denseWidth / 2)) {
                        continue;
                    }

                    const int outX = blockX * 2 + xOffset;
                    const int outY = diagY * 2 + rowOffset;
                    const size_t dstIdx = static_cast<size_t>(outY) * static_cast<size_t>(denseWidth) +
                                          static_cast<size_t>(outX);
                    if (denseData[dstIdx] != 0) {
                        collisions++;
                    }
                    denseData[dstIdx] = sample;
                    packed++;
                }
            }
        }

        width = denseWidth;
        height = denseHeight;
        rawData.swap(denseData);
        logProcessing("Packed Fuji sparse image to Bayer-like CFA: width=%d height=%d packed=%llu collisions=%llu parityViolations=%llu",
                      width,
                      height,
                      static_cast<unsigned long long>(packed),
                      static_cast<unsigned long long>(collisions),
                      static_cast<unsigned long long>(parityViolations));
    }

    logProcessing("readRaw completed: width=%d height=%d bitDepth=%d", width, height, bitDepth);

    return true;
}

bool SuperCCDProcessor::reconstructSuperCCD(const std::vector<uint16_t> &rawData,
                                            int width,
                                            int height,
                                            bool output12MP,
                                            std::vector<uint16_t> &outputData,
                                            int &outWidth,
                                            int &outHeight,
                                            QString &error)
{
    if (width <= 0 || height <= 0) {
        error = QStringLiteral("Invalid raw image size.");
        return false;
    }

    // The 12 MP path is now produced directly in readRaw(). The 6 MP path
    // still returns the packed half-width Bayer-like CFA plane.
    outWidth = output12MP ? width : width;
    outHeight = height;

    if (outWidth == 0 || outHeight == 0) {
        error = QStringLiteral("Image dimensions too small.");
        return false;
    }

    outputData.resize(static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight));

    if (!output12MP) {
        for (size_t i = 0; i < outputData.size() && i < rawData.size(); ++i) {
            outputData[i] = rawData[i];
        }
        return true;
    }

    for (size_t i = 0; i < outputData.size() && i < rawData.size(); ++i) {
        outputData[i] = rawData[i];
    }

    return true;
}
