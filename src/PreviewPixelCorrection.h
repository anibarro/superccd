#ifndef PREVIEWPIXELCORRECTION_H
#define PREVIEWPIXELCORRECTION_H

#include <QImage>

#include <cstddef>

namespace PreviewPixelCorrection {

size_t suppressIsolatedLumaOutliers(QImage &image);

}

#endif
