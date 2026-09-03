#include "esp8266controller.h"
#include <QDir>
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
    : QObject(parent), fd_(-1), notifier_(nullptr), timeoutTimer_(new QTimer(this)),
      operation_(Idle), simulated_(false)
{
    timeoutTimer_->setSingleShot(true);
    connect(timeoutTimer_, &QTimer::timeout, this, &Esp8266Controller::timeout);
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
    emit portStateChanged(true, QStringLiteral("%1 · %2 8N1").arg(path).arg(baudRate));
    sendCommand(QByteArrayLiteral("AT\r\n"), 2000);
    return true;
}

void Esp8266Controller::closePort()
{
    timeoutTimer_->stop(); delete notifier_; notifier_ = nullptr;
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1; simulated_ = false; operation_ = Idle; receiveBuffer_.clear();
}
bool Esp8266Controller::isOpen() const { return simulated_ || fd_ >= 0; }

void Esp8266Controller::scanNetworks()
{
    if (!isOpen()) { emit operationFailed(QStringLiteral("请先打开串口")); return; }
    networks_.clear();
    if (simulated_) { QTimer::singleShot(500, this, [this] { networks_ = {{QStringLiteral("Office-WiFi"), -38, 3}, {QStringLiteral("Lab-2.4G"), -56, 3}, {QStringLiteral("Guest"), -72, 0}}; emit scanFinished(networks_); }); return; }
    operation_ = WaitingForScanMode; sendCommand(QByteArrayLiteral("AT+CWMODE=1\r\n"), 3000);
}

void Esp8266Controller::connectNetwork(const QString &ssid, const QString &password)
{
    if (!isOpen()) { emit operationFailed(QStringLiteral("请先打开串口")); return; }
    if (simulated_) { QTimer::singleShot(900, this, [this, ssid] { emit connectionStateChanged(true, QStringLiteral("已连接 %1 · 192.168.1.108").arg(ssid)); }); return; }
    operation_ = Connecting;
    const QString command = QStringLiteral("AT+CWJAP=\"%1\",\"%2\"\r\n").arg(escapeArgument(ssid), escapeArgument(password));
    sendCommand(command.toUtf8(), 25000);
}

void Esp8266Controller::sendCommand(const QByteArray &command, int timeoutMs)
{
    if (fd_ >= 0) ::write(fd_, command.constData(), static_cast<size_t>(command.size()));
    timeoutTimer_->start(timeoutMs);
}

void Esp8266Controller::readAvailable()
{
    char data[512]; ssize_t size;
    while ((size = ::read(fd_, data, sizeof(data))) > 0) receiveBuffer_.append(data, static_cast<int>(size));
    int end;
    while ((end = receiveBuffer_.indexOf('\n')) >= 0) { const QByteArray line = receiveBuffer_.left(end).trimmed(); receiveBuffer_.remove(0, end + 1); if (!line.isEmpty()) processLine(line); }
}

void Esp8266Controller::processLine(const QByteArray &line)
{
    const QString text = QString::fromUtf8(line);
    if (text.startsWith(QStringLiteral("+CWLAP:"))) {
        const int q1 = text.indexOf('"'); const int q2 = text.indexOf('"', q1 + 1);
        const QStringList fields = text.mid(q2 + 2).split(',');
        if (q1 >= 0 && q2 > q1 && !fields.isEmpty()) { WifiNetwork n{text.mid(q1 + 1, q2 - q1 - 1), fields.first().toInt(), text.mid(8, q1 - 9).toInt()}; auto it = std::find_if(networks_.begin(), networks_.end(), [&n](const WifiNetwork &v) { return v.ssid == n.ssid; }); if (it == networks_.end()) networks_.append(n); else if (n.rssi > it->rssi) *it = n; }
        return;
    }
    if (text == QStringLiteral("ERROR") || text == QStringLiteral("FAIL") || text.startsWith(QStringLiteral("+CWJAP:"))) { finishWithError(QStringLiteral("ESP8266 返回: %1").arg(text)); return; }
    if (operation_ == WaitingForScanMode && text == QStringLiteral("OK")) { operation_ = Scanning; sendCommand(QByteArrayLiteral("AT+CWLAP\r\n"), 15000); }
    else if (operation_ == Scanning && text == QStringLiteral("OK")) { timeoutTimer_->stop(); std::sort(networks_.begin(), networks_.end(), [](const WifiNetwork &a, const WifiNetwork &b) { return a.rssi > b.rssi; }); operation_ = Idle; emit scanFinished(networks_); }
    else if (operation_ == Connecting && (text == QStringLiteral("WIFI GOT IP") || text == QStringLiteral("OK"))) { operation_ = QueryingIp; sendCommand(QByteArrayLiteral("AT+CIFSR\r\n"), 3000); }
    else if (operation_ == QueryingIp && text.startsWith(QStringLiteral("+CIFSR:STAIP"))) emit connectionStateChanged(true, text);
    else if (operation_ == QueryingIp && text == QStringLiteral("OK")) { timeoutTimer_->stop(); operation_ = Idle; }
}

void Esp8266Controller::timeout() { finishWithError(QStringLiteral("ESP8266 响应超时, 请检查接线和波特率")); }
void Esp8266Controller::finishWithError(const QString &message) { timeoutTimer_->stop(); operation_ = Idle; emit operationFailed(message); }
QString Esp8266Controller::escapeArgument(const QString &value)
{
    QString escaped = value; escaped.replace(QStringLiteral("\\"), QStringLiteral("\\\\")); escaped.replace(QStringLiteral("\""), QStringLiteral("\\\"")); return escaped;
}
