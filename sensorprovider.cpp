#include "sensorprovider.h"

#include <QTimer>
#include <QtMath>

SimulatedSensorProvider::SimulatedSensorProvider(QObject *parent)
    : ISensorProvider(parent), timer_(new QTimer(this)), sampleIndex_(0), enabledDevices_(SensorAll)
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
    update(QStringLiteral("VEML7700 / I2C"), SensorVeml7700);
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
    snapshot.illuminance = enabledDevices_ & SensorVeml7700
        ? qMax(0.0, 480.0 + 260.0 * qSin(t * 0.28 - 0.6)) : qQNaN();
    emit snapshotReady(snapshot);
}
