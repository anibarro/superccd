#ifndef SRMERGE_H
#define SRMERGE_H

#include <cstddef>
#include <cstdint>
#include <vector>

// Sensor-level S + R merge for the Fuji Super CCD SR (S3 Pro / S5 Pro).
//
// Everything here works on the 45-degree "canvas" CFA that LibRaw produces,
// where column/row steps follow the honeycomb lattice. Facts measured on real
// S3 Pro and S5 Pro files that this code depends on:
//
//  * R photosites are NOT co-sited with S photosites. An R sample stored at
//    canvas (c, r) sits physically at (c + 0.5, r - 0.5) relative to the S
//    sample stored at the same index: exactly one pixel to the side in the
//    rectified 12 MP image, i.e. in the gaps of the S honeycomb.
//  * S stays linear right up to clipping, and clips at a level that depends on
//    ISO and channel (15870 at ISO 100, down to about 15831 at ISO 1600).
//  * The S/R ratio is not the nominal 16: about 18 / 16.5 / 15.4 for R/G/B, and
//    it moves with aperture and ISO, so it has to be fitted per image.
//  * R is roughly 8x noisier than S at equal brightness, so S is the better
//    signal for as long as it is unclipped.
//
// The merged result is linear float where 1.0 is the nominal S white level. It
// is never clamped at the top, so all of the R headroom survives.
namespace superccd {

// Color classes used for every per-color table here: 0 = R, 1 = G, 2 = B.
// LibRaw channel 3 (the second green) folds into class 1.
inline int srColorClass(uint8_t channel)
{
    return channel == 3 ? 1 : static_cast<int>(channel);
}

struct SrCalibration {
    double gain[3] = {18.2, 16.5, 15.4}; // S ADU per R ADU
    double intercept[3] = {0.0, 0.0, 0.0}; // S ADU added after the gain
    double sClip[3] = {15832.0, 15832.0, 15832.0}; // ADU where S saturates
    double rClip[3] = {5900.0, 5900.0, 5900.0}; // ADU where R saturates, measured per file
    int fittedBlocks[3] = {0, 0, 0}; // Flat blocks behind each gain fit
};

struct SrMergeParams {
    // S brightness where R starts to contribute, relative to the clip level.
    double start = 0.75;
    // Width control (0..1), mapped onto 0..0.4 of the S range.
    double width = 0.5;
    // Shape of the transition: 0 is a straight ramp, 1 a late, sharp knee.
    double smoothness = 0.65;
};

// Weight of R (0..1) for a sample at normalizedS = S / sClip.
double srBlendWeight(double normalizedS, const SrMergeParams &params);

// Resamples R onto the physical S photosite positions. Channels of 0xFF mark
// sites outside the sensor diamond, and a zero R sample is treated as missing.
void projectRToSSites(const std::vector<uint16_t> &rCfa,
                      const std::vector<uint8_t> &channels,
                      int width,
                      int height,
                      std::vector<float> &projected);

// Finds the clip levels from the histogram pile-ups and fits R to S per color
// on flat image blocks.
SrCalibration calibrateSr(const std::vector<uint16_t> &sCfa,
                          const std::vector<float> &projectedR,
                          const std::vector<uint8_t> &channels,
                          int width,
                          int height,
                          double nominalWhite,
                          double iso);

// Edge-preserving noise reduction for the projected R plane, scaled to the R
// noise at each level. Strength 0 leaves the plane untouched and 1 is the strongest
// setting. Only sites where R contributes to the merge are filtered, so the cost
// tracks highlight area.
void denoiseProjectedR(std::vector<float> &projectedR,
                       const std::vector<uint16_t> &sCfa,
                       const std::vector<uint8_t> &channels,
                       int width,
                       int height,
                       const SrCalibration &calibration,
                       const SrMergeParams &params,
                       double iso,
                       double strength);

struct SrMergeStats {
    double maxValue = 0.0; // Highest merged value, 1.0 = nominal S white
    std::size_t blendedSites = 0;
    std::size_t replacedSites = 0;
};

// Writes the merged linear CFA (1.0 = nominalWhite). Sites outside the diamond
// are written as 0.
SrMergeStats mergeSr(const std::vector<uint16_t> &sCfa,
                     const std::vector<float> &projectedR,
                     const std::vector<uint8_t> &channels,
                     int width,
                     int height,
                     const SrCalibration &calibration,
                     const SrMergeParams &params,
                     double nominalWhite,
                     std::vector<float> &merged);

} // namespace superccd

#endif // SRMERGE_H
