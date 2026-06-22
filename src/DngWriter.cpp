#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "DngWriter.h"

#include <QFile>

#include <algorithm>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <cinttypes>
#include <numeric>

#ifdef HAVE_LIBTIFF
#include <tiffio.h>
#endif

#ifdef HAVE_DNG_SDK
#include "dng_sdk.h"
#endif

namespace {
#ifndef TIFFTAG_DNGVERSION
#define TIFFTAG_DNGVERSION 50706
#endif
#ifndef TIFFTAG_DNGBACKWARDVERSION
#define TIFFTAG_DNGBACKWARDVERSION 50707
#endif
#ifndef TIFFTAG_UNIQUECAMERAMODEL
#define TIFFTAG_UNIQUECAMERAMODEL 50708
#endif
#ifndef TIFFTAG_CFAPLANECOLOR
#define TIFFTAG_CFAPLANECOLOR 50710
#endif
#ifndef TIFFTAG_BLACKLEVELREPEATDIM
#define TIFFTAG_BLACKLEVELREPEATDIM 50713
#endif
#ifndef TIFFTAG_BLACKLEVEL
#define TIFFTAG_BLACKLEVEL 50714
#endif
#ifndef TIFFTAG_WHITELEVEL
#define TIFFTAG_WHITELEVEL 50717
#endif
#ifndef TIFFTAG_CFAREPEATPATTERNDIM
#define TIFFTAG_CFAREPEATPATTERNDIM 33421
#endif
#ifndef TIFFTAG_CFAPATTERN
#define TIFFTAG_CFAPATTERN 33422
#endif
#ifndef TIFFTAG_COLORMATRIX1
#define TIFFTAG_COLORMATRIX1 50721
#endif
#ifndef TIFFTAG_DEFAULTSCALE
#define TIFFTAG_DEFAULTSCALE 50718
#endif
#ifndef TIFFTAG_DEFAULTCROPORIGIN
#define TIFFTAG_DEFAULTCROPORIGIN 50719
#endif
#ifndef TIFFTAG_ASSHOTNEUTRAL
#define TIFFTAG_ASSHOTNEUTRAL 50727
#endif
#ifndef TIFFTAG_BASELINEEXPOSURE
#define TIFFTAG_BASELINEEXPOSURE 50730
#endif
#ifndef TIFFTAG_FNUMBER
#define TIFFTAG_FNUMBER 33437
#endif
#ifndef TIFFTAG_EXPOSURETIME
#define TIFFTAG_EXPOSURETIME 33434
#endif
#ifndef TIFFTAG_ISOSPEEDRATINGS
#define TIFFTAG_ISOSPEEDRATINGS 34855
#endif
#ifndef TIFFTAG_FOCALLENGTH
#define TIFFTAG_FOCALLENGTH 37386
#endif
#ifndef TIFFTAG_DATETIMEORIGINAL
#define TIFFTAG_DATETIMEORIGINAL 36867
#endif
#ifndef TIFFTAG_DATETIMEDIGITIZED
#define TIFFTAG_DATETIMEDIGITIZED 36868
#endif
#ifndef TIFFTAG_LENSMODEL
#define TIFFTAG_LENSMODEL 42036
#endif
#ifndef TIFFTAG_EXIFIFD
#define TIFFTAG_EXIFIFD 34665
#endif
#ifndef TIFFTAG_EXIFVERSION
#define TIFFTAG_EXIFVERSION 36864
#endif
#ifndef TIFFTAG_FLASHPIXVERSION
#define TIFFTAG_FLASHPIXVERSION 40960
#endif
#ifndef TIFFTAG_COLORSPACE
#define TIFFTAG_COLORSPACE 40961
#endif
#ifndef EXIFCOLORSPACE_UNCALIBRATED
#define EXIFCOLORSPACE_UNCALIBRATED 65535
#endif
#ifndef TIFFTAG_PHOTOMETRIC
#define TIFFTAG_PHOTOMETRIC 262
#endif
#ifndef PHOTOMETRIC_RGB
#define PHOTOMETRIC_RGB 2
#endif
#ifndef PHOTOMETRIC_CFA
#define PHOTOMETRIC_CFA 32803
#endif
#ifndef PHOTOMETRIC_LINEARRAW
#define PHOTOMETRIC_LINEARRAW 34892
#endif
#ifndef TIFFTAG_SAMPLEFORMAT
#define TIFFTAG_SAMPLEFORMAT 339
#endif
#ifndef SAMPLEFORMAT_UINT
#define SAMPLEFORMAT_UINT 1
#endif
#ifndef TIFFTAG_ORIENTATION
#define TIFFTAG_ORIENTATION 274
#endif
#ifndef TIFFTAG_SUBFILETYPE
#define TIFFTAG_SUBFILETYPE 254
#endif
#ifndef ORIENTATION_TOPLEFT
#define ORIENTATION_TOPLEFT 1
#endif
#ifndef ORIENTATION_RIGHTTOP
#define ORIENTATION_RIGHTTOP 6
#endif
#ifndef FILETYPE_REDUCEDIMAGE
#define FILETYPE_REDUCEDIMAGE 0x1
#endif

#ifdef HAVE_LIBTIFF
static void logProcessing(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    FILE *f = fopen("processing.log", "a");
    if (f) {
        vfprintf(f, format, args);
        fprintf(f, "\n");
        fclose(f);
    }
    va_end(args);
}

static void libtiffErrorHandler(const char *module, const char *fmt, va_list ap)
{
    FILE *f = fopen("processing.log", "a");
    if (f) {
        fprintf(f, "LibTIFF ERROR (%s): ", module ? module : "unknown");
        vfprintf(f, fmt, ap);
        fprintf(f, "\n");
        fclose(f);
    }
}

static const TIFFFieldInfo dngFieldInfo[] = {
    { TIFFTAG_DNGVERSION, TIFF_VARIABLE2, TIFF_VARIABLE2, TIFF_BYTE, FIELD_CUSTOM, 1, 1, const_cast<char *>("DNGVersion") },
    { TIFFTAG_DNGBACKWARDVERSION, TIFF_VARIABLE2, TIFF_VARIABLE2, TIFF_BYTE, FIELD_CUSTOM, 1, 1, const_cast<char *>("DNGBackwardVersion") },
    { TIFFTAG_CFAPLANECOLOR, TIFF_VARIABLE2, TIFF_VARIABLE2, TIFF_BYTE, FIELD_CUSTOM, 1, 1, const_cast<char *>("CFAPlaneColor") },
    { TIFFTAG_BLACKLEVELREPEATDIM, TIFF_VARIABLE2, TIFF_VARIABLE2, TIFF_SHORT, FIELD_CUSTOM, 1, 1, const_cast<char *>("BlackLevelRepeatDim") },
    { TIFFTAG_BLACKLEVEL, TIFF_VARIABLE2, TIFF_VARIABLE2, TIFF_RATIONAL, FIELD_CUSTOM, 1, 1, const_cast<char *>("BlackLevel") },
    { TIFFTAG_WHITELEVEL, TIFF_VARIABLE2, TIFF_VARIABLE2, TIFF_LONG, FIELD_CUSTOM, 1, 1, const_cast<char *>("WhiteLevel") },
    { TIFFTAG_CFAREPEATPATTERNDIM, TIFF_VARIABLE2, TIFF_VARIABLE2, TIFF_SHORT, FIELD_CUSTOM, 1, 1, const_cast<char *>("CFARepeatPatternDim") },
    { TIFFTAG_CFAPATTERN, TIFF_VARIABLE2, TIFF_VARIABLE2, TIFF_BYTE, FIELD_CUSTOM, 1, 1, const_cast<char *>("CFAPattern") },
    { TIFFTAG_DEFAULTSCALE, TIFF_VARIABLE2, TIFF_VARIABLE2, TIFF_RATIONAL, FIELD_CUSTOM, 1, 1, const_cast<char *>("DefaultScale") },
    { TIFFTAG_COLORMATRIX1, TIFF_VARIABLE2, TIFF_VARIABLE2, TIFF_SRATIONAL, FIELD_CUSTOM, 1, 1, const_cast<char *>("ColorMatrix1") },
    { TIFFTAG_ASSHOTNEUTRAL, TIFF_VARIABLE2, TIFF_VARIABLE2, TIFF_RATIONAL, FIELD_CUSTOM, 1, 1, const_cast<char *>("AsShotNeutral") },
    { TIFFTAG_FNUMBER, 1, 1, TIFF_RATIONAL, FIELD_CUSTOM, 1, 0, const_cast<char *>("FNumber") },
    { TIFFTAG_EXPOSURETIME, 1, 1, TIFF_RATIONAL, FIELD_CUSTOM, 1, 0, const_cast<char *>("ExposureTime") },
    { TIFFTAG_ISOSPEEDRATINGS, TIFF_VARIABLE2, TIFF_VARIABLE2, TIFF_SHORT, FIELD_CUSTOM, 1, 1, const_cast<char *>("ISOSpeedRatings") },
    { TIFFTAG_FOCALLENGTH, 1, 1, TIFF_RATIONAL, FIELD_CUSTOM, 1, 0, const_cast<char *>("FocalLength") },
    { TIFFTAG_DATETIMEORIGINAL, TIFF_VARIABLE2, TIFF_VARIABLE2, TIFF_ASCII, FIELD_CUSTOM, 1, 1, const_cast<char *>("DateTimeOriginal") },
    { TIFFTAG_DATETIMEDIGITIZED, TIFF_VARIABLE2, TIFF_VARIABLE2, TIFF_ASCII, FIELD_CUSTOM, 1, 1, const_cast<char *>("DateTimeDigitized") },
    { TIFFTAG_LENSMODEL, TIFF_VARIABLE2, TIFF_VARIABLE2, TIFF_ASCII, FIELD_CUSTOM, 1, 1, const_cast<char *>("LensModel") },
    { TIFFTAG_EXIFVERSION, 4, 4, TIFF_UNDEFINED, FIELD_CUSTOM, 1, 0, const_cast<char *>("ExifVersion") },
    { TIFFTAG_FLASHPIXVERSION, 4, 4, TIFF_UNDEFINED, FIELD_CUSTOM, 1, 0, const_cast<char *>("FlashPixVersion") },
    { TIFFTAG_COLORSPACE, 1, 1, TIFF_SHORT, FIELD_CUSTOM, 1, 0, const_cast<char *>("ColorSpace") },
};

static const TIFFField *findTiffField(TIFF *tif, uint32_t tag, TIFFDataType type)
{
    return TIFFFindField(tif, tag, type);
}

static bool tiffFieldWantsCount(TIFF *tif, uint32_t tag, TIFFDataType type)
{
    const TIFFField *field = findTiffField(tif, tag, type);
    if (!field) {
        return true;
    }

    const int writeCount = TIFFFieldWriteCount(field);
    return TIFFFieldPassCount(field) ||
           writeCount == TIFF_VARIABLE ||
           writeCount == TIFF_VARIABLE2;
}

static void setByteArrayField(TIFF *tif, uint32_t tag, uint32_t count, const uint8_t *values)
{
    if (tiffFieldWantsCount(tif, tag, TIFF_BYTE)) {
        TIFFSetField(tif, tag, count, values);
    } else {
        TIFFSetField(tif, tag, values);
    }
}

static void setShortArrayField(TIFF *tif, uint32_t tag, uint32_t count, const uint16_t *values)
{
    if (tiffFieldWantsCount(tif, tag, TIFF_SHORT)) {
        TIFFSetField(tif, tag, count, values);
    } else {
        TIFFSetField(tif, tag, values);
    }
}

static void setLongArrayField(TIFF *tif, uint32_t tag, uint32_t count, const uint32_t *values)
{
    if (tiffFieldWantsCount(tif, tag, TIFF_LONG)) {
        TIFFSetField(tif, tag, count, values);
    } else {
        TIFFSetField(tif, tag, values);
    }
}

static void setRationalArrayField(TIFF *tif, uint32_t tag, uint32_t count, const float *values)
{
    if (tiffFieldWantsCount(tif, tag, TIFF_RATIONAL)) {
        TIFFSetField(tif, tag, count, values);
    } else {
        TIFFSetField(tif, tag, values);
    }
}

static void setSRationalArrayField(TIFF *tif, uint32_t tag, uint32_t count, const float *values)
{
    if (tiffFieldWantsCount(tif, tag, TIFF_SRATIONAL)) {
        TIFFSetField(tif, tag, count, values);
    } else {
        TIFFSetField(tif, tag, values);
    }
}

static void setAsciiField(TIFF *tif, uint32_t tag, const QByteArray &value)
{
    if (value.isEmpty()) {
        return;
    }
    if (tiffFieldWantsCount(tif, tag, TIFF_ASCII)) {
        TIFFSetField(tif, tag, static_cast<uint32_t>(value.size() + 1), value.constData());
    } else {
        TIFFSetField(tif, tag, value.constData());
    }
}

static void setSingleSRationalField(TIFF *tif, uint32_t tag, float value)
{
    // SRATIONAL is signed rational: (numerator, denominator)
    if (value > 0.0f) {
        TIFFSetField(tif, tag, static_cast<int32_t>(value * 10000), 10000);
    } else if (value < 0.0f) {
        TIFFSetField(tif, tag, static_cast<int32_t>(value * 10000), 10000);
    } else {
        TIFFSetField(tif, tag, 0, 1);
    }
}

static void setSingleRationalField(TIFF *tif, uint32_t tag, float value)
{
    // RATIONAL is stored as an unsigned numerator/denominator pair.
    if (value > 0.0f) {
        const uint32_t scale = 10000;
        uint32_t numerator = static_cast<uint32_t>(value * static_cast<float>(scale) + 0.5f);
        uint32_t denominator = scale;
        const uint32_t divisor = std::gcd(numerator, denominator);
        if (divisor > 1) {
            numerator /= divisor;
            denominator /= divisor;
        }
        if (numerator == 0) {
            numerator = 1;
        }
        TIFFSetField(tif, tag, numerator, denominator);
    } else {
        TIFFSetField(tif, tag, 0, 1);
    }
}

static void writeCommonCaptureMetadata(TIFF *tif,
                                       const SuperCCDMetadata &metadata,
                                       const QByteArray &makeBytes,
                                       const QByteArray &modelBytes,
                                       const QByteArray &softwareBytes,
                                       const QByteArray &dateTimeBytes,
                                       const QByteArray &lensModelBytes)
{
    TIFFSetField(tif, TIFFTAG_MAKE, makeBytes.constData());
    TIFFSetField(tif, TIFFTAG_MODEL, modelBytes.constData());
    TIFFSetField(tif, TIFFTAG_SOFTWARE, softwareBytes.constData());
    Q_UNUSED(metadata);
    Q_UNUSED(lensModelBytes);

    if (!metadata.dateTime.isEmpty()) {
        TIFFSetField(tif, TIFFTAG_DATETIME, dateTimeBytes.constData());
    }
}

static bool writeReducedPreviewDirectory(TIFF *tif, const QImage &thumbnail)
{
    if (thumbnail.isNull()) {
        return true;
    }

    QImage rgbThumb = thumbnail.convertToFormat(QImage::Format_RGB888);
    if (rgbThumb.isNull()) {
        return false;
    }

    TIFFSetField(tif, TIFFTAG_SUBFILETYPE, static_cast<uint32_t>(FILETYPE_REDUCEDIMAGE));
    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, static_cast<uint32_t>(rgbThumb.width()));
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, static_cast<uint32_t>(rgbThumb.height()));
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, static_cast<uint16_t>(8));
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, static_cast<uint16_t>(3));
    TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, static_cast<uint16_t>(PHOTOMETRIC_RGB));
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, static_cast<uint16_t>(PLANARCONFIG_CONTIG));
    TIFFSetField(tif, TIFFTAG_COMPRESSION, static_cast<uint16_t>(COMPRESSION_NONE));
    TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(tif, 0));
    TIFFSetField(tif, TIFFTAG_ORIENTATION, static_cast<uint16_t>(ORIENTATION_TOPLEFT));

    for (int row = 0; row < rgbThumb.height(); ++row) {
        if (TIFFWriteScanline(tif,
                              const_cast<uchar *>(rgbThumb.constScanLine(row)),
                              row,
                              0) < 0) {
            return false;
        }
    }

    return TIFFWriteDirectory(tif) == 1;
}

