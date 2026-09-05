#include "sensorprovider.h"

#include <QDir>
#include <QFile>
#include <QTimer>
#include <QtMath>

SimulatedSensorProvider::SimulatedSensorProvider(QObject *parent)
    : ISensorProvider(parent), timer_(new QTimer(this)), sampleIndex_(0), enabledDevices_(SensorAll),
      veml7700Path_(discoverVeml7700Path()), veml7700Online_(!veml7700Path_.isEmpty())
{
    timer_->setInterval(1000);
    connect(timer_, &QTimer::timeout, this, &SimulatedSensorProvider::sample);
}

void SimulatedSensorProvider::start()
{
    if (timer_->isActive())
        return;

    sampleIndex_ = 0;
    timer_->start();
    emit deviceStatusChanged(QStringLiteral("采集服务"), true,
                             QStringLiteral("采集中"));
    updateDeviceStatuses();
    emit deviceStatusChanged(QStringLiteral("串口 WiFi"), false,
                             QStringLiteral("等待 ESP8266 配置"));
    sample();
}

void SimulatedSensorProvider::stop()
{
    timer_->stop();
    emit deviceStatusChanged(QStringLiteral("采集服务"), false,
                             QStringLiteral("已暂停"));
    updateDeviceStatuses();
}

void SimulatedSensorProvider::setSamplingInterval(int intervalMs)
{
    timer_->setInterval(qMax(1000, intervalMs));
}

void SimulatedSensorProvider::setEnabledDevices(int deviceMask)
{
    enabledDevices_ = deviceMask & SensorAll;
    updateDeviceStatuses();
}

void SimulatedSensorProvider::updateDeviceStatuses()
{
    const bool collecting = timer_->isActive();
    const auto update = [this, collecting](const QString &device, int flag) {
        const bool enabled = enabledDevices_ & flag;
        emit deviceStatusChanged(device, enabled && collecting,
                                 enabled ? (collecting ? QStringLiteral("模拟采集") : QStringLiteral("等待采集"))
                                         : QStringLiteral("已禁用"));
    };
    update(QStringLiteral("BMP280 / I2C"), SensorBmp280);
    update(QStringLiteral("RS485 温湿度计"), SensorRs485);
    const bool vemlEnabled = enabledDevices_ & SensorVeml7700;
    emit deviceStatusChanged(QStringLiteral("VEML7700 / I2C"),
                             vemlEnabled && collecting && veml7700Online_,
                             !vemlEnabled ? QStringLiteral("已禁用")
                                          : veml7700Path_.isEmpty()
                                                ? (collecting ? QStringLiteral("IIO 不可用") : QStringLiteral("等待采集"))
                                                : (collecting
                                                       ? (veml7700Online_ ? QStringLiteral("IIO 采集")
                                                                          : QStringLiteral("IIO 读取失败"))
                                                       : QStringLiteral("等待采集")));
}

void SimulatedSensorProvider::sample()
{
    const double t = sampleIndex_++ / 10.0;
    SensorSnapshot snapshot;
    snapshot.timestamp = QDateTime::currentDateTime();
    snapshot.temperature = enabledDevices_ & SensorBmp280
        ? 23.5 + 1.8 * qSin(t) + 0.15 * qSin(t * 3.0) : qQNaN();
    snapshot.pressure = enabledDevices_ & SensorBmp280
        ? 101.25 + 0.32 * qSin(t * 0.35) : qQNaN();
    snapshot.humidity = enabledDevices_ & SensorRs485
        ? 54.0 + 8.0 * qSin(t * 0.72 + 0.8) : qQNaN();
    if (enabledDevices_ & SensorVeml7700) {
        if (veml7700Path_.isEmpty())
            veml7700Path_ = discoverVeml7700Path();

        QString errorMessage;
        if (veml7700Path_.isEmpty() || !readVeml7700(&snapshot.illuminance, &errorMessage)) {
            snapshot.illuminance = qQNaN();
            if (veml7700Online_) {
                veml7700Online_ = false;
                if (!errorMessage.isEmpty())
                    emit providerError(QStringLiteral("VEML7700 IIO 读取失败: %1").arg(errorMessage));
                updateDeviceStatuses();
            }
        } else if (!veml7700Online_) {
            veml7700Online_ = true;
            updateDeviceStatuses();
        }
    } else {
        snapshot.illuminance = qQNaN();
    }
    emit snapshotReady(snapshot);
}

QString SimulatedSensorProvider::discoverVeml7700Path() const
{
    const QString configuredPath = qEnvironmentVariable("ENVIRONMENT_MONITOR_VEML7700_SYSFS").trimmed();
    if (!configuredPath.isEmpty())
        return QFile::exists(configuredPath) ? configuredPath : QString();

    const QString iioRoot = qEnvironmentVariable("ENVIRONMENT_MONITOR_IIO_ROOT",
                                                  QStringLiteral("/sys/bus/iio/devices"));
    const QDir root(iioRoot);
    const QStringList devices = root.entryList(QStringList() << QStringLiteral("iio:device*"),
                                                QDir::Dirs | QDir::NoDotAndDotDot,
                                                QDir::Name);
    for (const QString &device : devices) {
        QFile nameFile(root.filePath(device + QStringLiteral("/name")));
        if (!nameFile.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        if (QString::fromLocal8Bit(nameFile.readAll()).trimmed().compare(QStringLiteral("veml7700"),
                                                                           Qt::CaseInsensitive) == 0)
            return root.filePath(device);
    }
    return QString();
}

bool SimulatedSensorProvider::readVeml7700(double *illuminance, QString *errorMessage) const
{
    const QString inputPath = veml7700Path_.endsWith(QStringLiteral("_input"))
        ? veml7700Path_ : veml7700Path_ + QStringLiteral("/in_illuminance_input");
    QFile inputFile(inputPath);
    if (!inputFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage)
            *errorMessage = inputFile.errorString();
        return false;
    }

    bool ok = false;
    const double value = QString::fromLocal8Bit(inputFile.readAll()).trimmed().toDouble(&ok);
    if (!ok || !qIsFinite(value) || value < 0.0) {
        if (errorMessage)
            *errorMessage = QStringLiteral("in_illuminance_input 返回无效值");
        return false;
    }
    *illuminance = value;
    return true;
}
