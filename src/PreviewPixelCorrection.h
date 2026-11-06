#ifndef PREVIEWPIXELCORRECTION_H
#define PREVIEWPIXELCORRECTION_H

#include <cstddef>
#include <vector>

namespace PreviewPixelCorrection {

// Pulls isolated bright or dark pixels back to their neighborhood. Works on
// interleaved linear RGB with 1.0 as S white, keeping color differences intact.
size_t suppressIsolatedLumaOutliers(std::vector<float> &rgb, int width, int height);

}

#endif