struct ExifEntry
{
    uint16_t tag = 0;
    uint16_t type = 0;
    uint32_t count = 0;
    QByteArray value;
};

static uint16_t readUint16(const QByteArray &data, int offset, bool littleEndian)
{
    const unsigned char *p =
        reinterpret_cast<const unsigned char *>(data.constData() + offset);
    if (littleEndian) {
        return static_cast<uint16_t>(p[0] | (p[1] << 8));
    }
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

static uint32_t readUint32(const QByteArray &data, int offset, bool littleEndian)
{
    const unsigned char *p =
        reinterpret_cast<const unsigned char *>(data.constData() + offset);
    if (littleEndian) {
        return static_cast<uint32_t>(p[0]) |
               (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) |
               (static_cast<uint32_t>(p[3]) << 24);
    }
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

static void writeUint16(QByteArray &data, int offset, uint16_t value, bool littleEndian)
{
    if (littleEndian) {
        data[offset] = static_cast<char>(value & 0xFF);
        data[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
    } else {
        data[offset] = static_cast<char>((value >> 8) & 0xFF);
        data[offset + 1] = static_cast<char>(value & 0xFF);
    }
}

static void writeUint32(QByteArray &data, int offset, uint32_t value, bool littleEndian)
{
    if (littleEndian) {
        data[offset] = static_cast<char>(value & 0xFF);
        data[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
        data[offset + 2] = static_cast<char>((value >> 16) & 0xFF);
        data[offset + 3] = static_cast<char>((value >> 24) & 0xFF);
    } else {
        data[offset] = static_cast<char>((value >> 24) & 0xFF);
        data[offset + 1] = static_cast<char>((value >> 16) & 0xFF);
        data[offset + 2] = static_cast<char>((value >> 8) & 0xFF);
        data[offset + 3] = static_cast<char>(value & 0xFF);
    }
}

static void appendUint16(QByteArray &data, uint16_t value, bool littleEndian)
{
    const int offset = data.size();
    data.resize(offset + 2);
    writeUint16(data, offset, value, littleEndian);
}

static void appendUint32(QByteArray &data, uint32_t value, bool littleEndian)
{
    const int offset = data.size();
    data.resize(offset + 4);
    writeUint32(data, offset, value, littleEndian);
}

static QByteArray makeAsciiValue(const QString &text)
{
    QByteArray bytes = text.trimmed().toUtf8();
    if (bytes.isEmpty()) {
        return QByteArray();
    }
    bytes.append('\0');
    return bytes;
}

static QByteArray makeShortValue(uint16_t value, bool littleEndian)
{
    QByteArray bytes(2, '\0');
    writeUint16(bytes, 0, value, littleEndian);
    return bytes;
}

static QByteArray makeRationalValue(double value, bool littleEndian)
{
    if (!(value > 0.0)) {
        return QByteArray();
    }

    uint32_t numerator = static_cast<uint32_t>(value * 10000.0 + 0.5);
    uint32_t denominator = 10000;
    const uint32_t divisor = std::gcd(numerator, denominator);
    if (divisor > 1) {
        numerator /= divisor;
        denominator /= divisor;
    }
    if (numerator == 0) {
        numerator = 1;
    }

    QByteArray bytes(8, '\0');
    writeUint32(bytes, 0, numerator, littleEndian);
    writeUint32(bytes, 4, denominator, littleEndian);
    return bytes;
}

static bool patchExifDirectoryInPlace(const QString &outputPath,
                                      const SuperCCDMetadata &metadata,
                                      QString *error)
{
    QFile file(outputPath);
    if (!file.open(QIODevice::ReadWrite)) {
        if (error) {
            *error = QStringLiteral("Could not reopen DNG to patch EXIF metadata.");
        }
        return false;
    }

    QByteArray data = file.readAll();
    if (data.size() < 16) {
        if (error) {
            *error = QStringLiteral("DNG file too small for EXIF patching.");
        }
        return false;
    }

    const bool littleEndian =
        (static_cast<unsigned char>(data[0]) == 'I' &&
         static_cast<unsigned char>(data[1]) == 'I');
    const bool bigEndian =
        (static_cast<unsigned char>(data[0]) == 'M' &&
         static_cast<unsigned char>(data[1]) == 'M');
    if (!littleEndian && !bigEndian) {
        if (error) {
            *error = QStringLiteral("Unrecognized TIFF byte order in DNG.");
        }
        return false;
    }

    const uint16_t tiffVersion = readUint16(data, 2, littleEndian);
    if (tiffVersion != 42) {
        if (error) {
            *error = QStringLiteral("BigTIFF/custom TIFF variants are not supported by the EXIF patcher.");
        }
        return false;
    }

    const uint32_t firstIfdOffset = readUint32(data, 4, littleEndian);
    if (firstIfdOffset == 0 || firstIfdOffset + 2 > static_cast<uint32_t>(data.size())) {
        if (error) {
            *error = QStringLiteral("Invalid first IFD offset in DNG.");
        }
        return false;
    }

    const uint16_t entryCount = readUint16(data, static_cast<int>(firstIfdOffset), littleEndian);
    const int entriesStart = static_cast<int>(firstIfdOffset) + 2;
    const int entriesEnd = entriesStart + static_cast<int>(entryCount) * 12;
    if (entriesEnd + 4 > data.size()) {
        if (error) {
            *error = QStringLiteral("Primary IFD is truncated in DNG.");
        }
        return false;
    }

    int exifValueFieldOffset = -1;
    for (uint16_t i = 0; i < entryCount; ++i) {
        const int entryOffset = entriesStart + static_cast<int>(i) * 12;
        const uint16_t tag = readUint16(data, entryOffset, littleEndian);
        if (tag == TIFFTAG_EXIFIFD) {
            exifValueFieldOffset = entryOffset + 8;
            break;
        }
    }

    if (exifValueFieldOffset < 0) {
        if (error) {
            *error = QStringLiteral("Primary DNG directory does not contain an EXIFIFD placeholder.");
        }
        return false;
    }

    std::vector<ExifEntry> entries;
    entries.push_back({TIFFTAG_EXIFVERSION, TIFF_UNDEFINED, 4, QByteArray("0231", 4)});
    entries.push_back({TIFFTAG_FLASHPIXVERSION, TIFF_UNDEFINED, 4, QByteArray("0100", 4)});
    entries.push_back({TIFFTAG_COLORSPACE, TIFF_SHORT, 1,
                       makeShortValue(EXIFCOLORSPACE_UNCALIBRATED, littleEndian)});

    const QByteArray dateTimeValue = makeAsciiValue(metadata.dateTime);
    if (!dateTimeValue.isEmpty()) {
        entries.push_back({TIFFTAG_DATETIMEORIGINAL, TIFF_ASCII,
                           static_cast<uint32_t>(dateTimeValue.size()), dateTimeValue});
        entries.push_back({TIFFTAG_DATETIMEDIGITIZED, TIFF_ASCII,
                           static_cast<uint32_t>(dateTimeValue.size()), dateTimeValue});
    }

    const QByteArray exposureTimeValue = makeRationalValue(metadata.shutter, littleEndian);
    if (!exposureTimeValue.isEmpty()) {
        entries.push_back({TIFFTAG_EXPOSURETIME, TIFF_RATIONAL, 1, exposureTimeValue});
    }

    const QByteArray fNumberValue = makeRationalValue(metadata.aperture, littleEndian);
    if (!fNumberValue.isEmpty()) {
        entries.push_back({TIFFTAG_FNUMBER, TIFF_RATIONAL, 1, fNumberValue});
    }

    if (metadata.iso > 0 && metadata.iso <= 65535) {
        entries.push_back({TIFFTAG_ISOSPEEDRATINGS, TIFF_SHORT, 1,
                           makeShortValue(static_cast<uint16_t>(metadata.iso), littleEndian)});
    }

    const QByteArray focalLengthValue = makeRationalValue(metadata.focalLength, littleEndian);
    if (!focalLengthValue.isEmpty()) {
        entries.push_back({TIFFTAG_FOCALLENGTH, TIFF_RATIONAL, 1, focalLengthValue});
    }

    const QByteArray lensModelValue = makeAsciiValue(metadata.lensModel);
    if (!lensModelValue.isEmpty()) {
        entries.push_back({TIFFTAG_LENSMODEL, TIFF_ASCII,
                           static_cast<uint32_t>(lensModelValue.size()), lensModelValue});
    }

    std::sort(entries.begin(),
              entries.end(),
              [](const ExifEntry &a, const ExifEntry &b) { return a.tag < b.tag; });

    QByteArray exifIfd;
    appendUint16(exifIfd, static_cast<uint16_t>(entries.size()), littleEndian);

    QByteArray extraData;
    const uint32_t exifIfdOffset = static_cast<uint32_t>(data.size());
    const uint32_t baseDataOffset =
        exifIfdOffset + 2 + static_cast<uint32_t>(entries.size()) * 12 + 4;
    uint32_t nextDataOffset = baseDataOffset;

    for (const ExifEntry &entry : entries) {
        appendUint16(exifIfd, entry.tag, littleEndian);
        appendUint16(exifIfd, entry.type, littleEndian);
        appendUint32(exifIfd, entry.count, littleEndian);

        if (entry.value.size() <= 4) {
            QByteArray inlineValue = entry.value;
            inlineValue.resize(4);
            exifIfd.append(inlineValue);
        } else {
            appendUint32(exifIfd, nextDataOffset, littleEndian);
            extraData.append(entry.value);
            if (extraData.size() & 1) {
                extraData.append('\0');
            }
            nextDataOffset = baseDataOffset + static_cast<uint32_t>(extraData.size());
        }
    }

    appendUint32(exifIfd, 0, littleEndian);
    exifIfd.append(extraData);

    data.append(exifIfd);
    writeUint32(data, exifValueFieldOffset, exifIfdOffset, littleEndian);

    if (!file.resize(0)) {
        if (error) {
            *error = QStringLiteral("Could not resize DNG while patching EXIF metadata.");
        }
        return false;
    }
    file.seek(0);
    if (file.write(data) != data.size()) {
        if (error) {
            *error = QStringLiteral("Could not write patched EXIF metadata to DNG.");
        }
        return false;
    }
    file.close();
    logProcessing("EXIF directory patched at offset %" PRIu32, exifIfdOffset);
    return true;
}


bool writeDngWithLibTiff(const QString &outputPath,
                         const std::vector<uint16_t> &buffer,
                         int width,
                         int height,
                         int bitDepth,
                         const SuperCCDMetadata &metadata,
                         QString &error)
{
    logProcessing("writeDngWithLibTiff start: %s (%dx%d)", outputPath.toUtf8().constData(), width, height);
    TIFFSetErrorHandler(libtiffErrorHandler);
    TIFF *tif = TIFFOpen(outputPath.toUtf8().constData(), "w");
    if (!tif) {
        logProcessing("TIFFOpen returned NULL for %s", outputPath.toUtf8().constData());
        error = QStringLiteral("Unable to open output DNG file.");
        return false;
    }

    // 1. DECLARATIONS: Declare variables at the top to avoid jumping over initializations (MSVC C2362)
    tsize_t scanlineSize = 0;
    std::vector<uint8_t> rowBuffer;
    // CRITICAL: Keep QByteArray alive while LibTIFF is using the pointers
    QByteArray makeBytes = metadata.make.toUtf8();
    QByteArray modelBytes = metadata.model.toUtf8();
    QByteArray softwareBytes = metadata.software.toUtf8();
    QByteArray dateTimeBytes = metadata.dateTime.toUtf8();
    QByteArray lensModelBytes = metadata.lensModel.toUtf8();
    const uint8_t dngVersion[4] = {1, 4, 0, 0};
    const uint8_t dngBackwardVersion[4] = {1, 1, 0, 0};
    const uint8_t cfaPlaneColor[3] = {0, 1, 2};
    const uint16_t blackLevelRepeatDim[2] = {2, 2};
    const uint16_t cfaRepeatPatternDim[2] = {2, 2};
    const uint8_t cfaPattern[4] = {1, 2, 0, 1};
    const float defaultScale[2] = {1.0f, 1.0f};

    // 2. BASIC IMAGE TAGS: Must be set BEFORE calling TIFFScanlineSize
    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, (uint32_t)width);
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, (uint32_t)height);
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, (uint16_t)bitDepth);
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, (uint16_t)1);
    TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, (uint16_t)PHOTOMETRIC_CFA);
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, (uint16_t)PLANARCONFIG_CONTIG);
    TIFFSetField(tif, TIFFTAG_COMPRESSION, (uint16_t)COMPRESSION_NONE);
    TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, (uint16_t)SAMPLEFORMAT_UINT);
    TIFFSetField(tif, TIFFTAG_ORIENTATION, (uint16_t)ORIENTATION_TOPLEFT);
    TIFFSetField(tif, TIFFTAG_EXIFIFD, static_cast<uint32_t>(8));

    // 3. SCANLINE SIZE: Now that tags are set, LibTIFF can calculate this correctly
    scanlineSize = TIFFScanlineSize(tif);
    if (scanlineSize <= 0) {
        logProcessing("TIFFScanlineSize failed (zero or negative)");
        goto fail;
    }
    rowBuffer.resize(static_cast<size_t>(scanlineSize));
    TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(tif, 0));

    // 4. METADATA & DNG TAGS
    if (!TIFFMergeFieldInfo(tif, dngFieldInfo, static_cast<uint32_t>(sizeof(dngFieldInfo) / sizeof(dngFieldInfo[0])))) {
        logProcessing("TIFFMergeFieldInfo returned 0 (tags already registered)");
    }

    writeCommonCaptureMetadata(tif,
                               metadata,
                               makeBytes,
                               modelBytes,
                               softwareBytes,
                               dateTimeBytes,
                               lensModelBytes);

      setByteArrayField(tif, TIFFTAG_DNGVERSION, 4, dngVersion);
      setByteArrayField(tif, TIFFTAG_DNGBACKWARDVERSION, 4, dngBackwardVersion);
      // Keep camera identity in the standard TIFF tags above. The DNG-specific
      // UniqueCameraModel tag crashes with the vcpkg LibTIFF custom-tag path.
      setByteArrayField(tif, TIFFTAG_CFAPLANECOLOR, 3, cfaPlaneColor);
      if (metadata.hasBlackLevels) {
          const float blackLevels[4] = {
              static_cast<float>(metadata.blackLevels[0]),
              static_cast<float>(metadata.blackLevels[1]),
              static_cast<float>(metadata.blackLevels[2]),
              static_cast<float>(metadata.blackLevels[3])
          };
          setShortArrayField(tif, TIFFTAG_BLACKLEVELREPEATDIM, 2, blackLevelRepeatDim);
          setRationalArrayField(tif, TIFFTAG_BLACKLEVEL, 4, blackLevels);
      }
      if (metadata.whiteLevel > 0) {
          const uint32_t whiteLevel = metadata.whiteLevel;
          setLongArrayField(tif, TIFFTAG_WHITELEVEL, 1, &whiteLevel);
      }
      setShortArrayField(tif, TIFFTAG_CFAREPEATPATTERNDIM, 2, cfaRepeatPatternDim);
      setByteArrayField(tif, TIFFTAG_CFAPATTERN, 4, cfaPattern);
      setRationalArrayField(tif, TIFFTAG_DEFAULTSCALE, 2, defaultScale);
      if (metadata.hasColorMatrix1) {
          const float colorMatrix1[9] = {
              static_cast<float>(metadata.colorMatrix1[0]),
              static_cast<float>(metadata.colorMatrix1[1]),
              static_cast<float>(metadata.colorMatrix1[2]),
              static_cast<float>(metadata.colorMatrix1[3]),
              static_cast<float>(metadata.colorMatrix1[4]),
              static_cast<float>(metadata.colorMatrix1[5]),
              static_cast<float>(metadata.colorMatrix1[6]),
              static_cast<float>(metadata.colorMatrix1[7]),
              static_cast<float>(metadata.colorMatrix1[8])
          };
          setSRationalArrayField(tif, TIFFTAG_COLORMATRIX1, 9, colorMatrix1);
      }
      if (metadata.hasAsShotNeutral) {
          const float asShotNeutral[3] = {
              static_cast<float>(metadata.asShotNeutral[0]),
              static_cast<float>(metadata.asShotNeutral[1]),
              static_cast<float>(metadata.asShotNeutral[2])
          };
          setRationalArrayField(tif, TIFFTAG_ASSHOTNEUTRAL, 3, asShotNeutral);
      }
      if (metadata.hasBaselineExposure) {
          const float baselineExposure = static_cast<float>(metadata.baselineExposure);
          logProcessing("writing BaselineExposure tag: %.6f", baselineExposure);
          setSingleSRationalField(tif, TIFFTAG_BASELINEEXPOSURE, baselineExposure);
      }
      if (metadata.hasFlip) {
          // LibRaw flip: 0=none, 3=180, 5=90CCW, 6=90CW, negative=mirrored
          // TIFF orientation: 1=TLL, 2=TR, 3=BRL, 4=BL, 5=LR, 6=RT, 7=RB, 8=LB
          uint16_t orientation = 1; // default: normal
          const int flip = metadata.flip < 0 ? -metadata.flip : metadata.flip;
          if (flip == 3 || flip == 2) {
              orientation = 3; // BOTRIGHT - 180°
          } else if (flip == 5 || flip == 1) {
              orientation = 8; // LEFTBOT - 90° CCW
          } else if (flip == 6) {
              orientation = 6; // RIGHTTOP - 90° CW
          } else if (metadata.flip < 0) {
              // Mirrored - use negative flip to determine base orientation
              if (flip == 3 || flip == 2) {
                  orientation = 2; // TOPRIGHT - mirrored horizontal
              } else if (flip == 5 || flip == 1) {
                  orientation = 5; // LEFTTOP - mirrored 90° CCW
              } else if (flip == 6) {
                  orientation = 7; // RIGHTBOT - mirrored 90° CW
              }
          }
          logProcessing("writing Orientation tag: %d (from RAF flip=%d)", orientation, flip);
          TIFFSetField(tif, TIFFTAG_ORIENTATION, orientation);
      }
    logProcessing("TIFFSetField calls completed");

    for (int row = 0; row < height; ++row) {
        // Ensure we don't read past the provided buffer
        size_t bytesPerRow = static_cast<size_t>(width) * sizeof(uint16_t);
        std::memset(rowBuffer.data(), 0, rowBuffer.size());
        size_t toCopy = (static_cast<size_t>(scanlineSize) < bytesPerRow) ? static_cast<size_t>(scanlineSize) : bytesPerRow;
        std::memcpy(rowBuffer.data(), &buffer[static_cast<size_t>(row) * width], toCopy);
        if (TIFFWriteScanline(tif, rowBuffer.data(), row, 0) < 0) {
            TIFFClose(tif);
            error = QStringLiteral("Failed to write DNG scanline.");
            return false;
        }
        if ((row & 0x3F) == 0) { // every 64 rows
            logProcessing("writing row %d/%d", row, height);
        }
    }

    if (!TIFFWriteDirectory(tif)) {
        TIFFClose(tif);
        error = QStringLiteral("Failed to finalize primary DNG directory.");
        return false;
    }

    if (!writeReducedPreviewDirectory(tif, metadata.embeddedThumbnail)) {
        TIFFClose(tif);
        error = QStringLiteral("Failed to write embedded preview image.");
        return false;
    }

    TIFFClose(tif);
    if (!patchExifDirectoryInPlace(outputPath, metadata, &error)) {
        return false;
    }
    logProcessing("DNG write completed: %s (%dx%d)", outputPath.toUtf8().constData(), width, height);
    return true;

