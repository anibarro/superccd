#ifndef DNGWRITER_H
#define DNGWRITER_H

#include <QImage>
#include <QString>
#include <vector>
#include "SuperCCDProcessor.h"

class DngWriter
{
public:
    static bool writeDng(const QString &outputPath,
                         const std::vector<uint16_t> &buffer,
                         int width,
                         int height,
                         int bitDepth,
                         const SuperCCDMetadata &metadata,
                         QString &error);

    static bool writeLinearDng(const QString &outputPath,
                               const std::vector<uint16_t> &rgbBuffer,
                               int width,
                               int height,
                               int bitDepth,
                               const SuperCCDMetadata &metadata,
                               QString &error);

    static bool writeRgbTiff16(const QString &outputPath,
                               const QImage &image,
                               const SuperCCDMetadata *metadata,
                               QString &error);
};

#endif // DNGWRITER_H
