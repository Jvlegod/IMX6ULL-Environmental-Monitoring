#ifndef ESP8266CONTROLLER_H
#define ESP8266CONTROLLER_H

#include <QObject>
#include <QVector>
#include <QJsonObject>
#include <QQueue>
#include "sensorprovider.h"

class QSocketNotifier;
class QTimer;

struct WifiNetwork
{
    WifiNetwork(const QString &ssidValue = QString(), int rssiValue = -100, int encryptionValue = 0)
        : ssid(ssidValue), rssi(rssiValue), encryption(encryptionValue) {}
    QString ssid;
    int rssi;
    int encryption;
};

class Esp8266Controller final : public QObject
{
    Q_OBJECT
public:
    explicit Esp8266Controller(QObject *parent = nullptr);
    ~Esp8266Controller() override;
    static QStringList availablePorts();
    bool openPort(const QString &path, int baudRate = 115200);
    void closePort();
    bool isOpen() const;
    void scanNetworks();
    void connectNetwork(const QString &ssid, const QString &password);
    void startOta(const QString &host, quint16 port, const QString &manifestPath);
    void publishTelemetry(const SensorSnapshot &snapshot);

signals:
    void portStateChanged(bool open, const QString &detail);
    void scanFinished(const QVector<WifiNetwork> &networks);
    void connectionStateChanged(bool connected, const QString &detail);
    void operationFailed(const QString &message);
    void otaProgress(qint64 received, qint64 total);
    void otaPackageReady(const QString &version, const QString &path);
    void systemUpdateReady(const QString &updateId, const QString &directory);
    void remoteSamplingInterval(int seconds);
    void remoteDeviceEnabled(const QString &device, bool enabled);
    void remoteThresholds(double temperatureMin, double temperatureMax, double humidityMin, double humidityMax,
                          double pressureMin, double pressureMax, double illuminanceMin, double illuminanceMax);

private slots:
    void readAvailable();
    void timeout();
    void pollWifiStatus();
    void pollRemoteCommand();

private:
    enum Operation { Idle, WaitingForScanMode, Scanning, Connecting, QueryingIp, QueryingStatus, CommandConnecting, CommandWaitingPrompt, CommandSending, HttpConnecting, HttpWaitingPrompt, HttpSending,
                     OtaSettingMode, OtaClosing, OtaConnecting, OtaWaitingPrompt, OtaReceiving };
    void sendCommand(const QByteArray &command, int timeoutMs);
    void sendOtaRequest();
    void processLine(const QByteArray &line);
    void processIpdPayload(const QByteArray &payload);
    void processHttpData(const QByteArray &data);
    void finishHttpResponse();
    void finishWithError(const QString &message);
    static QString escapeArgument(const QString &value);
    void beginOtaConnection();
    void beginOtaTcpConnection();
    bool writeSerial(const QByteArray &data);
    void startNextSystemArtifact();
    void scheduleNextTask();

    int fd_;
    QSocketNotifier *notifier_;
    QTimer *timeoutTimer_;
    QTimer *statusTimer_;
    QTimer *commandTimer_;
    QByteArray receiveBuffer_;
    QVector<WifiNetwork> networks_;
    Operation operation_;
    bool simulated_;
    bool otaPromptHandled_;
    bool otaHeadersParsed_;
    QString otaHost_;
    quint16 otaPort_;
    QString otaManifestPath_;
    QString otaVersion_;
    QString otaFilePath_;
    QString otaFileSha256_;
    qint64 otaExpectedBytes_;
    qint64 otaExpectedFileBytes_;
    qint64 otaReceivedBytes_;
    QByteArray otaHttpBody_;
    QByteArray otaHttpHeaders_;
    QByteArray otaRequest_;
    class QFile *otaFile_;
    bool otaDownloadingFile_;
    bool mqttReady_;
    QString mqttHost_;
    quint16 mqttPort_;
    QString mqttDeviceId_;
    QByteArray telemetryRequest_;
    QByteArray commandRequest_;
    QByteArray commandResponse_;
    bool commandPolling_;
    qint64 otaLastLoggedBytes_;
    bool systemUpdateActive_;
    QString systemUpdateId_;
    QString systemUpdateVersion_;
    QString systemUpdateDir_;
    QVector<QJsonObject> systemUpdateArtifacts_;
    int systemUpdateIndex_;
    QString otaLocalPath_;
    QQueue<SensorSnapshot> telemetryQueue_;
    bool commandQueued_;
    bool statusQueued_;
    bool wifiConnected_;
    bool scanQueued_;
    bool connectQueued_;
    QString queuedSsid_;
    QString queuedPassword_;
};

Q_DECLARE_METATYPE(WifiNetwork)
Q_DECLARE_METATYPE(QVector<WifiNetwork>)

#endif