fail:
    TIFFClose(tif);
    error = QStringLiteral("DNG metadata serialization failed.");
    return false;
}

bool writeLinearDngWithLibTiff(const QString &outputPath,
                               const std::vector<uint16_t> &buffer,
                               int width,
                               int height,
                               int bitDepth,
                               const SuperCCDMetadata &metadata,
                               QString &error)
{
    logProcessing("writeLinearDngWithLibTiff start: %s (%dx%d)", outputPath.toUtf8().constData(), width, height);
    TIFFSetErrorHandler(libtiffErrorHandler);
    TIFF *tif = TIFFOpen(outputPath.toUtf8().constData(), "w");
    if (!tif) {
        error = QStringLiteral("Unable to open output DNG file.");
        return false;
    }

    QByteArray makeBytes = metadata.make.toUtf8();
    QByteArray modelBytes = metadata.model.toUtf8();
    QByteArray softwareBytes = metadata.software.toUtf8();
    QByteArray dateTimeBytes = metadata.dateTime.toUtf8();
    QByteArray lensModelBytes = metadata.lensModel.toUtf8();
    const uint8_t dngVersion[4] = {1, 4, 0, 0};
    const uint8_t dngBackwardVersion[4] = {1, 1, 0, 0};
    const double colorMatrix1[9] = {
        1.0, 0.0, 0.0,
        0.0, 1.0, 0.0,
        0.0, 0.0, 1.0
    };

    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, static_cast<uint32_t>(width));
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, static_cast<uint32_t>(height));
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, static_cast<uint16_t>(bitDepth));
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, static_cast<uint16_t>(3));
    TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, static_cast<uint16_t>(PHOTOMETRIC_RGB));
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, static_cast<uint16_t>(PLANARCONFIG_CONTIG));
    TIFFSetField(tif, TIFFTAG_COMPRESSION, static_cast<uint16_t>(COMPRESSION_NONE));
    TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, static_cast<uint16_t>(SAMPLEFORMAT_UINT));
    TIFFSetField(tif, TIFFTAG_ORIENTATION, static_cast<uint16_t>(ORIENTATION_TOPLEFT));
    TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(tif, 0));
    TIFFSetField(tif, TIFFTAG_EXIFIFD, static_cast<uint32_t>(8));

    if (!TIFFMergeFieldInfo(tif, dngFieldInfo, static_cast<uint32_t>(sizeof(dngFieldInfo) / sizeof(dngFieldInfo[0])))) {
        logProcessing("TIFFMergeFieldInfo returned 0 (tags already registered)");
    }

    writeCommonCaptureMetadata(tif,
                               metadata,
                               makeBytes,
                               modelBytes,
                               softwareBytes,
                               dateTimeBytes,
                               lensModelBytes);

    setByteArrayField(tif, TIFFTAG_DNGVERSION, 4, dngVersion);
    setByteArrayField(tif, TIFFTAG_DNGBACKWARDVERSION, 4, dngBackwardVersion);
    Q_UNUSED(colorMatrix1);

    const size_t samplesPerRow = static_cast<size_t>(width) * 3;
    for (int row = 0; row < height; ++row) {
        const uint16_t *rowPtr = &buffer[static_cast<size_t>(row) * samplesPerRow];
        if (TIFFWriteScanline(tif, const_cast<uint16_t *>(rowPtr), row, 0) < 0) {
            TIFFClose(tif);
            error = QStringLiteral("Failed to write linear DNG scanline.");
            return false;
        }
        if ((row & 0x3F) == 0) {
            logProcessing("writing linear row %d/%d", row, height);
        }
    }

    if (!TIFFWriteDirectory(tif)) {
        TIFFClose(tif);
        error = QStringLiteral("Failed to finalize primary DNG directory.");
        return false;
    }

    if (!writeReducedPreviewDirectory(tif, metadata.embeddedThumbnail)) {
        TIFFClose(tif);
        error = QStringLiteral("Failed to write embedded preview image.");
        return false;
    }

    TIFFClose(tif);
    if (!patchExifDirectoryInPlace(outputPath, metadata, &error)) {
        return false;
    }
    logProcessing("Linear DNG write completed: %s (%dx%d)", outputPath.toUtf8().constData(), width, height);
    return true;
}

