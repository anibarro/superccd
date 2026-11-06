#include "SceneRender.h"
#include "ParallelProcessing.h"

#include <algorithm>
#include <cmath>

namespace superccd {
namespace {

using Matrix3 = std::array<double, 9>;

// ---- Color science ---------------------------------------------------------

constexpr Matrix3 kXyzFromRec2020 = {
    0.6369580, 0.1446169, 0.1688810,
    0.2627002, 0.6779981, 0.0593017,
    0.0000000, 0.0280727, 1.0609851};
constexpr Matrix3 kXyzFromRec709 = {
    0.4123908, 0.3575843, 0.1804808,
    0.2126390, 0.7151687, 0.0721923,
    0.0193308, 0.1191948, 0.9505322};
// Oklab, Bjorn Ottosson.
constexpr Matrix3 kOklabLmsFromXyz = {
    0.8189330101, 0.3618667424, -0.1288597137,
    0.0329845436, 0.9293118715, 0.0361456387,
    0.0482003018, 0.2643662691, 0.6338517070};
constexpr Matrix3 kOklabFromLms = {
    0.2104542553, 0.7936177850, -0.0040720468,
    1.9779984951, -2.4285922050, 0.4505937099,
    0.0259040371, 0.7827717662, -0.8086757660};

constexpr float kLumaR = 0.2627002f;
constexpr float kLumaG = 0.6779981f;
constexpr float kLumaB = 0.0593017f;

Matrix3 multiply(const Matrix3 &a, const Matrix3 &b)
{
    Matrix3 result{};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            double sum = 0.0;
            for (int k = 0; k < 3; ++k) {
                sum += a[static_cast<size_t>(row * 3 + k)] *
                       b[static_cast<size_t>(k * 3 + column)];
            }
            result[static_cast<size_t>(row * 3 + column)] = sum;
        }
    }
    return result;
}

Matrix3 inverse(const Matrix3 &m)
{
    const double c00 = m[4] * m[8] - m[5] * m[7];
    const double c01 = m[5] * m[6] - m[3] * m[8];
    const double c02 = m[3] * m[7] - m[4] * m[6];
    const double determinant = m[0] * c00 + m[1] * c01 + m[2] * c02;
    if (std::abs(determinant) < 1e-12) {
        return {1, 0, 0, 0, 1, 0, 0, 0, 1};
    }
    const double s = 1.0 / determinant;
    return {
        c00 * s, (m[2] * m[7] - m[1] * m[8]) * s, (m[1] * m[5] - m[2] * m[4]) * s,
        c01 * s, (m[0] * m[8] - m[2] * m[6]) * s, (m[2] * m[3] - m[0] * m[5]) * s,
        c02 * s, (m[1] * m[6] - m[0] * m[7]) * s, (m[0] * m[4] - m[1] * m[3]) * s};
}

std::array<double, 3> transformVector(const Matrix3 &m, const std::array<double, 3> &v)
{
    return {
        m[0] * v[0] + m[1] * v[1] + m[2] * v[2],
        m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
        m[6] * v[0] + m[7] * v[1] + m[8] * v[2]};
}

struct LinearToOklab {
    std::array<float, 9> toLms{};
    std::array<float, 9> fromLms{};
    std::array<float, 9> toLab{};
    std::array<float, 9> fromLab{};
    std::array<float, 9> rec709FromRec2020{};

    LinearToOklab()
    {
        const Matrix3 lmsFromWork = multiply(kOklabLmsFromXyz, kXyzFromRec2020);
        const Matrix3 workFromLms = inverse(lmsFromWork);
        const Matrix3 labFromLms = kOklabFromLms;
        const Matrix3 lmsFromLab = inverse(labFromLms);
        const Matrix3 toRec709 = multiply(inverse(kXyzFromRec709), kXyzFromRec2020);
        for (size_t i = 0; i < 9; ++i) {
            toLms[i] = static_cast<float>(lmsFromWork[i]);
            fromLms[i] = static_cast<float>(workFromLms[i]);
            toLab[i] = static_cast<float>(labFromLms[i]);
            fromLab[i] = static_cast<float>(lmsFromLab[i]);
            rec709FromRec2020[i] = static_cast<float>(toRec709[i]);
        }
    }
};

const LinearToOklab &oklab()
{
    static const LinearToOklab instance;
    return instance;
}

// Camera to working space. Rows are normalized so that a white-balanced neutral
// (1, 1, 1) stays neutral, which is the convention dcraw and LibRaw use.
Matrix3 workFromCameraMatrix(const SceneImage &scene)
{
    if (!scene.hasColorMatrix) {
        return multiply(inverse(kXyzFromRec2020), kXyzFromRec709);
    }
    Matrix3 cameraFromWork = multiply(scene.xyzToCam, kXyzFromRec2020);
    for (int row = 0; row < 3; ++row) {
        const size_t offset = static_cast<size_t>(row * 3);
        const double sum = cameraFromWork[offset] + cameraFromWork[offset + 1] +
                           cameraFromWork[offset + 2];
        if (std::abs(sum) > 1e-9) {
            for (size_t column = 0; column < 3; ++column) {
                cameraFromWork[offset + column] /= sum;
            }
        }
    }
    return inverse(cameraFromWork);
}

// ---- White balance ---------------------------------------------------------

struct Uv {
    double u;
    double v;
};

// Planckian locus in CIE 1960 UCS, Krystek's rational approximation.
Uv planckianUv(double kelvin)
{
    const double t = kelvin;
    return {
        (0.860117757 + 1.54118254e-4 * t + 1.28641212e-7 * t * t) /
            (1.0 + 8.42420235e-4 * t + 7.08145163e-7 * t * t),
        (0.317398726 + 4.22806245e-5 * t + 4.20481691e-8 * t * t) /
            (1.0 - 2.89741816e-5 * t + 1.61456053e-7 * t * t)};
}

constexpr double kMinimumMired = 1.0e6 / 15000.0;
constexpr double kMaximumMired = 1.0e6 / 1667.0;

struct LocusPosition {
    double mired = 1.0e6 / 6500.0;
    double duv = 0.0;
};

Uv locusNormal(double mired)
{
    const Uv before = planckianUv(1.0e6 / (mired - 0.5));
    const Uv after = planckianUv(1.0e6 / (mired + 0.5));
    double nu = -(after.v - before.v);
    double nv = after.u - before.u;
    const double length = std::hypot(nu, nv);
    nu /= length;
    nv /= length;
    if (nv < 0.0) { // Positive Duv points to the green side of the locus
        nu = -nu;
        nv = -nv;
    }
    return {nu, nv};
}

