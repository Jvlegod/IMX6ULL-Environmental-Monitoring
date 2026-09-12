#include "esp8266controller.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSocketNotifier>
#include <QTimer>
#include <algorithm>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

namespace {
speed_t serialSpeed(int baud)
{
    if (baud == 9600) return B9600;
    if (baud == 57600) return B57600;
    return B115200;
}
}

Esp8266Controller::Esp8266Controller(QObject *parent)
    : QObject(parent), fd_(-1), notifier_(nullptr), timeoutTimer_(new QTimer(this)), statusTimer_(new QTimer(this)),
      operation_(Idle), simulated_(false), otaPromptHandled_(false), otaHeadersParsed_(false),
      otaPort_(80), otaExpectedBytes_(0), otaExpectedFileBytes_(0), otaReceivedBytes_(0), otaFile_(nullptr), otaDownloadingFile_(false), mqttReady_(false), mqttPort_(1883), mqttDeviceId_(QStringLiteral("gateway-001"))
{
    timeoutTimer_->setSingleShot(true);
    connect(timeoutTimer_, &QTimer::timeout, this, &Esp8266Controller::timeout);
    statusTimer_->setInterval(5000);
    connect(statusTimer_, &QTimer::timeout, this, &Esp8266Controller::pollWifiStatus);
}

Esp8266Controller::~Esp8266Controller() { closePort(); }

QStringList Esp8266Controller::availablePorts()
{
    QDir dev(QStringLiteral("/dev"));
    QStringList ports;
    const QStringList patterns{QStringLiteral("ttymxc*"), QStringLiteral("ttyUSB*"), QStringLiteral("ttyACM*"), QStringLiteral("ttyS*")};
    for (const QString &name : dev.entryList(patterns, QDir::System | QDir::Readable, QDir::Name)) ports << QStringLiteral("/dev/") + name;
    ports.removeDuplicates();
    return ports;
}

