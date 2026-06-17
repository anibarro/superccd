#include "AmazeDemosaic.h"

#include <libraw/libraw.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace {

class AmazeLibRaw final : public LibRaw
{
public:
    AmazeLibRaw()
    {
        callbacks.interpolate_bayer_cb = &AmazeLibRaw::interpolateBayerCallback;
    }

    void demosaicAmaze()
    {
        amaze_demosaic_RT();
    }

private:
    static void interpolateBayerCallback(void *ctx)
    {
        if (!ctx) {
            return;
        }
        static_cast<AmazeLibRaw *>(ctx)->demosaicAmaze();
    }

    void amaze_demosaic_RT();
};

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

#define CLASS AmazeLibRaw::
#define width imgdata.sizes.iwidth
#define height imgdata.sizes.iheight
#define image imgdata.image
#define pre_mul imgdata.color.pre_mul
#define CLIP(x) LIM(x,0,65535)
#include "../third_party/libraw/amaze_demosaic_RT.cc"
#undef CLIP
#undef pre_mul
#undef image
#undef height
#undef width
#undef CLASS

} // namespace

bool demosaicSparseBayerAmaze(const std::vector<uint16_t> &cfa,
                              int width,
                              int height,
                              const SuperCCDMetadata &metadata,
                              std::vector<uint16_t> &rgb,
                              int &rgbWidth,
                              int &rgbHeight,
                              QString &error)
{
    rgb.clear();
    rgbWidth = 0;
    rgbHeight = 0;

    if (width <= 0 || height <= 0 ||
        cfa.size() < static_cast<size_t>(width) * static_cast<size_t>(height)) {
        error = QStringLiteral("Invalid CFA data for AMaZE demosaic.");
        return false;
    }

    AmazeLibRaw raw;
    const double meanBlack = metadata.hasBlackLevels
        ? (metadata.blackLevels[0] + metadata.blackLevels[1] +
           metadata.blackLevels[2] + metadata.blackLevels[3]) * 0.25
        : 0.0;
    const unsigned blackLevel = static_cast<unsigned>(std::clamp(
        static_cast<int>(std::lround(meanBlack)),
        0,
        65535));

    int result = raw.open_bayer(
        reinterpret_cast<const unsigned char *>(cfa.data()),
        static_cast<unsigned>(cfa.size() * sizeof(uint16_t)),
        static_cast<ushort>(width),
        static_cast<ushort>(height),
        0,
        0,
        0,
        0,
        0,
        LIBRAW_OPENBAYER_GBRG,
        0,
        0,
        blackLevel);
    if (result != LIBRAW_SUCCESS) {
        error = QStringLiteral("LibRaw AMaZE preview setup failed: %1")
                    .arg(QString::fromUtf8(libraw_strerror(result)));
        return false;
    }

    raw.imgdata.params.user_qual = 3;
    raw.imgdata.params.output_bps = 16;
    raw.imgdata.params.output_color = 0;
    raw.imgdata.params.use_camera_wb = 0;
    raw.imgdata.params.use_auto_wb = 0;
    raw.imgdata.params.no_auto_bright = 1;
    raw.imgdata.params.no_auto_scale = 1;
    raw.imgdata.params.bright = 1.0f;
    raw.imgdata.params.gamm[0] = 1.0;
    raw.imgdata.params.gamm[1] = 1.0;
    if (metadata.whiteLevel > 0) {
        raw.imgdata.params.user_sat = metadata.whiteLevel;
    }

    result = raw.unpack();
    if (result != LIBRAW_SUCCESS) {
        error = QStringLiteral("LibRaw AMaZE preview unpack failed: %1")
                    .arg(QString::fromUtf8(libraw_strerror(result)));
        return false;
    }

    result = raw.dcraw_process();
    if (result != LIBRAW_SUCCESS) {
        error = QStringLiteral("LibRaw AMaZE preview demosaic failed: %1")
                    .arg(QString::fromUtf8(libraw_strerror(result)));
        return false;
    }

    int imageError = LIBRAW_SUCCESS;
    libraw_processed_image_t *image = raw.dcraw_make_mem_image(&imageError);
    if (!image) {
        error = QStringLiteral("LibRaw AMaZE preview image creation failed: %1")
                    .arg(QString::fromUtf8(libraw_strerror(imageError)));
        return false;
    }

    rgbWidth = static_cast<int>(image->width);
    rgbHeight = static_cast<int>(image->height);
    if (image->type != LIBRAW_IMAGE_BITMAP || image->colors < 3 ||
        rgbWidth <= 0 || rgbHeight <= 0) {
        raw.dcraw_clear_mem(image);
        error = QStringLiteral("LibRaw AMaZE preview returned an invalid RGB image.");
        return false;
    }

    const size_t pixelCount = static_cast<size_t>(rgbWidth) * static_cast<size_t>(rgbHeight);
    rgb.resize(pixelCount * 3);
    if (image->bits == 16) {
        const uint16_t *src = reinterpret_cast<const uint16_t *>(image->data);
        for (size_t i = 0; i < pixelCount; ++i) {
            rgb[i * 3 + 0] = src[i * image->colors + 0];
            rgb[i * 3 + 1] = src[i * image->colors + 1];
            rgb[i * 3 + 2] = src[i * image->colors + 2];
        }
    } else if (image->bits == 8) {
        const uint8_t *src = image->data;
        for (size_t i = 0; i < pixelCount; ++i) {
            rgb[i * 3 + 0] = static_cast<uint16_t>(src[i * image->colors + 0]) * 257u;
            rgb[i * 3 + 1] = static_cast<uint16_t>(src[i * image->colors + 1]) * 257u;
            rgb[i * 3 + 2] = static_cast<uint16_t>(src[i * image->colors + 2]) * 257u;
        }
    } else {
        raw.dcraw_clear_mem(image);
        error = QStringLiteral("Unsupported LibRaw AMaZE preview bit depth.");
        return false;
    }
    raw.dcraw_clear_mem(image);

    return true;
}