LocusPosition locusFromXyz(const std::array<double, 3> &xyz)
{
    const double denominator = xyz[0] + 15.0 * xyz[1] + 3.0 * xyz[2];
    LocusPosition position;
    if (denominator <= 1e-12) {
        return position;
    }
    const double u = 4.0 * xyz[0] / denominator;
    const double v = 6.0 * xyz[1] / denominator;

    // Golden-section search for the closest point on the locus.
    constexpr double kGolden = 0.6180339887498949;
    double low = kMinimumMired;
    double high = kMaximumMired;
    const auto distance = [&](double mired) {
        const Uv p = planckianUv(1.0e6 / mired);
        return std::hypot(u - p.u, v - p.v);
    };
    double a = high - kGolden * (high - low);
    double b = low + kGolden * (high - low);
    double fa = distance(a);
    double fb = distance(b);
    for (int iteration = 0; iteration < 48; ++iteration) {
        if (fa < fb) {
            high = b;
            b = a;
            fb = fa;
            a = high - kGolden * (high - low);
            fa = distance(a);
        } else {
            low = a;
            a = b;
            fa = fb;
            b = low + kGolden * (high - low);
            fb = distance(b);
        }
    }
    position.mired = 0.5 * (low + high);
    const Uv locus = planckianUv(1.0e6 / position.mired);
    const Uv normal = locusNormal(position.mired);
    position.duv = (u - locus.u) * normal.u + (v - locus.v) * normal.v;
    return position;
}

std::array<double, 3> xyzFromLocus(const LocusPosition &position)
{
    const Uv locus = planckianUv(1.0e6 / position.mired);
    const Uv normal = locusNormal(position.mired);
    const double u = locus.u + position.duv * normal.u;
    const double v = locus.v + position.duv * normal.v;
    const double denominator = 2.0 * u - 8.0 * v + 4.0;
    const double x = 3.0 * u / denominator;
    const double y = 2.0 * v / denominator;
    return {x / y, 1.0, (1.0 - x - y) / y};
}

// Temperature steps are uniform in mired, which is how color differences scale.
constexpr double kMiredPerSliderUnit = 1.5;
// Same scale as the DNG specification's tint value.
constexpr double kTintScale = 3000.0;

LocusPosition asShotLocus(const SceneImage &scene)
{
    const std::array<double, 3> neutral = {
        1.0 / scene.asShotMul[0], 1.0 / scene.asShotMul[1], 1.0 / scene.asShotMul[2]};
    return locusFromXyz(transformVector(inverse(scene.xyzToCam), neutral));
}

// ---- Base tone curve -------------------------------------------------------

// Fitted to the response of the embedded camera JPEGs
// over S3 Pro and S5 Pro frames, so a photo opens the way the camera rendered it.
// These are constants on purpose: making them depend on the image would bring
// back the automatic balancing that hid the real exposure.
constexpr double kMiddleGrayLog2 = -2.69;  // Scene level shown as middle gray
constexpr double kDisplayGrayLog2 = -2.4739; // log2(0.18)
constexpr double kBaseSlope = 1.33;
constexpr double kShoulderOffset = 0.49;   // Stops above gray where roll-off begins

// A per-channel camera curve raises saturation in the midtones and takes it away
// toward white. The tone curve here keeps channel ratios, so that behavior is
// added back as a chroma gain following the display level.
constexpr double kBaseChroma = 1.30;
constexpr double kWhiteChroma = 0.45;

constexpr float kLutMinimum = -24.0f;
constexpr float kLutMaximum = 12.0f;
constexpr int kLutPerStop = 32;
constexpr int kLutSize = static_cast<int>(kLutMaximum - kLutMinimum) * kLutPerStop + 1;

inline float lookup(const std::vector<float> &lut, float x)
{
    const float position = std::clamp((x - kLutMinimum) * kLutPerStop, 0.0f,
                                      static_cast<float>(kLutSize - 1) - 0.001f);
    const int index = static_cast<int>(position);
    const float fraction = position - static_cast<float>(index);
    const float low = lut[static_cast<size_t>(index)];
    return low + (lut[static_cast<size_t>(index) + 1] - low) * fraction;
}

