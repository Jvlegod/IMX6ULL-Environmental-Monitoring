#include "appsettings.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace {
QString legacySettingsPath()
{
    const QString homePath = QDir::homePath();
    return homePath + QStringLiteral("/.config/jvle/environment_monitor.conf");
}

void migrateLegacySettings(const QString &path)
{
    if (QFileInfo::exists(path))
        return;

    QStringList candidates{legacySettingsPath()};
    if (!candidates.contains(QStringLiteral("/root/.config/jvle/environment_monitor.conf")))
        candidates << QStringLiteral("/root/.config/jvle/environment_monitor.conf");
    for (const QString &candidate : candidates) {
        if (!QFileInfo::exists(candidate))
            continue;
        QSettings oldSettings(candidate, QSettings::IniFormat);
        QSettings newSettings(path, QSettings::IniFormat);
        for (const QString &key : oldSettings.allKeys())
            newSettings.setValue(key, oldSettings.value(key));
        newSettings.sync();
        return;
    }
}
}

QString environmentMonitorSettingsPath()
{
    QString directory = qEnvironmentVariable("ENVIRONMENT_MONITOR_CONFIG_DIR");
    if (directory.isEmpty() && QFileInfo(QStringLiteral("/etc")).isWritable())
        directory = QStringLiteral("/etc/environment_monitor");
    if (directory.isEmpty())
        directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (directory.isEmpty())
        directory = QDir::homePath() + QStringLiteral("/.config/environment_monitor");

    QDir().mkpath(directory);
    const QString path = directory + QStringLiteral("/settings.ini");
    migrateLegacySettings(path);
    return path;
}