bool writeRgbTiff16WithLibTiff(const QString &outputPath,
                               const QImage &image,
                               const SuperCCDMetadata *metadata,
                               QString &error)
{
    const QImage rgbImage = image.convertToFormat(QImage::Format_RGBX64);
    TIFFSetErrorHandler(libtiffErrorHandler);
    TIFF *tif = TIFFOpen(outputPath.toUtf8().constData(), "w");
    if (!tif) {
        error = QStringLiteral("Unable to open output TIFF file.");
        return false;
    }

    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, static_cast<uint32_t>(rgbImage.width()));
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, static_cast<uint32_t>(rgbImage.height()));
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, static_cast<uint16_t>(16));
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, static_cast<uint16_t>(3));
    TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, static_cast<uint16_t>(PHOTOMETRIC_RGB));
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, static_cast<uint16_t>(PLANARCONFIG_CONTIG));
    TIFFSetField(tif, TIFFTAG_COMPRESSION, static_cast<uint16_t>(COMPRESSION_NONE));
    TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, static_cast<uint16_t>(SAMPLEFORMAT_UINT));
    TIFFSetField(tif, TIFFTAG_ORIENTATION, static_cast<uint16_t>(ORIENTATION_TOPLEFT));
    TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(tif, 0));

    QByteArray makeBytes;
    QByteArray modelBytes;
    QByteArray softwareBytes = QByteArrayLiteral("superccd2dng");
    QByteArray dateTimeBytes;
    QByteArray lensModelBytes;
    if (metadata) {
        makeBytes = metadata->make.toUtf8();
        modelBytes = metadata->model.toUtf8();
        if (!metadata->software.trimmed().isEmpty()) {
            softwareBytes = metadata->software.toUtf8();
        }
        dateTimeBytes = metadata->dateTime.toLatin1();
        lensModelBytes = metadata->lensModel.trimmed().toUtf8();
        writeCommonCaptureMetadata(tif,
                                   *metadata,
                                   makeBytes,
                                   modelBytes,
                                   softwareBytes,
                                   dateTimeBytes,
                                   lensModelBytes);
    } else {
        TIFFSetField(tif, TIFFTAG_SOFTWARE, softwareBytes.constData());
    }

    std::vector<uint16_t> rowBuffer(static_cast<size_t>(rgbImage.width()) * 3);
    for (int y = 0; y < rgbImage.height(); ++y) {
        const QRgba64 *source = reinterpret_cast<const QRgba64 *>(rgbImage.constScanLine(y));
        for (int x = 0; x < rgbImage.width(); ++x) {
            const size_t offset = static_cast<size_t>(x) * 3;
            rowBuffer[offset + 0] = source[x].red();
            rowBuffer[offset + 1] = source[x].green();
            rowBuffer[offset + 2] = source[x].blue();
        }
        if (TIFFWriteScanline(tif, rowBuffer.data(), y, 0) < 0) {
            TIFFClose(tif);
            error = QStringLiteral("Failed to write 16-bit TIFF scanline.");
            return false;
        }
    }

    TIFFClose(tif);
    return true;
}
#endif
}

