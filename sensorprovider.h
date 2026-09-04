#ifndef SENSORPROVIDER_H
#define SENSORPROVIDER_H

#include <QObject>
#include <QDateTime>

struct SensorSnapshot
{
    QDateTime timestamp;
    double temperature = 0.0;
    double humidity = 0.0;
    double pressure = 0.0;
    double illuminance = 0.0;
};

enum SensorDevice {
    SensorBmp280 = 1,
    SensorRs485 = 2,
    SensorVeml7700 = 4,
    SensorAll = SensorBmp280 | SensorRs485 | SensorVeml7700
};

Q_DECLARE_METATYPE(SensorSnapshot)

class ISensorProvider : public QObject
{
    Q_OBJECT

public:
    explicit ISensorProvider(QObject *parent = nullptr) : QObject(parent) {}
    ~ISensorProvider() override = default;

public slots:
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void setSamplingInterval(int intervalMs) = 0;
    virtual void setEnabledDevices(int deviceMask) = 0;

signals:
    void snapshotReady(const SensorSnapshot &snapshot);
    void deviceStatusChanged(const QString &device, bool connected, const QString &detail);
    void providerError(const QString &message);
};

class SimulatedSensorProvider final : public ISensorProvider
{
    Q_OBJECT

public:
    explicit SimulatedSensorProvider(QObject *parent = nullptr);

public slots:
    void start() override;
    void stop() override;
    void setSamplingInterval(int intervalMs) override;
    void setEnabledDevices(int deviceMask) override;

private slots:
    void sample();

private:
    class QTimer *timer_;
    int sampleIndex_;
    int enabledDevices_;
};

#endif
