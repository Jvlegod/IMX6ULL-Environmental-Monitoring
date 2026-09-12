#ifndef WIFIDIALOG_H
#define WIFIDIALOG_H

#include "esp8266controller.h"
#include <QDialog>
#include <QEvent>
#include <QElapsedTimer>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QProgressBar;
class QTableWidget;
class QWidget;
class QKeyEvent;
class QResizeEvent;
class QShowEvent;

class WifiDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit WifiDialog(QWidget *parent = nullptr);
    ~WifiDialog() override;
    void publishTelemetry(const SensorSnapshot &snapshot);
    Esp8266Controller *controller() { return &controller_; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

signals:
    void wifiStateChanged(bool connected, const QString &detail);
    void otaStatusChanged(int progress, const QString &detail);

private slots:
    void scanNetworks();
    void connectNetwork();
    void showScanResults(const QVector<WifiNetwork> &networks);
    void showPortState(bool open, const QString &detail);
    void showConnectionState(bool connected, const QString &detail);
    void showError(const QString &message);
    void startOta();
    void showOtaProgress(qint64 received, qint64 total);
    void applyOtaPackage(const QString &version, const QString &path);

private:
    void setBusy(bool busy);
    void loadSettings();
    void saveSettings() const;
    void showKeyboard(QLineEdit *edit);
    void hideKeyboard();

    Esp8266Controller controller_;
    QComboBox *portCombo_;
    QPushButton *scanButton_;
    QLineEdit *ssidEdit_;
    QLineEdit *passwordEdit_;
    QPushButton *connectButton_;
    QLineEdit *otaHostEdit_;
    QLineEdit *otaPortEdit_;
    QLineEdit *otaManifestEdit_;
    QPushButton *otaButton_;
    QProgressBar *otaProgress_;
    QTableWidget *networkTable_;
    QLabel *statusLabel_;
    QWidget *keyboardPanel_;
    QLineEdit *keyboardEdit_;
    bool closeAllowed_;
    QElapsedTimer keyboardTimer_;
};

#endif
