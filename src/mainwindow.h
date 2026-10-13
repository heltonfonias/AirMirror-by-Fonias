#pragma once

#include <string>

#include <QLabel>
#include <QMainWindow>
#include <QComboBox>
#include <QPushButton>
#include <QSystemTrayIcon>
#include <QProcess>
#include <QCheckBox>
#include <QMessageBox>
#include <QPointer>

class QMenu;
class QAction;
class AirPlayWorker;
class LogViewer;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    static constexpr int RestartExitCode = 100;

    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void openSettingsDialog();
    void startReceiver();
    void stopReceiver();
    void showFromTray();
    void showLicense();
    void openSettingsFile();
    void openListArgsFile();
    void openCurrentLog();
    void quit();
    void onAirplayStarted();
    void onAirplayStopped();
    void onAirplayError(const QString &message);


private:
    void migrateLegacySettings();
    void startServer();
    void stopServer();
    void setupTray();
    void setupUI();
    void updateStatus();
    void applyRendererAndFullscreenArgs(QStringList &args);

    QStringList getArgumentsFromFile();
    void ensureSettingsFileExists();
    QString userArgumentsPath() const;
    QString machineArgumentsPath() const;
    QString activeArgumentsPath() const;
    QString expandEnvironmentVariables(const QString &content) const;
    bool ensureBonjourServiceInstalled();
    bool isWindowsServicePresent(const std::wstring &serviceName) const;
    void restartApplication();

    bool isAutostartEnabled() const;
    void setAutostart(bool enabled);

    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_trayMenu = nullptr;
    QAction *m_startAction = nullptr;
    QAction *m_stopAction = nullptr;

    QPushButton *m_settingsBtn = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_detailLabel = nullptr;
    QLabel *m_connectLabel = nullptr;
    QLabel *m_networkLabel = nullptr;
    QPushButton *m_startButton = nullptr;
    QPushButton *m_stopButton = nullptr;

    QPointer<AirPlayWorker> m_worker;
    QPointer<LogViewer> m_logViewer;

    bool m_running = false;
    bool m_quitting = false;
    bool m_serverEnabled = true;
};