double smoothstep(double edge0, double edge1, double x)
{
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double softPlus(double x, double knee)
{
    const double z = x / knee;
    return z > 30.0 ? x : knee * std::log1p(std::exp(z));
}

double srgbEncode(double linear)
{
    return linear <= 0.0031308
        ? 12.92 * linear
        : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
}

double srgbDecode(double encoded)
{
    return encoded <= 0.04045
        ? encoded / 12.92
        : std::pow((encoded + 0.055) / 1.055, 2.4);
}

// log2 display luminance for a log2 scene luminance. Straight in the mid range,
// then an exponential approach to display white that matches the camera. An
// exponential gets within one 16-bit code of white after a few stops, so the last
// part hands over to a much slower hyperbola: however hard the exposure is pushed,
// every stop keeps a slope of a few codes and stays separable.
double curveLog2(double x, double slope, double shoulderX)
{
    double shoulderY = kDisplayGrayLog2 + slope * (shoulderX - kMiddleGrayLog2);
    if (shoulderY > -0.30) {
        shoulderX -= (shoulderY + 0.30) / slope;
        shoulderY = -0.30;
    }
    if (x <= shoulderX) {
        return kDisplayGrayLog2 + slope * (x - kMiddleGrayLog2);
    }
    constexpr double kTailStart = -0.004;
    const double y = shoulderY * std::exp(slope * (x - shoulderX) / shoulderY);
    if (y < kTailStart) {
        return y;
    }
    const double tailX = shoulderX + shoulderY / slope * std::log(kTailStart / shoulderY);
    // The cap keeps a steep, high-contrast curve from using up the tail quickly.
    const double tailRate = std::min(slope / -shoulderY, 0.7);
    return kTailStart / (1.0 + tailRate * (x - tailX));
}

// sRGB encoder by table. Indexing on sqrt(x) spends the entries where the curve
// is steep, keeping the error below one 16-bit code.
class SrgbTable {
public:
    SrgbTable()
    {
        m_values.resize(kSize + 2);
        for (int i = 0; i <= kSize + 1; ++i) {
            const double root = static_cast<double>(i) / kSize;
            m_values[static_cast<size_t>(i)] = static_cast<float>(
                srgbEncode(std::min(root * root, 1.0)) * 65535.0);
        }
    }

    uint16_t encode(float linear) const
    {
        const float root = std::sqrt(std::clamp(linear, 0.0f, 1.0f)) * kSize;
        const int index = static_cast<int>(root);
        const float fraction = root - static_cast<float>(index);
        const float low = m_values[static_cast<size_t>(index)];
        const float value = low + (m_values[static_cast<size_t>(index) + 1] - low) * fraction;
        return static_cast<uint16_t>(value + 0.5f);
    }

private:
    static constexpr int kSize = 16384;
    std::vector<float> m_values;
};

const SrgbTable &srgbTable()
{
    static const SrgbTable instance;
    return instance;
}

inline float signedCubeRoot(float value)
{
    return std::cbrt(value);
}

// ---- Guided filter ---------------------------------------------------------

// Box filter with clamped edges, as two running-sum passes.
void boxFilter(const std::vector<float> &source,
               int width,
               int height,
               int radius,
               std::vector<float> &result)
{
    std::vector<float> horizontal(source.size());
    const float scale = 1.0f / static_cast<float>(2 * radius + 1);
    parallel::forRows(height, 8, [&](int y, unsigned) {
        const float *row = source.data() + static_cast<size_t>(y) * static_cast<size_t>(width);
        float *out = horizontal.data() + static_cast<size_t>(y) * static_cast<size_t>(width);
        double sum = 0.0;
        for (int dx = -radius; dx <= radius; ++dx) {
            sum += row[std::clamp(dx, 0, width - 1)];
        }
        for (int x = 0; x < width; ++x) {
            out[x] = static_cast<float>(sum) * scale;
            sum += row[std::min(x + radius + 1, width - 1)] -
                   row[std::max(x - radius, 0)];
        }
    });

    result.resize(source.size());
    parallel::forRanges(0, static_cast<size_t>(width), 32,
                        [&](size_t begin, size_t end, unsigned) {
        std::vector<double> sums(end - begin, 0.0);
        const auto at = [&](int y, size_t x) {
            return horizontal[static_cast<size_t>(std::clamp(y, 0, height - 1)) *
                                  static_cast<size_t>(width) + x];
        };
        for (int dy = -radius; dy <= radius; ++dy) {
            for (size_t x = begin; x < end; ++x) {
                sums[x - begin] += at(dy, x);
            }
        }
        for (int y = 0; y < height; ++y) {
            for (size_t x = begin; x < end; ++x) {
                result[static_cast<size_t>(y) * static_cast<size_t>(width) + x] =
                    static_cast<float>(sums[x - begin]) * scale;
                sums[x - begin] += at(y + radius + 1, x) - at(y - radius, x);
            }
        }
    });
}

void guidedCoefficients(const std::vector<float> &signal,
                        int width,
                        int height,
                        int radius,
                        float epsilon,
                        std::vector<float> &a,
                        std::vector<float> &b)
{
    std::vector<float> squared(signal.size());
    for (size_t i = 0; i < signal.size(); ++i) {
        squared[i] = signal[i] * signal[i];
    }
    std::vector<float> mean;
    std::vector<float> meanSquared;
    boxFilter(signal, width, height, radius, mean);
    boxFilter(squared, width, height, radius, meanSquared);

    std::vector<float> rawA(signal.size());
    std::vector<float> rawB(signal.size());
    for (size_t i = 0; i < signal.size(); ++i) {
        const float variance = std::max(meanSquared[i] - mean[i] * mean[i], 0.0f);
        rawA[i] = variance / (variance + epsilon);
        rawB[i] = mean[i] * (1.0f - rawA[i]);
    }
    boxFilter(rawA, width, height, radius, a);
    boxFilter(rawB, width, height, radius, b);
}

// Norm used for all tone decisions. Luminance alone lets saturated colors
// overshoot display white, while the maximum alone darkens them; the mean of the
// two follows perceived brightness and keeps hues intact.
inline float toneNorm(float red, float green, float blue)
{
    const float luminance = kLumaR * red + kLumaG * green + kLumaB * blue;
    return 0.5f * (luminance + std::max({red, green, blue}));
}

void buildToneGuide(SceneImage &scene)
{
    // The smallest mip that keeps a long side of at least 1024 pixels.
    const float *source = scene.rgb.data();
    int width = scene.width;
    int height = scene.height;
    for (const SceneMip &mip : scene.mips) {
        if (std::max(mip.width, mip.height) < 1024) {
            break;
        }
        source = mip.rgb.data();
        width = mip.width;
        height = mip.height;
    }

    const Matrix3 workFromCamera = workFromCameraMatrix(scene);
    Matrix3 scaled = workFromCamera;
    for (size_t row = 0; row < 3; ++row) {
        for (size_t column = 0; column < 3; ++column) {
            scaled[row * 3 + column] *= scene.asShotMul[column];
        }
    }
    std::array<float, 9> m{};
    for (size_t i = 0; i < 9; ++i) {
        m[i] = static_cast<float>(scaled[i]);
    }

    std::vector<float> logNorm(static_cast<size_t>(width) * static_cast<size_t>(height));
    parallel::forRows(height, 16, [&](int y, unsigned) {
        for (int x = 0; x < width; ++x) {
            const size_t index = static_cast<size_t>(y) * static_cast<size_t>(width) +
                                 static_cast<size_t>(x);
            const float *c = source + index * 3;
            const float r = m[0] * c[0] + m[1] * c[1] + m[2] * c[2];
            const float g = m[3] * c[0] + m[4] * c[1] + m[5] * c[2];
            const float b = m[6] * c[0] + m[7] * c[1] + m[8] * c[2];
            // The floor keeps pure noise in the deepest shadows out of the
            // variance estimate.
            logNorm[index] = std::log2(std::max(toneNorm(r, g, b), 1.0e-4f));
        }
    });

    // Two scales combined: the small one follows objects, the large one the
    // lighting. Epsilon is a variance in stops squared. Structure stronger than
    // that is treated as an edge and keeps its contrast with no halo.
    const int longSide = std::max(width, height);
    const int smallRadius = std::max(longSide / 96, 2);
    const int largeRadius = std::max(longSide / 24, 4);
    std::vector<float> a1;
    std::vector<float> b1;
    std::vector<float> a2;
    std::vector<float> b2;
    guidedCoefficients(logNorm, width, height, smallRadius, 0.15f, a1, b1);
    guidedCoefficients(logNorm, width, height, largeRadius, 0.60f, a2, b2);

    scene.guide.width = width;
    scene.guide.height = height;
    scene.guide.a.resize(a1.size());
    scene.guide.b.resize(b1.size());
    for (size_t i = 0; i < a1.size(); ++i) {
        scene.guide.a[i] = 0.5f * (a1[i] + a2[i]);
        scene.guide.b[i] = 0.5f * (b1[i] + b2[i]);
    }
}

void buildMips(SceneImage &scene)
{
    scene.mips.clear();
    const float *source = scene.rgb.data();
    int width = scene.width;
    int height = scene.height;
    while (std::max(width, height) > 256 && std::min(width, height) >= 2) {
        SceneMip mip;
        mip.width = width / 2;
        mip.height = height / 2;
        mip.rgb.resize(static_cast<size_t>(mip.width) * static_cast<size_t>(mip.height) * 3);
        const int sourceWidth = width;
        parallel::forRows(mip.height, 16, [&](int y, unsigned) {
            const float *row0 = source + static_cast<size_t>(y) * 2 *
                                static_cast<size_t>(sourceWidth) * 3;
            const float *row1 = row0 + static_cast<size_t>(sourceWidth) * 3;
            float *out = mip.rgb.data() + static_cast<size_t>(y) *
                         static_cast<size_t>(mip.width) * 3;
            for (int x = 0; x < mip.width; ++x) {
                for (int channel = 0; channel < 3; ++channel) {
                    const size_t left = static_cast<size_t>(x) * 6 + static_cast<size_t>(channel);
                    out[static_cast<size_t>(x) * 3 + static_cast<size_t>(channel)] = 0.25f *
                        (row0[left] + row0[left + 3] + row1[left] + row1[left + 3]);
                }
            }
        });
        scene.mips.push_back(std::move(mip));
        source = scene.mips.back().rgb.data();
        width = scene.mips.back().width;
        height = scene.mips.back().height;
    }
}

} // namespace

