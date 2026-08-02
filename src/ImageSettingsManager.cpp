#include "ImageSettingsManager.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

const QString ImageSettingsManager::kSettingsFileName = QStringLiteral(".superccd_settings.json");

ImageSettingsManager::ImageSettingsManager()
{
}

void ImageSettingsManager::setOutputFolder(const QString &folder)
{
    m_outputFolder = folder;
}

QString ImageSettingsManager::outputFolder() const
{
    return m_outputFolder;
}

QString ImageSettingsManager::settingsFilePath() const
{
    if (m_outputFolder.isEmpty()) {
        return QString();
    }
    return QDir(m_outputFolder).filePath(kSettingsFileName);
}

QJsonObject ImageSettingsManager::loadSettingsFile() const
{
    const QString path = settingsFilePath();
    if (path.isEmpty()) {
        return QJsonObject();
    }

    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return QJsonObject();
    }

    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return QJsonObject();
    }

    return doc.object();
}

bool ImageSettingsManager::saveSettingsFile(const QJsonObject &root) const
{
    const QString path = settingsFilePath();
    if (path.isEmpty()) {
        return false;
    }

    QDir dir(m_outputFolder);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    const QJsonDocument doc(root);
    file.write(doc.toJson(QJsonDocument::Indented));
    return file.commit();
}

QJsonObject ImageSettingsManager::loadImageSettings(const QString &imageFilename) const
{
    if (imageFilename.isEmpty()) {
        return QJsonObject();
    }

    const QJsonObject root = loadSettingsFile();
    if (!root.contains(imageFilename)) {
        return QJsonObject();
    }

    const QJsonValue value = root.value(imageFilename);
    if (!value.isObject()) {
        return QJsonObject();
    }

    return value.toObject();
}

bool ImageSettingsManager::saveImageSettings(const QString &imageFilename, const QJsonObject &settings)
{
    if (imageFilename.isEmpty() || m_outputFolder.isEmpty()) {
        return false;
    }

    QJsonObject root = loadSettingsFile();
    root[imageFilename] = settings;
    return saveSettingsFile(root);
}

bool ImageSettingsManager::hasImageSettings(const QString &imageFilename) const
{
    if (imageFilename.isEmpty()) {
        return false;
    }

    const QJsonObject root = loadSettingsFile();
    return root.contains(imageFilename);
}

bool ImageSettingsManager::removeImageSettings(const QString &imageFilename)
{
    if (imageFilename.isEmpty() || m_outputFolder.isEmpty()) {
        return false;
    }

    QJsonObject root = loadSettingsFile();
    if (!root.contains(imageFilename)) {
        return false;
    }

    root.remove(imageFilename);
    return saveSettingsFile(root);
}

void ImageSettingsManager::clearAllSettings()
{
    const QString path = settingsFilePath();
    if (!path.isEmpty()) {
        QFile::remove(path);
    }
}
