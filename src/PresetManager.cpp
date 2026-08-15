#include "PresetManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

#if defined(Q_OS_MACOS)
#include <QStandardPaths>
#endif

const QString PresetManager::kDefaultPresetName = QStringLiteral("Default");

// Resolve the folder where preset JSON files live. On macOS, when running
// from an .app bundle, that means "<bundle>/Contents/Resources/presets".
// On Windows/Linux we keep the presets next to the executable.
PresetManager::PresetManager()
{
#if defined(Q_OS_MACOS)
    const QString appDir = QCoreApplication::applicationDirPath();
    // macOS bundle layout: SuperCCD2DNG.app/Contents/MacOS/superccd2dng
    // Resources sits beside MacOS inside Contents.
    const QFileInfo appDirInfo(appDir);
    const QDir contentsDir = appDirInfo.dir(); // Contents
    if (contentsDir.dirName() == QStringLiteral("Contents")) {
        m_presetsDir = QDir(contentsDir).filePath(QStringLiteral("Resources/presets"));
    } else {
        m_presetsDir = QDir(appDir).filePath(QStringLiteral("presets"));
    }
#else
    m_presetsDir = QDir(QCoreApplication::applicationDirPath())
                       .filePath(QStringLiteral("presets"));
#endif
}

QString PresetManager::presetsDirectory() const
{
    return m_presetsDir;
}

QString PresetManager::presetFilePath(const QString &name) const
{
    if (name.isEmpty()) {
        return QString();
    }
    return QDir(m_presetsDir).filePath(name + QStringLiteral(".json"));
}

bool PresetManager::isDefaultPreset(const QString &name)
{
    return name.compare(kDefaultPresetName, Qt::CaseInsensitive) == 0;
}

QStringList PresetManager::availablePresets() const
{
    QStringList result;
    result.reserve(8);

    QDir dir(m_presetsDir);
    if (dir.exists()) {
        const QStringList filters{QStringLiteral("*.json")};
        const QFileInfoList entries =
            dir.entryInfoList(filters, QDir::Files | QDir::Readable, QDir::Name);
        for (const QFileInfo &info : entries) {
            const QString baseName = info.completeBaseName();
            if (baseName.isEmpty()) {
                continue;
            }
            if (isDefaultPreset(baseName)) {
                // Keep an on-disk Default preset at the top, but do not
                // expose one when its file is not available.
                if (!result.contains(baseName)) {
                    result.prepend(baseName);
                }
                continue;
            }
            result.append(baseName);
        }
    }

    return result;
}

bool PresetManager::presetExists(const QString &name) const
{
    if (name.isEmpty()) {
        return false;
    }
    return QFileInfo::exists(presetFilePath(name));
}

QJsonObject PresetManager::loadPreset(const QString &name) const
{
    if (name.isEmpty()) {
        return QJsonObject();
    }

    const QString path = presetFilePath(name);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QJsonObject();
    }

    const QByteArray data = file.readAll();
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return QJsonObject();
    }
    return doc.object();
}

bool PresetManager::savePreset(const QString &name, const QJsonObject &data)
{
    if (name.isEmpty()) {
        return false;
    }
    if (isDefaultPreset(name)) {
        return false; // the default preset is immutable
    }

    QDir dir(m_presetsDir);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        return false;
    }

    QSaveFile file(presetFilePath(name));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    QJsonDocument doc(data);
    const QByteArray serialized = doc.toJson(QJsonDocument::Indented);
    if (file.write(serialized) != serialized.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

bool PresetManager::deletePreset(const QString &name)
{
    if (name.isEmpty() || isDefaultPreset(name)) {
        return false;
    }
    const QString path = presetFilePath(name);
    if (!QFileInfo::exists(path)) {
        return false;
    }
    return QFile::remove(path);
}