bool DngWriter::writeDng(const QString &outputPath,
                         const std::vector<uint16_t> &buffer,
                         int width,
                         int height,
                         int bitDepth,
                         const SuperCCDMetadata &metadata,
                         QString &error)
{
    logProcessing("DngWriter::writeDng entry: %s (%dx%d)", outputPath.toUtf8().constData(), width, height);
    if (width <= 0 || height <= 0 || buffer.empty()) {
        error = QStringLiteral("Invalid image data for DNG export.");
        return false;
    }

#ifdef HAVE_DNG_SDK
    Q_UNUSED(outputPath)
    Q_UNUSED(buffer)
    Q_UNUSED(width)
    Q_UNUSED(height)
    Q_UNUSED(bitDepth)
    Q_UNUSED(metadata)
    error = QStringLiteral("Adobe DNG SDK support is not yet implemented in this version.");
    return false;
#elif defined(HAVE_LIBTIFF)
    return writeDngWithLibTiff(outputPath, buffer, width, height, bitDepth, metadata, error);
#else
    Q_UNUSED(outputPath)
    Q_UNUSED(buffer)
    Q_UNUSED(width)
    Q_UNUSED(height)
    Q_UNUSED(bitDepth)
    Q_UNUSED(metadata)
    error = QStringLiteral("No DNG writer available. Install LibTIFF or Adobe DNG SDK and rebuild the project.");
    return false;
#endif
}

