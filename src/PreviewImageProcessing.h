#ifndef PREVIEWIMAGEPROCESSING_H
#define PREVIEWIMAGEPROCESSING_H

#include "SceneRender.h"

#include <QImage>
#include <QRectF>
#include <QSize>

// QImage adapters around SceneRenderer.
namespace PreviewImageProcessing {

// Renders a source rectangle (in scene pixels) for display.
QImage renderRegion8(const superccd::SceneImage &scene,
                     const superccd::EditParams &params,
                     const QRectF &source,
                     const QSize &outputSize);

// Renders the whole scene for display and for the scopes.
QImage renderFull8(const superccd::SceneImage &scene,
                   const superccd::EditParams &params);

// Renders the whole scene to 16-bit sRGB for export.
QImage renderFull16(const superccd::SceneImage &scene,
                    const superccd::EditParams &params);

// Sharpens an already rendered 16-bit image, for exports resized after rendering.
void sharpen16(QImage &image, double amount);

}

#endif // PREVIEWIMAGEPROCESSING_H
