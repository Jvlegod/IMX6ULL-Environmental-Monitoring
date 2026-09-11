#ifndef ESP8266CONTROLLER_H
#define ESP8266CONTROLLER_H

#include <QObject>
#include <QVector>
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

private slots:
    void readAvailable();
    void timeout();
    void pollWifiStatus();

private:
    enum Operation { Idle, WaitingForScanMode, Scanning, Connecting, QueryingIp, QueryingStatus, MqttConfiguring, MqttConnecting,
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

    int fd_;
    QSocketNotifier *notifier_;
    QTimer *timeoutTimer_;
    QTimer *statusTimer_;
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
};

Q_DECLARE_METATYPE(WifiNetwork)
Q_DECLARE_METATYPE(QVector<WifiNetwork>)

#endif
