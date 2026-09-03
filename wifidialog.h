#ifndef WIFIDIALOG_H
#define WIFIDIALOG_H

#include "esp8266controller.h"
#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

class WifiDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit WifiDialog(QWidget *parent = nullptr);

signals:
    void wifiStateChanged(bool connected, const QString &detail);

private slots:
    void scanNetworks();
    void connectNetwork();
    void showScanResults(const QVector<WifiNetwork> &networks);
    void showPortState(bool open, const QString &detail);
    void showConnectionState(bool connected, const QString &detail);
    void showError(const QString &message);

private:
    void setBusy(bool busy);

    Esp8266Controller controller_;
    QComboBox *portCombo_;
    QPushButton *scanButton_;
    QLineEdit *ssidEdit_;
    QLineEdit *passwordEdit_;
    QPushButton *connectButton_;
    QTableWidget *networkTable_;
    QLabel *statusLabel_;
};

#endif