bool Esp8266Controller::openPort(const QString &path, int baudRate)
{
    closePort();
    if (path.isEmpty()) { simulated_ = true; emit portStateChanged(true, QStringLiteral("ESP8266 模拟模式")); return true; }
    fd_ = ::open(path.toLocal8Bit().constData(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) { emit operationFailed(QStringLiteral("无法打开串口 %1").arg(path)); return false; }
    termios options{};
    if (tcgetattr(fd_, &options) != 0) { closePort(); emit operationFailed(QStringLiteral("读取串口参数失败")); return false; }
    cfmakeraw(&options);
    const speed_t speed = serialSpeed(baudRate);
    cfsetispeed(&options, speed); cfsetospeed(&options, speed);
    options.c_cflag |= CLOCAL | CREAD; options.c_cflag &= ~(CSTOPB | CRTSCTS | PARENB); options.c_cflag = (options.c_cflag & ~CSIZE) | CS8;
    if (tcsetattr(fd_, TCSANOW, &options) != 0) { closePort(); emit operationFailed(QStringLiteral("设置串口参数失败")); return false; }
    tcflush(fd_, TCIOFLUSH);
    notifier_ = new QSocketNotifier(fd_, QSocketNotifier::Read, this);
    connect(notifier_, &QSocketNotifier::activated, this, &Esp8266Controller::readAvailable);
    mqttHost_ = qEnvironmentVariable("ENVIRONMENT_MONITOR_MQTT_HOST", QStringLiteral("192.168.43.4"));
    mqttPort_ = static_cast<quint16>(qEnvironmentVariableIntValue("ENVIRONMENT_MONITOR_SERVER_PORT"));
    if (mqttPort_ == 0) mqttPort_ = 8080;
    mqttDeviceId_ = qEnvironmentVariable("ENVIRONMENT_MONITOR_DEVICE_ID", QStringLiteral("gateway-001"));
    mqttReady_ = false;
    emit portStateChanged(true, QStringLiteral("%1 · %2 8N1").arg(path).arg(baudRate));
    statusTimer_->start();
    QTimer::singleShot(200, this, &Esp8266Controller::pollWifiStatus);
    return true;
}

void Esp8266Controller::closePort()
{
    const bool wasOpen = isOpen();
    timeoutTimer_->stop(); statusTimer_->stop(); delete notifier_; notifier_ = nullptr;
    if (otaFile_) { otaFile_->close(); delete otaFile_; otaFile_ = nullptr; }
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1; simulated_ = false; operation_ = Idle; receiveBuffer_.clear(); mqttReady_ = false;
    if (wasOpen) emit portStateChanged(false, QStringLiteral("ESP8266 串口已关闭"));
}

bool Esp8266Controller::isOpen() const { return simulated_ || fd_ >= 0; }


void Esp8266Controller::pollWifiStatus()
{
    if (!isOpen() || operation_ != Idle) return;
    if (simulated_) { emit connectionStateChanged(true, QStringLiteral("模拟 WiFi 已连接")); return; }
    operation_ = QueryingStatus;
    sendCommand(QByteArrayLiteral("AT+CIPSTATUS\r\n"), 3000);
}

void Esp8266Controller::scanNetworks()
{
    if (!isOpen()) { emit operationFailed(QStringLiteral("请先打开串口")); return; }
    if (operation_ != Idle) { emit operationFailed(QStringLiteral("ESP8266 当前正在执行其他操作")); return; }
    networks_.clear();
    if (simulated_) { operation_ = Scanning; QTimer::singleShot(500, this, [this] { networks_ = {{QStringLiteral("Office-WiFi"), -38, 3}, {QStringLiteral("Lab-2.4G"), -56, 3}, {QStringLiteral("Guest"), -72, 0}}; operation_ = Idle; emit scanFinished(networks_); }); return; }
    operation_ = WaitingForScanMode; sendCommand(QByteArrayLiteral("AT+CWMODE_CUR=1\r\n"), 3000);
}

void Esp8266Controller::connectNetwork(const QString &ssid, const QString &password)
{
    if (!isOpen()) { emit operationFailed(QStringLiteral("请先打开串口")); return; }
    if (operation_ != Idle) { emit operationFailed(QStringLiteral("ESP8266 当前正在执行其他操作")); return; }
    if (simulated_) { operation_ = Connecting; QTimer::singleShot(900, this, [this, ssid] { operation_ = Idle; emit connectionStateChanged(true, QStringLiteral("已连接 %1 · 192.168.1.108").arg(ssid)); }); return; }
    operation_ = Connecting;
    const QString command = QStringLiteral("AT+CWJAP=\"%1\",\"%2\"\r\n").arg(escapeArgument(ssid), escapeArgument(password));
    sendCommand(command.toUtf8(), 25000);
}

void Esp8266Controller::publishTelemetry(const SensorSnapshot &snapshot)
{
    if (operation_ != Idle || fd_ < 0) return;
    const QJsonObject object{{QStringLiteral("protocol_version"), 1},
                             {QStringLiteral("device_id"), mqttDeviceId_},
                             {QStringLiteral("timestamp"), snapshot.timestamp.toUTC().toString(Qt::ISODate)},
                             {QStringLiteral("temperature_c"), snapshot.temperature},
                             {QStringLiteral("pressure_kpa"), snapshot.pressure},
                             {QStringLiteral("humidity_percent"), snapshot.humidity},
                             {QStringLiteral("illuminance_lux"), snapshot.illuminance},
                             {QStringLiteral("collision_warning"), snapshot.collisionWarning}};
    const QByteArray body = QJsonDocument(object).toJson(QJsonDocument::Compact).trimmed();
    telemetryRequest_ = QByteArrayLiteral("POST /api/v1/telemetry HTTP/1.1\r\nHost: ")
        + mqttHost_.toUtf8() + QByteArrayLiteral("\r\nContent-Type: application/json\r\nContent-Length: ")
        + QByteArray::number(body.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body;
    operation_ = HttpConnecting;
    const QString command = QStringLiteral("AT+CIPSTART=\"TCP\",\"%1\",%2\r\n").arg(mqttHost_).arg(mqttPort_);
    sendCommand(command.toUtf8(), 10000);
}

void Esp8266Controller::startOta(const QString &host, quint16 port, const QString &manifestPath)
{
    if (!isOpen()) { emit operationFailed(QStringLiteral("请先打开 ESP8266 串口")); return; }
    if (host.trimmed().isEmpty() || manifestPath.trimmed().isEmpty()) { emit operationFailed(QStringLiteral("OTA 服务器地址和 manifest 路径不能为空")); return; }
    if (operation_ != Idle) { emit operationFailed(QStringLiteral("ESP8266 当前正在执行其他操作")); return; }
    otaHost_ = host.trimmed(); otaPort_ = port ? port : 80; otaManifestPath_ = manifestPath.trimmed();
    otaVersion_.clear(); otaFilePath_.clear(); otaFileSha256_.clear(); otaHttpBody_.clear(); otaHttpHeaders_.clear();
    otaExpectedBytes_ = 0; otaExpectedFileBytes_ = 0; otaReceivedBytes_ = 0; otaDownloadingFile_ = false;
    if (simulated_) {
        QTimer::singleShot(300, this, [this] { emit otaProgress(1, 1); emit otaPackageReady(QStringLiteral("0.2.0"), QStringLiteral("/tmp/environment_monitor.new")); });
        return;
    }
    beginOtaConnection();
}

void Esp8266Controller::beginOtaConnection()
{
    operation_ = OtaSettingMode;
    sendCommand(QByteArrayLiteral("AT+CIPMODE=0\r\n"), 3000);
}

void Esp8266Controller::beginOtaTcpConnection()
{
    otaPromptHandled_ = false; otaHeadersParsed_ = false; otaHttpBody_.clear(); otaHttpHeaders_.clear();
    operation_ = OtaConnecting;
    const QByteArray command = QStringLiteral("AT+CIPSTART=\"TCP\",\"%1\",%2\r\n").arg(escapeArgument(otaHost_)).arg(otaPort_).toUtf8();
    sendCommand(command, 10000);
}

bool Esp8266Controller::writeSerial(const QByteArray &data)
{
    if (fd_ < 0) return false;
    return ::write(fd_, data.constData(), static_cast<size_t>(data.size())) == data.size();
}

void Esp8266Controller::sendCommand(const QByteArray &command, int timeoutMs)
{
    if (fd_ >= 0 && !writeSerial(command)) { finishWithError(QStringLiteral("ESP8266 串口写入失败")); return; }
    timeoutTimer_->start(timeoutMs);
}

void Esp8266Controller::sendOtaRequest()
{
    const QString path = otaDownloadingFile_ ? otaFilePath_ : otaManifestPath_;
    otaRequest_ = QStringLiteral("GET %1 HTTP/1.1\r\nHost: %2\r\nConnection: close\r\n\r\n").arg(path, otaHost_).toUtf8();
    operation_ = OtaWaitingPrompt;
    sendCommand(QStringLiteral("AT+CIPSEND=%1\r\n").arg(otaRequest_.size()).toUtf8(), 5000);
}

void Esp8266Controller::readAvailable()
{
    char data[1024]; ssize_t size;
    while ((size = ::read(fd_, data, sizeof(data))) > 0) receiveBuffer_.append(data, static_cast<int>(size));
    while (!receiveBuffer_.isEmpty()) {
        if (operation_ == OtaWaitingPrompt && receiveBuffer_.startsWith('>')) {
            receiveBuffer_.remove(0, 1);
            if (!otaPromptHandled_) { otaPromptHandled_ = true; operation_ = OtaReceiving; writeSerial(otaRequest_); timeoutTimer_->start(30000); }
            continue;
        }
        if (operation_ == HttpWaitingPrompt && receiveBuffer_.startsWith('>')) {
            receiveBuffer_.remove(0, 1);
            operation_ = HttpSending;
            writeSerial(telemetryRequest_);
            timeoutTimer_->start(5000);
            continue;
        }
        if (operation_ == OtaReceiving && receiveBuffer_.startsWith("+IPD,")) {
            const int colon = receiveBuffer_.indexOf(':');
            if (colon < 0) break;
            const QByteArray header = receiveBuffer_.left(colon);
            const int comma = header.lastIndexOf(',');
            bool ok = false; const int length = header.mid(comma + 1).toInt(&ok);
            if (!ok || length < 0) { finishWithError(QStringLiteral("ESP8266 +IPD 长度解析失败")); return; }
            if (receiveBuffer_.size() < colon + 1 + length) break;
            const QByteArray payload = receiveBuffer_.mid(colon + 1, length);
            receiveBuffer_.remove(0, colon + 1 + length);
            processIpdPayload(payload);
            continue;
        }
        const int end = receiveBuffer_.indexOf('\n');
        if (end < 0) break;
        const QByteArray line = receiveBuffer_.left(end).trimmed();
        receiveBuffer_.remove(0, end + 1);
        if (!line.isEmpty()) processLine(line);
    }
}

void Esp8266Controller::processIpdPayload(const QByteArray &payload)
{
    processHttpData(payload);
}

void Esp8266Controller::processHttpData(const QByteArray &data)
{
    if (!otaHeadersParsed_) {
        otaHttpHeaders_.append(data);
        const int separator = otaHttpHeaders_.indexOf("\r\n\r\n");
        if (separator < 0) return;
        const QByteArray body = otaHttpHeaders_.mid(separator + 4);
        const QByteArray headers = otaHttpHeaders_.left(separator);
        otaHttpHeaders_ = headers;
        otaHeadersParsed_ = true;
        const QList<QByteArray> lines = headers.split('\n');
        if (lines.isEmpty() || !lines.first().contains(" 200 ")) { finishWithError(QStringLiteral("OTA HTTP 响应失败: %1").arg(QString::fromLatin1(lines.value(0)))); return; }
        QRegularExpression expression(QStringLiteral("(?im)^Content-Length:\\s*(\\d+)") );
        const QRegularExpressionMatch match = expression.match(QString::fromLatin1(headers));
        if (!match.hasMatch()) { finishWithError(QStringLiteral("OTA 响应缺少 Content-Length")); return; }
        otaExpectedBytes_ = match.captured(1).toLongLong();
        if (otaDownloadingFile_) {
            delete otaFile_; otaFile_ = new QFile(QStringLiteral("/tmp/environment_monitor.new"));
            if (!otaFile_->open(QIODevice::WriteOnly | QIODevice::Truncate)) { finishWithError(QStringLiteral("无法创建 OTA 临时文件")); return; }
        }
        if (!body.isEmpty()) processHttpData(body);
        return;
    }
    if (data.isEmpty()) return;
    if (otaDownloadingFile_) {
        if (!otaFile_ || otaFile_->write(data) != data.size()) { finishWithError(QStringLiteral("写入 OTA 临时文件失败")); return; }
    } else otaHttpBody_.append(data);
    otaReceivedBytes_ += data.size();
    emit otaProgress(otaReceivedBytes_, otaExpectedBytes_);
    if (otaReceivedBytes_ >= otaExpectedBytes_) finishHttpResponse();
}

void Esp8266Controller::finishHttpResponse()
{
    timeoutTimer_->stop();
    if (otaDownloadingFile_) {
        if (otaFile_) { otaFile_->flush(); otaFile_->close(); delete otaFile_; otaFile_ = nullptr; }
        QFile file(QStringLiteral("/tmp/environment_monitor.new"));
        if (!file.open(QIODevice::ReadOnly)) { finishWithError(QStringLiteral("无法读取 OTA 临时文件")); return; }
        if (file.size() != otaExpectedFileBytes_) { file.close(); QFile::remove(file.fileName()); finishWithError(QStringLiteral("OTA 文件大小校验失败")); return; }
        const QString hash = QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
        if (hash.compare(otaFileSha256_, Qt::CaseInsensitive) != 0) { file.close(); QFile::remove(file.fileName()); finishWithError(QStringLiteral("OTA SHA256 校验失败")); return; }
        file.close();
        writeSerial(QByteArrayLiteral("AT+CIPCLOSE\r\n")); operation_ = Idle;
        emit otaPackageReady(otaVersion_, file.fileName());
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(otaHttpBody_);
    const QJsonObject manifest = document.object();
    otaVersion_ = manifest.value(QStringLiteral("version")).toString();
    otaFilePath_ = manifest.value(QStringLiteral("path")).toString();
    otaFileSha256_ = manifest.value(QStringLiteral("sha256")).toString().trimmed();
    const qint64 size = static_cast<qint64>(manifest.value(QStringLiteral("size")).toDouble());
    if (!otaFilePath_.startsWith(QLatin1Char('/')) || otaFilePath_.contains(QStringLiteral("..")) ||
        otaVersion_.isEmpty() || otaFileSha256_.size() != 64 || size <= 0) {
        finishWithError(QStringLiteral("OTA manifest.json 内容不完整或路径不安全")); return;
    }
    otaExpectedFileBytes_ = size;
    otaDownloadingFile_ = true; otaExpectedBytes_ = 0; otaReceivedBytes_ = 0;
    writeSerial(QByteArrayLiteral("AT+CIPCLOSE\r\n")); operation_ = Idle;
    QTimer::singleShot(200, this, &Esp8266Controller::beginOtaConnection);
}

void Esp8266Controller::processLine(const QByteArray &line)
{
    const QString text = QString::fromUtf8(line);
    if (text.startsWith(QStringLiteral("+CWLAP:"))) {
        const QRegularExpression expression(QStringLiteral("^\\+CWLAP:\\((\\d+),\"((?:\\\\.|[^\"])*)\",(-?\\d+)"));
        const QRegularExpressionMatch match = expression.match(text);
        if (match.hasMatch()) {
            WifiNetwork network{match.captured(2), match.captured(3).toInt(), match.captured(1).toInt()};
            auto it = std::find_if(networks_.begin(), networks_.end(), [&network](const WifiNetwork &value) { return value.ssid == network.ssid; });
            if (it == networks_.end()) networks_.append(network); else if (network.rssi > it->rssi) *it = network;
        }
        return;
    }
    if (text == QStringLiteral("ERROR") && operation_ == WaitingForScanMode) {
        operation_ = Scanning;
        sendCommand(QByteArrayLiteral("AT+CWLAP\r\n"), 15000);
        return;
    }
    if (operation_ == OtaSettingMode && (text == QStringLiteral("OK") || text == QStringLiteral("ERROR"))) {
        operation_ = OtaClosing;
        sendCommand(QByteArrayLiteral("AT+CIPCLOSE\r\n"), 2000);
        return;
    }
    if (operation_ == OtaClosing && (text == QStringLiteral("OK") || text == QStringLiteral("ERROR"))) {
        beginOtaTcpConnection();
        return;
    }
    if (text == QStringLiteral("ERROR") || text == QStringLiteral("FAIL") || text.startsWith(QStringLiteral("+CWJAP:"))) { finishWithError(QStringLiteral("ESP8266 返回: %1").arg(text)); return; }
    if (operation_ == WaitingForScanMode && text == QStringLiteral("OK")) { operation_ = Scanning; sendCommand(QByteArrayLiteral("AT+CWLAP\r\n"), 15000); }
    else if (operation_ == Scanning && text == QStringLiteral("OK")) { timeoutTimer_->stop(); std::sort(networks_.begin(), networks_.end(), [](const WifiNetwork &a, const WifiNetwork &b) { return a.rssi > b.rssi; }); operation_ = Idle; emit scanFinished(networks_); }
    else if (operation_ == Connecting && text == QStringLiteral("OK")) { operation_ = QueryingIp; sendCommand(QByteArrayLiteral("AT+CIFSR\r\n"), 3000); }
    else if (operation_ == QueryingIp && text.startsWith(QStringLiteral("+CIFSR:STAIP"))) { timeoutTimer_->stop(); operation_ = Idle; emit connectionStateChanged(true, text); }
    else if (operation_ == QueryingIp && text == QStringLiteral("OK")) { timeoutTimer_->stop(); operation_ = Idle; }
    else if (operation_ == QueryingStatus && text.startsWith(QStringLiteral("+CIFSR:STAIP"))) {
        timeoutTimer_->stop(); operation_ = Idle;
        const bool connected = !text.contains(QStringLiteral("\"0.0.0.0\""));
        emit connectionStateChanged(connected, connected ? text : QStringLiteral("WiFi 未连接"));
    }
    else if (operation_ == QueryingStatus && text.startsWith(QStringLiteral("STATUS:"))) {
        const int status = text.mid(QStringLiteral("STATUS:").size()).trimmed().toInt();
        if (status == 3 || status == 4) {
            timeoutTimer_->stop();
            operation_ = Idle;
            emit connectionStateChanged(status == 3,
                                        status == 3 ? QStringLiteral("ESP8266 TCP 已连接")
                                                    : QStringLiteral("ESP8266 WiFi 未连接"));
        }
    }
    else if (operation_ == QueryingStatus && text == QStringLiteral("OK")) { timeoutTimer_->stop(); operation_ = Idle; }
    else if (operation_ == HttpConnecting && (text == QStringLiteral("CONNECT") || text == QStringLiteral("Linked") || text == QStringLiteral("ALREADY CONNECTED") || text == QStringLiteral("OK"))) {
        operation_ = HttpWaitingPrompt;
        sendCommand(QStringLiteral("AT+CIPSEND=%1\r\n").arg(telemetryRequest_.size()).toUtf8(), 5000);
    }
    else if (operation_ == OtaConnecting && (text == QStringLiteral("CONNECT") || text == QStringLiteral("Linked") || text == QStringLiteral("ALREADY CONNECTED") || text == QStringLiteral("OK"))) { if (!otaPromptHandled_) sendOtaRequest(); }
    else if (operation_ == OtaWaitingPrompt && text == QStringLiteral(">")) { if (!otaPromptHandled_) { otaPromptHandled_ = true; operation_ = OtaReceiving; writeSerial(otaRequest_); timeoutTimer_->start(30000); } }
    else if (text == QStringLiteral("WIFI DISCONNECT")) emit connectionStateChanged(false, QStringLiteral("WiFi 已断开"));
}

void Esp8266Controller::timeout()
{
    if (operation_ == HttpConnecting || operation_ == HttpWaitingPrompt || operation_ == HttpSending) {
        operation_ = Idle;
        return;
    }
    if (operation_ == QueryingStatus) {
        operation_ = Idle;
        emit connectionStateChanged(false, QStringLiteral("WiFi 状态查询超时"));
        return;
    }
    finishWithError(QStringLiteral("ESP8266 响应超时, 请检查 WiFi, 服务器地址和串口连接"));
}
void Esp8266Controller::finishWithError(const QString &message)
{
    timeoutTimer_->stop();
    if (otaFile_) { otaFile_->close(); delete otaFile_; otaFile_ = nullptr; }
    if (otaDownloadingFile_) QFile::remove(QStringLiteral("/tmp/environment_monitor.new"));
    operation_ = Idle; emit operationFailed(message);
}
QString Esp8266Controller::escapeArgument(const QString &value)
{
    QString escaped = value; escaped.replace(QStringLiteral("\\"), QStringLiteral("\\\\")); escaped.replace(QStringLiteral("\""), QStringLiteral("\\\"")); return escaped;
}
