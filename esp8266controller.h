#ifndef ESP8266CONTROLLER_H
#define ESP8266CONTROLLER_H
#include <QObject>
#include <QVector>
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
signals:
    void portStateChanged(bool open, const QString &detail);
    void scanFinished(const QVector<WifiNetwork> &networks);
    void connectionStateChanged(bool connected, const QString &detail);
    void operationFailed(const QString &message);
private slots:
    void readAvailable();
    void timeout();
private:
    enum Operation { Idle, WaitingForScanMode, Scanning, Connecting, QueryingIp };
    void sendCommand(const QByteArray &command, int timeoutMs);
    void processLine(const QByteArray &line);
    void finishWithError(const QString &message);
    static QString escapeArgument(const QString &value);
    int fd_;
    QSocketNotifier *notifier_;
    QTimer *timeoutTimer_;
    QByteArray receiveBuffer_;
    QVector<WifiNetwork> networks_;
    Operation operation_;
    bool simulated_;
};
Q_DECLARE_METATYPE(WifiNetwork)
Q_DECLARE_METATYPE(QVector<WifiNetwork>)
#endif
