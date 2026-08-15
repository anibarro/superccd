#ifndef PRESETMANAGER_H
#define PRESETMANAGER_H

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QVariantMap>

// PresetManager handles loading, saving, and deleting named presets stored
// as JSON files inside a "presets" folder next to the application executable
// (or inside the macOS .app bundle Resources directory).
//
// The on-disk "Default" preset, when present, cannot be overwritten or
// deleted. When no matching file is found on disk the manager returns an
// empty object so callers can apply their own factory defaults.
class PresetManager
{
public:
    // The protected default preset name. It is listed first only when its
    // corresponding file is available.
    static const QString kDefaultPresetName;

    PresetManager();

    // Directory that stores the user-visible *.json preset files. On
    // Windows/Linux this is "<appDir>/presets". On macOS it resolves to
    // "<appBundle>/Contents/Resources/presets" when running from a bundle.
    QString presetsDirectory() const;

    // Returns the list of preset names (without the .json extension) that
    // are currently available. If present, "Default" is the first entry.
    QStringList availablePresets() const;

    // Returns true when a preset with the given name already exists on
    // disk. The comparison is case-insensitive on Windows, case-sensitive
    // elsewhere (matching the underlying filesystem behavior).
    bool presetExists(const QString &name) const;

    // Loads the JSON object associated with the given preset name. If the
    // file cannot be read (or the name is empty) an empty QJsonObject is
    // returned so the caller can safely fall back to its own defaults.
    QJsonObject loadPreset(const QString &name) const;

    // Writes the given JSON object to "<presetsDirectory>/<name>.json".
    // Returns false if the directory could not be created or the file
    // could not be written. Writing the Default preset is rejected even
    // though the UI already guards against this.
    bool savePreset(const QString &name, const QJsonObject &data);

    // Deletes the preset file associated with the given name. Returns
    // false if the file does not exist, could not be removed, or the
    // caller is trying to delete the protected Default preset.
    bool deletePreset(const QString &name);

    // Returns the absolute file path for the given preset name (regardless
    // of whether the file actually exists).
    QString presetFilePath(const QString &name) const;

    // Whether the given name matches the protected built-in preset.
    static bool isDefaultPreset(const QString &name);

private:
    QString m_presetsDir;
};

#endif // PRESETMANAGER_H
