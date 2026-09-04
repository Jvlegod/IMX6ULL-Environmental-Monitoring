#include "sensorprovider.h"

#include <QTimer>
#include <QtMath>

SimulatedSensorProvider::SimulatedSensorProvider(QObject *parent)
    : ISensorProvider(parent), timer_(new QTimer(this)), sampleIndex_(0)
{
    timer_->setInterval(1000);
    connect(timer_, &QTimer::timeout, this, &SimulatedSensorProvider::sample);
}

void SimulatedSensorProvider::start()
{
    if (timer_->isActive())
        return;

    sampleIndex_ = 0;
    emit deviceStatusChanged(QStringLiteral("BMP280 / I2C"), true,
                             QStringLiteral("模拟数据"));
    emit deviceStatusChanged(QStringLiteral("RS485 温湿度计"), true,
                             QStringLiteral("模拟数据"));
    emit deviceStatusChanged(QStringLiteral("VEML7700 / I2C"), true,
                             QStringLiteral("模拟数据"));
    emit deviceStatusChanged(QStringLiteral("串口 WiFi"), false,
                             QStringLiteral("等待 ESP8266 配置"));
    sample();
    timer_->start();
}

void SimulatedSensorProvider::stop()
{
    timer_->stop();
    emit deviceStatusChanged(QStringLiteral("采集服务"), false,
                             QStringLiteral("已暂停"));
}

void SimulatedSensorProvider::setSamplingInterval(int intervalMs)
{
    timer_->setInterval(qMax(1000, intervalMs));
}

void SimulatedSensorProvider::sample()
{
    const double t = sampleIndex_++ / 10.0;
    SensorSnapshot snapshot;
    snapshot.timestamp = QDateTime::currentDateTime();
    snapshot.temperature = 23.5 + 1.8 * qSin(t) + 0.15 * qSin(t * 3.0);
    snapshot.humidity = 54.0 + 8.0 * qSin(t * 0.72 + 0.8);
    snapshot.pressure = 101.25 + 0.32 * qSin(t * 0.35);
    snapshot.illuminance = qMax(0.0, 480.0 + 260.0 * qSin(t * 0.28 - 0.6));
    emit snapshotReady(snapshot);
}
