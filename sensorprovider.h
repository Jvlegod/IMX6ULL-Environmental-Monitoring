#ifndef SENSORPROVIDER_H
#define SENSORPROVIDER_H

#include <QObject>
#include <QDateTime>
#include <QString>

struct SensorSnapshot
{
    QDateTime timestamp;
    double temperature = 0.0;
    double humidity = 0.0;
    double pressure = 0.0;
    bool collisionWarning = false;
    double illuminance = 0.0;
};

enum SensorDevice {
    SensorBmp580 = 1,
    SensorRs485 = 2,
    SensorVeml7700 = 4,
    SensorAll = SensorBmp580 | SensorRs485 | SensorVeml7700
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
    void updateDeviceStatuses();
    QString discoverBmp580Path() const;
    bool readBmp580(double *temperature, double *pressure, QString *errorMessage) const;
    QString discoverIcm20608Path() const;
    bool readIcm20608(double *accelMagnitude, QString *errorMessage) const;
    QString discoverVeml7700Path() const;
    bool readVeml7700(double *illuminance, QString *errorMessage) const;
    class QTimer *timer_;
    int sampleIndex_;
    int enabledDevices_;
    QString bmp580Path_;
    bool bmp580Online_;
    QString veml7700Path_;
    bool veml7700Online_;
    QString icm20608Path_;
    bool icm20608BaselineValid_;
    double icm20608Baseline_;
};

#endif