void sharpenDisplayRgb(uint16_t *pixels,
                       int width,
                       int height,
                       size_t strideValues,
                       double amount,
                       const float *noiseCodes)
{
    if (amount <= 0.0 || width < 2 || height < 2) {
        return;
    }
    const double normalized = std::clamp(amount, 0.0, 100.0) / 100.0;
    const float strength = static_cast<float>(normalized * (1.0 + normalized * normalized));

    std::vector<float> luma(static_cast<size_t>(width) * static_cast<size_t>(height));
    parallel::forRows(height, 16, [&](int y, unsigned) {
        const uint16_t *row = pixels + static_cast<size_t>(y) * strideValues;
        float *out = luma.data() + static_cast<size_t>(y) * static_cast<size_t>(width);
        for (int x = 0; x < width; ++x) {
            out[x] = (77.0f * row[x * 4] + 150.0f * row[x * 4 + 1] + 29.0f * row[x * 4 + 2]) / 256.0f;
        }
    });

    parallel::forRows(height, 16, [&](int y, unsigned) {
        const float *current = luma.data() + static_cast<size_t>(y) * static_cast<size_t>(width);
        const float *above = luma.data() + static_cast<size_t>(std::max(y - 1, 0)) * static_cast<size_t>(width);
        const float *below = luma.data() + static_cast<size_t>(std::min(y + 1, height - 1)) * static_cast<size_t>(width);
        const float *noiseRow = noiseCodes
            ? noiseCodes + static_cast<size_t>(y) * static_cast<size_t>(width) : nullptr;
        uint16_t *row = pixels + static_cast<size_t>(y) * strideValues;
        for (int x = 0; x < width; ++x) {
            const float left = current[std::max(x - 1, 0)];
            const float right = current[std::min(x + 1, width - 1)];
            const float difference = current[x] - (above[x] + below[x] + left + right) * 0.25f;
            float detail = difference * strength;
            if (noiseRow && noiseRow[x] > 0.0f) {
                // A soft fade rather than a threshold: a hard cut flattens fine
                // texture and leaves only the strong edges, a painted look.
                const float limit = 3.0f * noiseRow[x];
                const float squared = difference * difference;
                detail *= squared / (squared + limit * limit);
            }
            for (int channel = 0; channel < 3; ++channel) {
                row[x * 4 + channel] = static_cast<uint16_t>(std::clamp(
                    row[x * 4 + channel] + detail + 0.5f, 0.0f, 65535.0f));
            }
        }
    });
}

float SceneNoiseModel::sigma(float level) const
{
    const float clamped = std::max(level, 0.0f);
    const float s = std::sqrt(sRead * sRead + sShot * clamped);
    if (clamped <= rStart) {
        return s;
    }
    const float r = std::sqrt(rRead * rRead + rShot * clamped);
    // Blend over half a stop, matching the gradual handover in the merge.
    const float t = std::min((clamped - rStart) / (0.5f * rStart), 1.0f);
    return s + (r - s) * t;
}

bool EditParams::operator==(const EditParams &other) const
{
    return exposureEv == other.exposureEv && temperature == other.temperature &&
           tint == other.tint && contrast == other.contrast &&
           highlights == other.highlights && shadows == other.shadows &&
           whites == other.whites && blacks == other.blacks && gamma == other.gamma &&
           vibrance == other.vibrance && saturation == other.saturation &&
           sharpenAmount == other.sharpenAmount &&
           reconstructHighlights == other.reconstructHighlights &&
           reconstructThreshold == other.reconstructThreshold &&
           reconstructNeutralize == other.reconstructNeutralize;
}

double sceneMiddleGray()
{
    return std::exp2(kMiddleGrayLog2);
}

WhiteBalanceSolution solveWhiteBalance(const SceneImage &scene,
                                       double temperature,
                                       double tint)
{
    WhiteBalanceSolution solution;
    if (!scene.hasColorMatrix) {
        // Without a matrix, move along the blue-amber and green-magenta axes.
        solution.mul = {
            scene.asShotMul[0] * std::exp2(temperature / 200.0),
            scene.asShotMul[1] * std::exp2(-tint / 200.0),
            scene.asShotMul[2] * std::exp2(-temperature / 200.0)};
    } else {
        LocusPosition position = asShotLocus(scene);
        position.mired = std::clamp(position.mired - temperature * kMiredPerSliderUnit,
                                    kMinimumMired, kMaximumMired);
        position.duv += tint / kTintScale;
        const std::array<double, 3> neutral = transformVector(scene.xyzToCam, xyzFromLocus(position));
        for (size_t i = 0; i < 3; ++i) {
            solution.mul[i] = neutral[i] > 1e-9 ? 1.0 / neutral[i] : scene.asShotMul[i];
        }
        solution.kelvin = 1.0e6 / position.mired;
        solution.duv = position.duv;
    }
    const double green = solution.mul[1] > 0.0 ? solution.mul[1] : 1.0;
    for (double &value : solution.mul) {
        value /= green;
    }
    return solution;
}

