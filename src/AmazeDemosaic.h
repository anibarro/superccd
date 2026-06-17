#ifndef AMAZEDEMOSAIC_H
#define AMAZEDEMOSAIC_H

#include "SuperCCDProcessor.h"

#include <QString>
#include <cstdint>
#include <vector>

bool demosaicSparseBayerAmaze(const std::vector<uint16_t> &cfa,
                              int width,
                              int height,
                              const SuperCCDMetadata &metadata,
                              std::vector<uint16_t> &rgb,
                              int &rgbWidth,
                              int &rgbHeight,
                              QString &error);

#endif // AMAZEDEMOSAIC_H
