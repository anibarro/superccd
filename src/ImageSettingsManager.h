#ifndef IMAGESETTINGSMANAGER_H
#define IMAGESETTINGSMANAGER_H

#include <QString>
#include <QJsonObject>

// ImageSettingsManager handles per-image settings stored in a global
// JSON file within each output folder. This allows users to have different
// settings for different images that are automatically restored when an
// image is loaded.
//
// The settings file is named ".superccd_settings.json" and contains a
// dictionary mapping image filenames to their settings.
class ImageSettingsManager
{
public:
    ImageSettingsManager();

    // Sets the output folder where the settings file should be stored.
    // This should be called whenever the output folder changes.
    void setOutputFolder(const QString &folder);

    // Returns the current output folder.
    QString outputFolder() const;

    // Returns the path to the settings file in the current output folder.
    QString settingsFilePath() const;

    // Loads settings for a specific image file. Returns an empty QJsonObject
    // if no settings exist for the given filename.
    QJsonObject loadImageSettings(const QString &imageFilename) const;

    // Saves settings for a specific image file. The settings are written
    // to the global settings file immediately.
    bool saveImageSettings(const QString &imageFilename, const QJsonObject &settings);

    // Returns true if settings exist for the given image filename.
    bool hasImageSettings(const QString &imageFilename) const;

    // Removes settings for a specific image file.
    bool removeImageSettings(const QString &imageFilename);

    // Clears all stored image settings.
    void clearAllSettings();

private:
    // Loads the entire settings file into memory.
    QJsonObject loadSettingsFile() const;

    // Saves the entire settings object to disk.
    bool saveSettingsFile(const QJsonObject &root) const;

    QString m_outputFolder;
    static const QString kSettingsFileName;
};

#endif // IMAGESETTINGSMANAGER_H
