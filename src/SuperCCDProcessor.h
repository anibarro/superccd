#ifndef SUPERCCDPROCESSOR_H
#define SUPERCCDPROCESSOR_H

#include <QString>
#include <QImage>
#include <QDateTime>
#include <array>
#include <vector>

enum class ExportMode {
    RawCfa6MP,
    Linear12MPExperimental
};

struct ConversionSettings {
    ExportMode exportMode = ExportMode::RawCfa6MP;
    int previewMaxSize = 960;
    int previewRotation = 0;
    double rHeadroomScale = 1.0;
    double rTransitionDelay = 0.0;
    double rTransitionSmoothness = 0.65;
    double linearChromaSuppression = 1.0;
    bool correctPreviewOutliers = false;
    bool exportPlaneImages = false;  // Export S and R plane images alongside merged DNG
    double toneGamma = 2.2;  // Preview gamma (default sRGB gamma)
    double toneContrast = 0.0;  // Preview contrast adjustment (-0.5 to +0.5 range)
};

struct SuperCCDMetadata {
    QString make;
    QString model;
    QString software;
    QString dateTime;
    int iso = 0;
    double shutter = 0.0;
    double aperture = 0.0;
    uint16_t whiteLevel = 0;
    std::array<double, 4> blackLevels = {0.0, 0.0, 0.0, 0.0};
    bool hasBlackLevels = false;
    std::array<double, 3> asShotNeutral = {1.0, 1.0, 1.0};
    bool hasAsShotNeutral = false;
    double asShotTint = 0.0;  // Tint adjustment for green channel balance
    std::array<double, 9> colorMatrix1 = {
        1.0, 0.0, 0.0,
        0.0, 1.0, 0.0,
        0.0, 0.0, 1.0
    };
    bool hasColorMatrix1 = false;
    double baselineExposure = 0.0;
    bool hasBaselineExposure = false;
    int fujiWidth = 0;
    QImage embeddedThumbnail;

    // Rotation/flip info from RAF (0=none, 1=90CCW, 2=180, 3=270, negative=mirrored)
    int flip = 0;
    bool hasFlip = false;

    // FujiCurve tone curve data (65536 entries of 16-bit values)
    std::vector<unsigned short> curve;
    bool hasCurve = false;

    // Export FujiCurve as CSV LUT file
    bool exportCurveToCsv(const QString &filePath) const;
    // Export as 3DL LUT file (for color grading software)
    bool exportCurveTo3dl(const QString &filePath) const;
};

class SuperCCDProcessor
{
public:
    static bool extractEmbeddedThumbnail(const QString &inputPath,
                                         QImage &thumbnail,
                                         QString *error = nullptr);

    bool process(const QString &inputPath,
                 const QString &outputPath,
                 const ConversionSettings &settings,
                 QString &error);

    bool renderPreview(const QString &inputPath,
                       const ConversionSettings &settings,
                       QImage &preview,
                       QString &error);

private:
    struct CfaPreviewCache {
        QString inputPath;
        QDateTime lastModifiedUtc;
        qint64 fileSize = -1;
        bool valid = false;
        std::vector<uint16_t> sCfa;
        std::vector<uint16_t> rCfa;
        std::vector<uint8_t> sChannels;
        std::vector<uint8_t> rChannels;
        std::vector<uint16_t> projectedR;
        std::vector<uint16_t> sUpsampled;  // S upsampled to full resolution (same size as CFA)
        std::vector<uint16_t> rUpsampled;  // R upsampled to full resolution (same size as CFA)
        int width = 0;
        int height = 0;
        int upsampledWidth = 0;  // Same as width (kept for API compatibility)
        int upsampledHeight = 0;  // Same as height (kept for API compatibility)
        int bitDepth = 0;
        SuperCCDMetadata metadata;
    };

    bool ensure6MPCache(const QString &inputPath,
                        CfaPreviewCache &cache,
                        QString &error);

    bool readRaw(const QString &inputPath,
                 bool output12MP,
                 std::vector<uint16_t> &rawData,
                 int &width,
                 int &height,
                 int &bitDepth,
                 SuperCCDMetadata &metadata,
                 QString &error);

    bool readProcessedRgb(const QString &inputPath,
                          std::vector<uint16_t> &rgbData,
                          int &width,
                          int &height,
                          int &bitDepth,
                          SuperCCDMetadata &metadata,
                          QString &error);

    bool readLinearPlaneRgb(const QString &inputPath,
                            int planeChannel,
                            std::vector<uint16_t> &rgbData,
                            int &width,
                            int &height,
                            int &bitDepth,
                            SuperCCDMetadata &metadata,
                            QString &error);

    bool readNativeRawPartitionRgb(const QString &inputPath,
                                   int partitionMode,
                                   int partitionClass,
                                   std::vector<uint16_t> &rgbData,
                                   int &width,
                                   int &height,
                                   int &bitDepth,
                                   SuperCCDMetadata &metadata,
                                   QString &error);

    bool readNativeVerticalPairRgb(const QString &inputPath,
                                   bool brighterSample,
                                   int columnParity,
                                   std::vector<uint16_t> &rgbData,
                                   int &width,
                                   int &height,
                                   int &bitDepth,
                                   SuperCCDMetadata &metadata,
                                   QString &error);

    bool readNativeSameColorPairRgb(const QString &inputPath,
                                    int dx,
                                    int dy,
                                    bool brighterSample,
                                    std::vector<uint16_t> &rgbData,
                                    int &width,
                                    int &height,
                                    int &bitDepth,
                                    SuperCCDMetadata &metadata,
                                    QString &error);

    bool readSelectedShotCfa(const QString &inputPath,
                             int shotSelect,
                             bool output12MP,
                             std::vector<uint16_t> &cfaData,
                             int &width,
                             int &height,
                             int &bitDepth,
                             SuperCCDMetadata &metadata,
                             std::vector<uint8_t> *channelMap,
                             QString &error);

    bool reconstructSuperCCD(const std::vector<uint16_t> &rawData,
                             int width,
                             int height,
                             bool output12MP,
                             std::vector<uint16_t> &outputData,
                             int &outWidth,
                             int &outHeight,
                             QString &error);

    CfaPreviewCache m_cfaPreviewCache;
};

#endif // SUPERCCDPROCESSOR_H