bool estimateNeutralWhiteBalance(const SceneImage &scene,
                                 int x,
                                 int y,
                                 int width,
                                 int height,
                                 double &temperature,
                                 double &tint)
{
    const int left = std::clamp(x, 0, scene.width);
    const int top = std::clamp(y, 0, scene.height);
    const int right = std::clamp(x + width, 0, scene.width);
    const int bottom = std::clamp(y + height, 0, scene.height);
    double sum[3] = {0.0, 0.0, 0.0};
    size_t count = 0;
    for (int row = top; row < bottom; ++row) {
        for (int column = left; column < right; ++column) {
            const float *pixel = scene.rgb.data() +
                (static_cast<size_t>(row) * static_cast<size_t>(scene.width) +
                 static_cast<size_t>(column)) * 3;
            sum[0] += pixel[0];
            sum[1] += pixel[1];
            sum[2] += pixel[2];
            ++count;
        }
    }
    if (count == 0 || sum[0] <= 0.0 || sum[1] <= 0.0 || sum[2] <= 0.0) {
        return false;
    }

    if (!scene.hasColorMatrix) {
        const double red = sum[1] / sum[0] / scene.asShotMul[0];
        const double blue = sum[1] / sum[2] / scene.asShotMul[2];
        temperature = 100.0 * (std::log2(red) - std::log2(blue));
        tint = -100.0 * (std::log2(red) + std::log2(blue));
    } else {
        const LocusPosition target = locusFromXyz(
            transformVector(inverse(scene.xyzToCam), {sum[0], sum[1], sum[2]}));
        const LocusPosition base = asShotLocus(scene);
        temperature = (base.mired - target.mired) / kMiredPerSliderUnit;
        tint = (target.duv - base.duv) * kTintScale;
    }
    return std::isfinite(temperature) && std::isfinite(tint);
}

