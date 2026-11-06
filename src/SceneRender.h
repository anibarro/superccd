#ifndef SCENERENDER_H
#define SCENERENDER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// Scene-referred rendering.
//
// A SceneImage holds demosaiced camera RGB as linear float, where 1.0 is the
// sensor white level of the S photosites. That anchor is a fixed property of the
// camera and never depends on image content, so a frame shot two stops under
// looks two stops under. The R highlights simply continue above 1.0 (up to about
// 16.0), and nothing in the pipeline clamps them until the final display encode,
// where the tone curve approaches white without ever reaching it.
namespace superccd {

// Noise of one scene sample, normalized so that 1.0 is S white. S samples and R
// samples follow different models because R carries about 8x the relative noise.
struct SceneNoiseModel {
    float sRead = 0.0006f;
    float sShot = 0.00006f;
    float rRead = 0.0072f;
    float rShot = 0.0020f;
    float rStart = 0.95f; // Scene level above which samples are R-dominated

    float sigma(float level) const;
};

struct SceneMip {
    int width = 0;
    int height = 0;
    std::vector<float> rgb;
};

// Edge-preserving base layer of the log luminance, stored as guided-filter
// coefficients at reduced size. Because the coefficients are applied to the
// full-resolution signal, a 100% crop and a full export get the same local tone.
struct ToneGuide {
    int width = 0;
    int height = 0;
    std::vector<float> a;
    std::vector<float> b;
};

struct SceneImage {
    int width = 0;
    int height = 0;
    std::vector<float> rgb; // Interleaved camera RGB, white balance not applied

    std::vector<SceneMip> mips; // Half size, quarter size, ...
    ToneGuide guide;

    std::array<double, 3> asShotMul = {1.0, 1.0, 1.0}; // Normalized to green = 1
    std::array<double, 9> xyzToCam = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    bool hasColorMatrix = false;
    double iso = 100.0;
    double headroomEv = 0.0; // Measured stops of real data above S white
    // Camera RGB level where each channel stops recording. The defaults mean
    // "never", which turns highlight reconstruction off.
    std::array<float, 3> channelCeiling = {1.0e9f, 1.0e9f, 1.0e9f};
    SceneNoiseModel noise;

    bool isValid() const
    {
        return width > 0 && height > 0 &&
            rgb.size() >= static_cast<size_t>(width) * static_cast<size_t>(height) * 3;
    }
};

using SceneImagePtr = std::shared_ptr<const SceneImage>;

// Develop settings. All ranges follow Camera Raw conventions: 0 leaves the
// as-shot rendering unchanged.
struct EditParams {
    double exposureEv = 0.0;   // -5..5 stops
    double temperature = 0.0;  // -200..200, relative to as shot, + is warmer
    double tint = 0.0;         // -100..100, relative to as shot, + is magenta
    double contrast = 0.0;     // -100..100
    double highlights = 0.0;   // -100..100, - recovers
    double shadows = 0.0;      // -100..100, + recovers
    double whites = 0.0;       // -100..100
    double blacks = 0.0;       // -100..100
    double gamma = 1.0;        // 0.3..3.0, midtone gamma as in Levels
    double vibrance = 0.0;     // -100..100
    double saturation = 0.0;   // -100..100
    double sharpenAmount = 0.0;  // 0..100
    bool reconstructHighlights = true;
    double reconstructThreshold = 92.0; // 80..100, % of the ceiling where repair starts
    double reconstructNeutralize = 60.0; // 0..100, how far saturated areas go to white

    bool operator==(const EditParams &other) const;
    bool operator!=(const EditParams &other) const { return !(*this == other); }
};

struct WhiteBalanceSolution {
    std::array<double, 3> mul = {1.0, 1.0, 1.0};
    double kelvin = 0.0; // 0 when the scene has no color matrix
    double duv = 0.0;
};

WhiteBalanceSolution solveWhiteBalance(const SceneImage &scene,
                                       double temperature,
                                       double tint);

// Finds the Temperature and Tint that make the given area neutral. Returns false
// when the area has no usable signal.
bool estimateNeutralWhiteBalance(const SceneImage &scene,
                                 int x,
                                 int y,
                                 int width,
                                 int height,
                                 double &temperature,
                                 double &tint);

// Rotates clockwise by 90, 180 or 270 degrees.
void rotateScene(SceneImage &scene, int degrees);

// Builds the mips and the tone guide. Call once after the pixels are final.
void finalizeScene(SceneImage &scene);

// Luminance sharpening of finished 16-bit sRGB pixels (4 values per pixel): one
// light unsharp pass at output size, as the last step of the pipeline. Working
// here rather than in linear light keeps it natural looking, with fine texture
// intact. noiseCodes, when given, holds the noise level per pixel in output codes
// for the areas where sharpening should fade out, and 0 elsewhere.
void sharpenDisplayRgb(uint16_t *pixels,
                       int width,
                       int height,
                       size_t strideValues,
                       double amount,
                       const float *noiseCodes);

class SceneRenderer {
public:
    SceneRenderer(const SceneImage &scene, const EditParams &params);

    // Renders a source rectangle (in full-size scene pixels) to outWidth x
    // outHeight. Output is 16-bit sRGB, 4 values per pixel (R, G, B, padding),
    // with rows strideValues apart.
    void render(double sourceX,
                double sourceY,
                double sourceWidth,
                double sourceHeight,
                int outWidth,
                int outHeight,
                uint16_t *output,
                size_t strideValues) const;

    // Luminance tone response for tests and diagnostics: the display-linear
    // output for a scene luminance, without local adjustments.
    double toneResponse(double sceneLuminance) const;

private:
    void reconstructPixel(float *camera) const;
    void renderPlain(double sourceX,
                     double sourceY,
                     double sourceWidth,
                     double sourceHeight,
                     int outWidth,
                     int outHeight,
                     uint16_t *output,
                     size_t strideValues,
                     float *noiseCodes) const;
    void shadeRow(const float *camera,
                  int count,
                  const float *guideX,
                  float guideY,
                  uint16_t *output,
                  float *noiseCodes,
                  float noiseScale) const;

    const SceneImage &m_scene;
    EditParams m_params;

    std::array<float, 9> m_workFromCamera{};
    bool m_reconstruct = false;
    std::array<float, 3> m_ceilingStart{};
    std::array<float, 3> m_ceilingInverseRange{};
    std::array<float, 3> m_balance{};
    float m_neutralize = 0.0f;
    float m_exposureLog2 = 0.0f;
    float m_noiseLog2 = 0.0f;
    float m_saturationScale = 1.0f;
    float m_vibrance = 0.0f;

    std::vector<float> m_toneLut;
    std::vector<float> m_deltaLut;
    std::vector<float> m_detailLut;
    std::vector<float> m_chromaLut;
};

// Middle gray in scene units (1.0 = S white), as used by the base tone curve.
double sceneMiddleGray();

} // namespace superccd

#endif // SCENERENDER_H