bool DngWriter::writeLinearDng(const QString &outputPath,
                               const std::vector<uint16_t> &rgbBuffer,
                               int width,
                               int height,
                               int bitDepth,
                               const SuperCCDMetadata &metadata,
                               QString &error)
{
    logProcessing("DngWriter::writeLinearDng entry: %s (%dx%d)", outputPath.toUtf8().constData(), width, height);
    if (width <= 0 || height <= 0 || rgbBuffer.size() < static_cast<size_t>(width) * static_cast<size_t>(height) * 3) {
        error = QStringLiteral("Invalid RGB image data for linear DNG export.");
        return false;
    }

#ifdef HAVE_DNG_SDK
    Q_UNUSED(outputPath)
    Q_UNUSED(rgbBuffer)
    Q_UNUSED(width)
    Q_UNUSED(height)
    Q_UNUSED(bitDepth)
    Q_UNUSED(metadata)
    error = QStringLiteral("Adobe DNG SDK support is not yet implemented in this version.");
    return false;
#elif defined(HAVE_LIBTIFF)
    return writeLinearDngWithLibTiff(outputPath, rgbBuffer, width, height, bitDepth, metadata, error);
#else
    Q_UNUSED(outputPath)
    Q_UNUSED(rgbBuffer)
    Q_UNUSED(width)
    Q_UNUSED(height)
    Q_UNUSED(bitDepth)
    Q_UNUSED(metadata)
    error = QStringLiteral("No DNG writer available. Install LibTIFF or Adobe DNG SDK and rebuild the project.");
    return false;
#endif
}

bool DngWriter::writeRgbTiff16(const QString &outputPath,
                               const QImage &image,
                               const SuperCCDMetadata *metadata,
                               QString &error)
{
    if (image.isNull()) {
        error = QStringLiteral("Invalid image data for TIFF export.");
        return false;
    }

#ifdef HAVE_LIBTIFF
    return writeRgbTiff16WithLibTiff(outputPath, image, metadata, error);
#else
    Q_UNUSED(outputPath)
    Q_UNUSED(metadata)
    error = QStringLiteral("16-bit TIFF export requires LibTIFF support.");
    return false;
#endif
}
