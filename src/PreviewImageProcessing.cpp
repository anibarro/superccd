#include "PreviewImageProcessing.h"
#include "ParallelProcessing.h"

#include <cstdint>

namespace {

QImage render16(const superccd::SceneImage &scene,
                const superccd::EditParams &params,
                const QRectF &source,
                const QSize &outputSize)
{
    QImage image(outputSize, QImage::Format_RGBX64);
    if (image.isNull()) {
        return image;
    }
    const superccd::SceneRenderer renderer(scene, params);
    renderer.render(source.x(),
                    source.y(),
                    source.width(),
                    source.height(),
                    outputSize.width(),
                    outputSize.height(),
                    reinterpret_cast<uint16_t *>(image.bits()),
                    static_cast<size_t>(image.bytesPerLine()) / sizeof(uint16_t));
    return image;
}

QRectF fullRect(const superccd::SceneImage &scene)
{
    return QRectF(0.0, 0.0, scene.width, scene.height);
}

}

QImage PreviewImageProcessing::renderRegion8(const superccd::SceneImage &scene,
                                             const superccd::EditParams &params,
                                             const QRectF &source,
                                             const QSize &outputSize)
{
    const QImage wide = render16(scene, params, source, outputSize);
    if (wide.isNull()) {
        return wide;
    }
    QImage image(outputSize, QImage::Format_RGB32);
    const int width = outputSize.width();
    superccd::parallel::forRows(outputSize.height(), 64, [&](int y, unsigned) {
        const uint16_t *from = reinterpret_cast<const uint16_t *>(wide.constScanLine(y));
        QRgb *to = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            // Exact rounding from 16 to 8 bits: 65535 maps onto 255.
            to[x] = qRgb((from[x * 4 + 0] + 128) / 257,
                         (from[x * 4 + 1] + 128) / 257,
                         (from[x * 4 + 2] + 128) / 257);
        }
    });
    return image;
}

QImage PreviewImageProcessing::renderFull8(const superccd::SceneImage &scene,
                                           const superccd::EditParams &params)
{
    return renderRegion8(scene, params, fullRect(scene), QSize(scene.width, scene.height));
}

QImage PreviewImageProcessing::renderFull16(const superccd::SceneImage &scene,
                                            const superccd::EditParams &params)
{
    return render16(scene, params, fullRect(scene), QSize(scene.width, scene.height));
}

void PreviewImageProcessing::sharpen16(QImage &image, double amount)
{
    if (image.format() != QImage::Format_RGBX64) {
        image = image.convertToFormat(QImage::Format_RGBX64);
    }
    superccd::sharpenDisplayRgb(reinterpret_cast<uint16_t *>(image.bits()),
                                image.width(),
                                image.height(),
                                static_cast<size_t>(image.bytesPerLine()) / sizeof(uint16_t),
                                amount,
                                nullptr);
}