void rotateScene(SceneImage &scene, int degrees)
{
    const int turns = ((degrees / 90) % 4 + 4) % 4;
    if (turns == 0 || !scene.isValid()) {
        return;
    }
    const int width = scene.width;
    const int height = scene.height;
    const int newWidth = turns == 2 ? width : height;
    const int newHeight = turns == 2 ? height : width;
    std::vector<float> rotated(scene.rgb.size());
    parallel::forRows(height, 16, [&](int y, unsigned) {
        for (int x = 0; x < width; ++x) {
            int nx = x;
            int ny = y;
            if (turns == 1) {
                nx = height - 1 - y;
                ny = x;
            } else if (turns == 2) {
                nx = width - 1 - x;
                ny = height - 1 - y;
            } else {
                nx = y;
                ny = width - 1 - x;
            }
            const float *source = scene.rgb.data() +
                (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 3;
            float *target = rotated.data() +
                (static_cast<size_t>(ny) * static_cast<size_t>(newWidth) + static_cast<size_t>(nx)) * 3;
            target[0] = source[0];
            target[1] = source[1];
            target[2] = source[2];
        }
    });
    scene.rgb = std::move(rotated);

    scene.width = newWidth;
    scene.height = newHeight;
}

void finalizeScene(SceneImage &scene)
{
    if (!scene.isValid()) {
        return;
    }
    buildMips(scene);
    buildToneGuide(scene);
}

SceneRenderer::SceneRenderer(const SceneImage &scene, const EditParams &params)
    : m_scene(scene)
    , m_params(params)
{
    const WhiteBalanceSolution balance =
        solveWhiteBalance(scene, params.temperature, params.tint);
    const Matrix3 workFromCamera = workFromCameraMatrix(scene);
    const double exposure = std::exp2(params.exposureEv);
    m_exposureLog2 = static_cast<float>(params.exposureEv);
    for (size_t row = 0; row < 3; ++row) {
        for (size_t column = 0; column < 3; ++column) {
            m_workFromCamera[row * 3 + column] = static_cast<float>(
                workFromCamera[row * 3 + column] * balance.mul[column] * exposure);
        }
    }

    m_reconstruct = params.reconstructHighlights && scene.channelCeiling[1] < 1.0e8f;
    const float thresholdFraction = static_cast<float>(
        std::clamp(params.reconstructThreshold, 50.0, 99.0) / 100.0);
    for (size_t channel = 0; channel < 3; ++channel) {
        const float ceiling = scene.channelCeiling[channel];
        m_ceilingStart[channel] = ceiling * thresholdFraction;
        m_ceilingInverseRange[channel] = 1.0f / (ceiling * (1.0f - thresholdFraction));
        m_balance[channel] = static_cast<float>(balance.mul[channel]);
    }
    m_neutralize = static_cast<float>(std::clamp(params.reconstructNeutralize, 0.0, 100.0) / 100.0);

    m_saturationScale = static_cast<float>(1.0 + params.saturation / 100.0);
    m_vibrance = static_cast<float>(params.vibrance / 100.0);

    const double slope = kBaseSlope * std::exp2(params.contrast / 100.0 * 0.70);
    const double shoulderX = kMiddleGrayLog2 + kShoulderOffset + params.whites / 100.0 * 1.25;
    const double gamma = std::clamp(params.gamma, 0.1, 10.0);
    const double black = -params.blacks / 100.0 * 0.012;
    const double toeSoftness = 0.0015;

    const double readFloor = std::max(static_cast<double>(scene.noise.sRead), 1e-5);
    const double noiseLog2 = std::log2(3.0 * readFloor);
    m_noiseLog2 = static_cast<float>(noiseLog2);

    const double highlights = std::clamp(params.highlights / 100.0, -1.0, 1.0);
    const double shadows = std::clamp(params.shadows / 100.0, -1.0, 1.0);
    const double highlightPivot = kMiddleGrayLog2 + 1.0;
    const double shadowPivot = kMiddleGrayLog2 - 0.25;

    const auto blackAdjusted = [&](double linear) {
        const double shifted = linear - black;
        return 0.5 * (shifted + std::sqrt(shifted * shifted + 4.0 * toeSoftness * toeSoftness * (black > 0.0 ? 1.0 : 0.0)));
    };
    const double whiteOut = blackAdjusted(1.0);

    m_toneLut.resize(kLutSize);
    m_deltaLut.resize(kLutSize);
    m_detailLut.resize(kLutSize);
    m_chromaLut.resize(kLutSize);
    for (int i = 0; i < kLutSize; ++i) {
        const double x = kLutMinimum + static_cast<double>(i) / kLutPerStop;

        double linear = std::exp2(curveLog2(x, slope, shoulderX));
        linear = blackAdjusted(linear) / whiteOut;
        if (gamma != 1.0) {
            linear = srgbDecode(std::pow(srgbEncode(std::min(linear, 1.0)), 1.0 / gamma));
        }
        m_toneLut[static_cast<size_t>(i)] = static_cast<float>(linear);
        m_chromaLut[static_cast<size_t>(i)] = static_cast<float>(
            kBaseChroma + (kWhiteChroma - kBaseChroma) * smoothstep(0.40, 0.92, linear));

        // Highlights and Shadows work on the base layer, so the amount of
        // lift or compression follows areas rather than single pixels.
        double delta = 0.0;
        if (highlights < 0.0) {
            // Compression alone would pack the range above S white against the
            // shoulder of the tone curve. Moving the whole bright end down as
            // well gives those stops room on the display. Together the two terms
            // stay below a slope of 1, so tones never invert.
            delta += highlights * (0.60 * smoothstep(highlightPivot - 1.5, highlightPivot + 1.5, x) +
                                   0.55 * softPlus(x - highlightPivot, 0.6));
        } else if (highlights > 0.0) {
            delta += highlights * 0.45 * softPlus(x - highlightPivot, 0.6);
        }
        double lift = 0.0;
        if (shadows > 0.0) {
            // Fades out below the noise floor: there is no detail down there to
            // recover, only noise to amplify.
            lift = shadows * 0.65 * softPlus(shadowPivot - x, 0.6) *
                   smoothstep(noiseLog2 - 2.0, noiseLog2 + 0.5, x);
            delta += lift;
        } else if (shadows < 0.0) {
            delta += shadows * 0.45 * softPlus(shadowPivot - x, 0.6);
        }
        m_deltaLut[static_cast<size_t>(i)] = static_cast<float>(delta);

        // Near the noise floor the fine detail is mostly noise, so lifted areas
        // pass less of it through. The gain is then spent on the base layer.
        const double noiseShare = 1.0 - smoothstep(noiseLog2, noiseLog2 + 3.0, x);
        m_detailLut[static_cast<size_t>(i)] = static_cast<float>(
            1.0 - 0.55 * noiseShare * lift / (lift + 1.5));
    }
}

double SceneRenderer::toneResponse(double sceneLuminance) const
{
    const float x = static_cast<float>(
        std::log2(std::max(sceneLuminance, 1e-12)) + m_params.exposureEv);
    return lookup(m_toneLut, x);
}

// Highlight reconstruction, in camera RGB. Every channel has a level where it stops
// recording, and the white-balance gains then pull the capped channels apart, which
// is what turns saturated highlights pink. All of this only ever raises values, and
// a channel that is below its ceiling is real data: a bright blue sky has high green
// and blue, but they are under their ceilings and it is left alone.
void SceneRenderer::reconstructPixel(float *camera) const
{
    float capped[3];
    float most = 0.0f;
    for (int c = 0; c < 3; ++c) {
        const float t = std::clamp(
            (camera[c] - m_ceilingStart[static_cast<size_t>(c)]) *
                m_ceilingInverseRange[static_cast<size_t>(c)],
            0.0f, 1.0f);
        capped[c] = t * t * (3.0f - 2.0f * t);
        most = std::max(most, capped[c]);
    }
    if (most <= 0.0f) {
        return;
    }

    const float balanced[3] = {
        camera[0] * m_balance[0], camera[1] * m_balance[1], camera[2] * m_balance[2]};

    // A capped channel lost whatever was above its ceiling. The other two say how
    // bright the light really was, so their mean is the estimate for it.
    float rebuilt[3];
    for (int c = 0; c < 3; ++c) {
        const float others = 0.5f * (balanced[(c + 1) % 3] + balanced[(c + 2) % 3]);
        rebuilt[c] = balanced[c] + capped[c] * std::max(others - balanced[c], 0.0f);
    }

    // With two or more channels capped, the ratio between them is meaningless: a
    // blue lamp clips green and blue together and records as cyan. Such pixels
    // fade toward neutral, as they do in film, keeping a trace of their hue.
    const float highest = std::max({capped[0], capped[1], capped[2]});
    const float lowest = std::min({capped[0], capped[1], capped[2]});
    const float second = capped[0] + capped[1] + capped[2] - highest - lowest;
    const float neutralize = second * m_neutralize;
    const float neutral = std::max({rebuilt[0], rebuilt[1], rebuilt[2]});
    for (int c = 0; c < 3; ++c) {
        const float value = rebuilt[c] + (neutral - rebuilt[c]) * neutralize;
        camera[c] = value / m_balance[static_cast<size_t>(c)];
    }
}

void SceneRenderer::shadeRow(const float *camera,
                             int count,
                             const float *guideX,
                             float guideY,
                             uint16_t *output,
                             float *noiseCodes,
                             float noiseScale) const
{
    const std::array<float, 9> &m = m_workFromCamera;
    const LinearToOklab &lab = oklab();
    const SrgbTable &encoder = srgbTable();
    const ToneGuide &guide = m_scene.guide;
    const bool hasGuide = guide.width > 1 && guide.height > 1;

    int gy0 = 0;
    int gy1 = 0;
    float fy = 0.0f;
    if (hasGuide) {
        const float clampedY = std::clamp(guideY, 0.0f, static_cast<float>(guide.height - 1));
        gy0 = std::min(static_cast<int>(clampedY), guide.height - 2);
        gy1 = gy0 + 1;
        fy = clampedY - static_cast<float>(gy0);
    }
    const float *a0 = hasGuide ? guide.a.data() + static_cast<size_t>(gy0) * static_cast<size_t>(guide.width) : nullptr;
    const float *a1 = hasGuide ? guide.a.data() + static_cast<size_t>(gy1) * static_cast<size_t>(guide.width) : nullptr;
    const float *b0 = hasGuide ? guide.b.data() + static_cast<size_t>(gy0) * static_cast<size_t>(guide.width) : nullptr;
    const float *b1 = hasGuide ? guide.b.data() + static_cast<size_t>(gy1) * static_cast<size_t>(guide.width) : nullptr;

    for (int i = 0; i < count; ++i) {
        float c[3] = {camera[i * 3], camera[i * 3 + 1], camera[i * 3 + 2]};
        if (m_reconstruct) {
            reconstructPixel(c);
        }
        float red = m[0] * c[0] + m[1] * c[1] + m[2] * c[2];
        float green = m[3] * c[0] + m[4] * c[1] + m[5] * c[2];
        float blue = m[6] * c[0] + m[7] * c[1] + m[8] * c[2];

        const float norm = toneNorm(red, green, blue);
        uint16_t *pixel = output + static_cast<size_t>(i) * 4;
        if (!(norm > 1.0e-9f)) {
            pixel[0] = pixel[1] = pixel[2] = encoder.encode(lookup(m_toneLut, kLutMinimum));
            pixel[3] = 65535;
            if (noiseCodes) {
                noiseCodes[i] = 0.0f;
            }
            continue;
        }

        const float logNorm = std::log2(norm);
        float base = logNorm;
        if (hasGuide) {
            const float clampedX = std::clamp(guideX[i], 0.0f, static_cast<float>(guide.width - 1));
            const int gx0 = std::min(static_cast<int>(clampedX), guide.width - 2);
            const float fx = clampedX - static_cast<float>(gx0);
            const float w00 = (1.0f - fx) * (1.0f - fy);
            const float w10 = fx * (1.0f - fy);
            const float w01 = (1.0f - fx) * fy;
            const float w11 = fx * fy;
            const float a = a0[gx0] * w00 + a0[gx0 + 1] * w10 + a1[gx0] * w01 + a1[gx0 + 1] * w11;
            const float b = b0[gx0] * w00 + b0[gx0 + 1] * w10 + b1[gx0] * w01 + b1[gx0 + 1] * w11;
            // The guide was built without the Exposure setting, which is a pure
            // shift in this domain.
            base = a * (logNorm - m_exposureLog2) + b + m_exposureLog2;
        }

        const float delta = lookup(m_deltaLut, base);
        const float detail = lookup(m_detailLut, base);
        const float toned = base + delta + (logNorm - base) * detail;
        const float display = lookup(m_toneLut, toned);
        const float ratio = display / norm;

        if (noiseCodes) {
            // Size of the sensor noise here, in output codes, in the two zones
            // where sharpening must hold back: deep shadows and R-sourced
            // highlights. Everywhere else stays 0, so texture sharpens freely.
            const float sceneLog2 = logNorm - m_exposureLog2;
            const float level = std::exp2(sceneLog2);
            const float rStart = m_scene.noise.rStart;
            const float shadowZone = 1.0f - static_cast<float>(
                smoothstep(m_noiseLog2 + 1.0, m_noiseLog2 + 4.0, sceneLog2));
            const float highlightZone = static_cast<float>(
                smoothstep(rStart, 1.4 * rStart, level));
            const float zone = std::max(shadowZone, highlightZone);
            float codes = 0.0f;
            if (zone > 0.0f) {
                const float stops = std::min(
                    1.4427f * m_scene.noise.sigma(level) / level * noiseScale, 2.0f);
                codes = zone * static_cast<float>(
                    encoder.encode(lookup(m_toneLut, toned + stops)) - encoder.encode(display));
            }
            noiseCodes[i] = codes;
        }

        red *= ratio;
        green *= ratio;
        blue *= ratio;

        {
            const float l = lab.toLms[0] * red + lab.toLms[1] * green + lab.toLms[2] * blue;
            const float mm = lab.toLms[3] * red + lab.toLms[4] * green + lab.toLms[5] * blue;
            const float s = lab.toLms[6] * red + lab.toLms[7] * green + lab.toLms[8] * blue;
            const float lRoot = signedCubeRoot(l);
            const float mRoot = signedCubeRoot(mm);
            const float sRoot = signedCubeRoot(s);
            const float lightness = lab.toLab[0] * lRoot + lab.toLab[1] * mRoot + lab.toLab[2] * sRoot;
            float chromaA = lab.toLab[3] * lRoot + lab.toLab[4] * mRoot + lab.toLab[5] * sRoot;
            float chromaB = lab.toLab[6] * lRoot + lab.toLab[7] * mRoot + lab.toLab[8] * sRoot;

            float scale = m_saturationScale * lookup(m_chromaLut, toned);
            if (m_vibrance != 0.0f) {
                // Vibrance favors muted colors and goes easy on skin tones,
                // which sit around 45 degrees in the a/b plane.
                const float chroma = std::sqrt(chromaA * chromaA + chromaB * chromaB);
                float muted = 1.0f - std::min(chroma / 0.25f, 1.0f);
                muted *= muted;
                const float skinDot = chroma > 1e-5f
                    ? (chromaA + chromaB) * 0.70710678f / chroma : 0.0f;
                const float skin = std::max(skinDot, 0.0f);
                const float protection = 1.0f - 0.6f * skin * skin * skin * skin;
                scale *= 1.0f + m_vibrance * (0.25f + 0.75f * muted) * protection;
            }
            chromaA *= scale;
            chromaB *= scale;

            const float lr = lab.fromLab[0] * lightness + lab.fromLab[1] * chromaA + lab.fromLab[2] * chromaB;
            const float mr = lab.fromLab[3] * lightness + lab.fromLab[4] * chromaA + lab.fromLab[5] * chromaB;
            const float sr = lab.fromLab[6] * lightness + lab.fromLab[7] * chromaA + lab.fromLab[8] * chromaB;
            const float lc = lr * lr * lr;
            const float mc = mr * mr * mr;
            const float sc = sr * sr * sr;
            red = lab.fromLms[0] * lc + lab.fromLms[1] * mc + lab.fromLms[2] * sc;
            green = lab.fromLms[3] * lc + lab.fromLms[4] * mc + lab.fromLms[5] * sc;
            blue = lab.fromLms[6] * lc + lab.fromLms[7] * mc + lab.fromLms[8] * sc;
        }

        const std::array<float, 9> &g = lab.rec709FromRec2020;
        float outRed = g[0] * red + g[1] * green + g[2] * blue;
        float outGreen = g[3] * red + g[4] * green + g[5] * blue;
        float outBlue = g[6] * red + g[7] * green + g[8] * blue;

        // Out-of-gamut colors move in a straight line toward a gray, which keeps
        // their hue. In the shadows and midtones that gray is the color's own
        // luminance. Bright colors use the brightness the tone curve asked for
        // instead: a blue lamp has very little luminance, and aiming at it would
        // leave its hottest core darker than the glow around it.
        const float luminance = std::clamp(
            0.2126f * outRed + 0.7152f * outGreen + 0.0722f * outBlue, 0.0f, 1.0f);
        const float highest = std::max({outRed, outGreen, outBlue});
        const float lowest = std::min({outRed, outGreen, outBlue});
        if (highest > 1.0f || lowest < 0.0f) {
            const float t = std::clamp((display - 0.5f) * 2.5f, 0.0f, 1.0f);
            const float bright = t * t * (3.0f - 2.0f * t);
            const float anchor = std::clamp(
                luminance + std::max(display - luminance, 0.0f) * bright, 0.0f, 0.999f);
            float limit = 1.0f;
            if (highest > 1.0f) {
                limit = std::min(limit, (1.0f - anchor) / (highest - anchor));
            }
            if (lowest < 0.0f) {
                limit = std::min(limit, anchor / (anchor - lowest));
            }
            outRed = anchor + (outRed - anchor) * limit;
            outGreen = anchor + (outGreen - anchor) * limit;
            outBlue = anchor + (outBlue - anchor) * limit;
        }

        pixel[0] = encoder.encode(outRed);
        pixel[1] = encoder.encode(outGreen);
        pixel[2] = encoder.encode(outBlue);
        pixel[3] = 65535;
    }
}

void SceneRenderer::render(double sourceX,
                           double sourceY,
                           double sourceWidth,
                           double sourceHeight,
                           int outWidth,
                           int outHeight,
                           uint16_t *output,
                           size_t strideValues) const
{
    if (outWidth <= 0 || outHeight <= 0) {
        return;
    }
    if (m_params.sharpenAmount <= 0.0) {
        renderPlain(sourceX, sourceY, sourceWidth, sourceHeight, outWidth, outHeight,
                    output, strideValues, nullptr);
        return;
    }

    // One extra pixel on each side gives the kernel real neighbors, so a tile of
    // the view sharpens exactly like the same area of a full render.
    const double stepX = sourceWidth / outWidth;
    const double stepY = sourceHeight / outHeight;
    const int paddedWidth = outWidth + 2;
    const int paddedHeight = outHeight + 2;
    const size_t paddedStride = static_cast<size_t>(paddedWidth) * 4;
    std::vector<uint16_t> padded(paddedStride * static_cast<size_t>(paddedHeight));
    std::vector<float> noise(static_cast<size_t>(paddedWidth) * static_cast<size_t>(paddedHeight));
    renderPlain(sourceX - stepX, sourceY - stepY,
                sourceWidth + 2.0 * stepX, sourceHeight + 2.0 * stepY,
                paddedWidth, paddedHeight, padded.data(), paddedStride, noise.data());
    sharpenDisplayRgb(padded.data(), paddedWidth, paddedHeight, paddedStride,
                      m_params.sharpenAmount, noise.data());
    for (int y = 0; y < outHeight; ++y) {
        std::copy_n(padded.data() + static_cast<size_t>(y + 1) * paddedStride + 4,
                    static_cast<size_t>(outWidth) * 4,
                    output + static_cast<size_t>(y) * strideValues);
    }
}

void SceneRenderer::renderPlain(double sourceX,
                                double sourceY,
                                double sourceWidth,
                                double sourceHeight,
                                int outWidth,
                                int outHeight,
                                uint16_t *output,
                                size_t strideValues,
                                float *noiseCodes) const
{
    if (outWidth <= 0 || outHeight <= 0 || !m_scene.isValid()) {
        return;
    }

    const double pixelStepX = sourceWidth / outWidth;
    const double pixelStepY = sourceHeight / outHeight;

    // Pick the mip closest to the output size, never one coarser than needed.
    const float *pixels = m_scene.rgb.data();
    int levelWidth = m_scene.width;
    int levelHeight = m_scene.height;
    double levelScale = 1.0;
    const double step = std::min(pixelStepX, pixelStepY);
    for (const SceneMip &mip : m_scene.mips) {
        if (levelScale * 2.0 > step) {
            break;
        }
        pixels = mip.rgb.data();
        levelWidth = mip.width;
        levelHeight = mip.height;
        levelScale *= 2.0;
    }

    const ToneGuide &guide = m_scene.guide;
    const double guideScaleX = guide.width > 0
        ? static_cast<double>(guide.width) / m_scene.width : 0.0;
    const double guideScaleY = guide.height > 0
        ? static_cast<double>(guide.height) / m_scene.height : 0.0;

    const bool exactGrid = levelScale == 1.0 &&
        std::abs(pixelStepX - 1.0) < 1e-9 && std::abs(pixelStepY - 1.0) < 1e-9 &&
        sourceX == std::floor(sourceX) && sourceY == std::floor(sourceY);

    parallel::forRows(outHeight, 8, [&](int y, unsigned) {
        std::vector<float> row(static_cast<size_t>(outWidth) * 3);
        std::vector<float> guideX(static_cast<size_t>(outWidth));
        const double centerY = sourceY + (y + 0.5) * pixelStepY;

        for (int x = 0; x < outWidth; ++x) {
            const double centerX = sourceX + (x + 0.5) * pixelStepX;
            guideX[static_cast<size_t>(x)] = static_cast<float>(centerX * guideScaleX - 0.5);
            float *target = row.data() + static_cast<size_t>(x) * 3;

            if (exactGrid) {
                const int px = std::clamp(static_cast<int>(sourceX) + x, 0, levelWidth - 1);
                const int py = std::clamp(static_cast<int>(sourceY) + y, 0, levelHeight - 1);
                const float *source = pixels +
                    (static_cast<size_t>(py) * static_cast<size_t>(levelWidth) + static_cast<size_t>(px)) * 3;
                target[0] = source[0];
                target[1] = source[1];
                target[2] = source[2];
                continue;
            }

            const double levelX = centerX / levelScale - 0.5;
            const double levelY = centerY / levelScale - 0.5;
            const int x0 = std::clamp(static_cast<int>(std::floor(levelX)), 0, levelWidth - 1);
            const int y0 = std::clamp(static_cast<int>(std::floor(levelY)), 0, levelHeight - 1);
            const int x1 = std::min(x0 + 1, levelWidth - 1);
            const int y1 = std::min(y0 + 1, levelHeight - 1);
            const float fx = static_cast<float>(std::clamp(levelX - x0, 0.0, 1.0));
            const float fy = static_cast<float>(std::clamp(levelY - y0, 0.0, 1.0));
            const float *p00 = pixels + (static_cast<size_t>(y0) * static_cast<size_t>(levelWidth) + static_cast<size_t>(x0)) * 3;
            const float *p10 = pixels + (static_cast<size_t>(y0) * static_cast<size_t>(levelWidth) + static_cast<size_t>(x1)) * 3;
            const float *p01 = pixels + (static_cast<size_t>(y1) * static_cast<size_t>(levelWidth) + static_cast<size_t>(x0)) * 3;
            const float *p11 = pixels + (static_cast<size_t>(y1) * static_cast<size_t>(levelWidth) + static_cast<size_t>(x1)) * 3;
            for (int c = 0; c < 3; ++c) {
                const float top = p00[c] + (p10[c] - p00[c]) * fx;
                const float bottom = p01[c] + (p11[c] - p01[c]) * fx;
                target[c] = top + (bottom - top) * fy;
            }
        }

        shadeRow(row.data(), outWidth, guideX.data(),
                 static_cast<float>(centerY * guideScaleY - 0.5),
                 output + static_cast<size_t>(y) * strideValues,
                 noiseCodes ? noiseCodes + static_cast<size_t>(y) * static_cast<size_t>(outWidth)
                            : nullptr,
                 static_cast<float>(1.0 / levelScale));
    });
}

} // namespace superccd
