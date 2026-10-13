#include "mainwindow.h"
#include "airplayworker.h"
#include "logviewer.h"
#include "mdns_responder.hpp"
#include "windowappearance.h"
#include <windows.h>
#include <winsvc.h>
#include <io.h>

#include <QProcess>
#include <QProgressDialog>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QPushButton>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QSaveFile>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QComboBox>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QTextStream>

struct RenameData {
    DWORD pid;
    QString newTitle;
};

// Callback method that Windows calls for every opened window
BOOL CALLBACK EnumWindowsProcRename(HWND hwnd, LPARAM lParam) {
    RenameData *data = reinterpret_cast<RenameData*>(lParam);
    DWORD windowPid;
    GetWindowThreadProcessId(hwnd, &windowPid);

    if (windowPid == data->pid) {
        char windowTitle[512];
        if (GetWindowTextA(hwnd, windowTitle, sizeof(windowTitle))) {
            QString title = QString::fromLocal8Bit(windowTitle);
            
            if (title.contains("Direct") && title.contains("enderer")) {
                qInfo("Found renderer window; updating its title.");
                SetWindowTextW(hwnd, reinterpret_cast<const wchar_t*>(data->newTitle.utf16()));
                return FALSE;
            }
        }
    }
    return TRUE;
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    migrateLegacySettings();
    ensureSettingsFileExists();
    setupTray();
    setupUI();
    m_updateManager = new UpdateManager(this);
    connect(m_updateManager, &UpdateManager::updateFound, this,
            [this](const UpdateInfo &update) {
        m_availableUpdate = update;
        m_hasUpdate = true;
        m_updateButton->setText("Update available");
        m_updateButton->setEnabled(true);
        if (m_manualUpdateCheck) showUpdatePrompt();
        else m_tray->showMessage("AirMirror update", "Version " + update.version +
                                 " is ready. Open AirMirror to install it.",
                                 QSystemTrayIcon::Information, 7000);
        m_manualUpdateCheck = false;
    });
    connect(m_updateManager, &UpdateManager::upToDate, this, [this]() {
        m_updateButton->setText("Updates");
        m_updateButton->setEnabled(true);
        if (m_manualUpdateCheck)
            QMessageBox::information(this, "AirMirror updates",
                                     "You have the latest version (" +
                                     QCoreApplication::applicationVersion() + ").");
        m_manualUpdateCheck = false;
    });
    connect(m_updateManager, &UpdateManager::checkFailed, this,
            [this](const QString &message) {
        m_updateButton->setText("Updates");
        m_updateButton->setEnabled(true);
        if (m_manualUpdateCheck)
            QMessageBox::warning(this, "Update check failed", message);
        else qWarning().noquote() << "Update check failed:" << message;
        m_manualUpdateCheck = false;
    });
    QTimer::singleShot(8000, m_updateManager, &UpdateManager::check);
    auto *updateTimer = new QTimer(this);
    updateTimer->setInterval(6 * 60 * 60 * 1000);
    connect(updateTimer, &QTimer::timeout, m_updateManager, &UpdateManager::check);
    updateTimer->start();

    // Bonjour improves device discovery, but the server can also run without it.
    if (ensureBonjourServiceInstalled()) {
        startServer();
    } else {
        if (!m_quitting) {
            m_quitting = true;
            QTimer::singleShot(0, qApp, &QCoreApplication::quit);
        }
        return;
    }
}

MainWindow::~MainWindow() {
    m_quitting = true;
    stopServer();
}

QString MainWindow::userArgumentsPath() const {
    QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return appDataPath + "/arguments.txt";
}

void MainWindow::migrateLegacySettings() {
    QSettings settings;
    QSettings legacy("HKEY_CURRENT_USER\\Software\\leapbtw\\uxplay-windows",
                     QSettings::NativeFormat);
    for (const QString &key : {"force_fs_enabled", "renderer_mode"}) {
        if (!settings.contains(key) && legacy.contains(key)) {
            settings.setValue(key, legacy.value(key));
        }
    }

    const QString oldPath = QProcessEnvironment::systemEnvironment()
                                .value("APPDATA") + "/leapbtw/uxplay-windows/arguments.txt";
    const QString newPath = userArgumentsPath();
    if (!QFile::exists(newPath) && QFile::exists(oldPath)) {
        QDir().mkpath(QFileInfo(newPath).absolutePath());
        QFile::copy(oldPath, newPath);
    }
}

QString MainWindow::machineArgumentsPath() const {
    QString programDataPath =
        QProcessEnvironment::systemEnvironment().value("ProgramData");
    if (programDataPath.isEmpty()) {
        programDataPath = "C:/ProgramData";
    }
    return programDataPath + "/uxplay-windows/arguments.txt";
}

QString MainWindow::activeArgumentsPath() const {
    QString machinePath = machineArgumentsPath();
    if (QFile::exists(machinePath)) {
        return machinePath;
    }

    return userArgumentsPath();
}

QString MainWindow::expandEnvironmentVariables(const QString &content) const {
    QString expanded = content;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    for (const QString &key : env.keys()) {
        expanded.replace("%" + key + "%", env.value(key), Qt::CaseInsensitive);
    }

    return expanded;
}

void MainWindow::ensureSettingsFileExists() {
    if (QFile::exists(userArgumentsPath()) || QFile::exists(machineArgumentsPath())) {
        return;
    }

    QFileInfo info(userArgumentsPath());
    QDir().mkpath(info.absolutePath());

    QFile file(userArgumentsPath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << "-n AirMirror-by-Fonias -nh";
        file.close();
    }
}

QStringList MainWindow::getArgumentsFromFile() {
    const QString userPath = userArgumentsPath();
    const QString machinePath = machineArgumentsPath();
    const QString selectedPath = activeArgumentsPath();

    qInfo().noquote() << "[arguments] Machine file:"
                      << QDir::toNativeSeparators(machinePath)
                      << (QFile::exists(machinePath) ? "(found)" : "(not found)");
    qInfo().noquote() << "[arguments] User file:"
                      << QDir::toNativeSeparators(userPath)
                      << (QFile::exists(userPath) ? "(found)" : "(not found)");
    qInfo().noquote() << "[arguments] Reading:"
                      << QDir::toNativeSeparators(selectedPath);

    QFile file(selectedPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString content = expandEnvironmentVariables(QTextStream(&file).readAll().trimmed());
        file.close();
        return QProcess::splitCommand(content);
    }

    qWarning().noquote()
        << "[arguments] Unable to read the selected file; using built-in defaults.";
    return QStringList() << "-n" << "AirMirror-by-Fonias" << "-nh";
}

void MainWindow::setupUI() {
    setWindowTitle("AirMirror by Fonias");
    setWindowIcon(QApplication::windowIcon());
    resize(700, 750);
    setMinimumSize(620, 690);
    setStyleSheet(R"(
        QMainWindow, QWidget#Root { background: #101512; color: #F4F0E8; }
        QWidget#Card { background: #1C241F; border-radius: 17px; }
        QLabel { color: #F4F0E8; background: transparent; }
        QLabel#Eyebrow { color: #F28D73; font-size: 11px; font-weight: 700; }
        QLabel#Muted { color: #B8C5BA; }
        QPushButton { background: #303A33; color: #F4F0E8; border: 0;
                      border-radius: 8px; padding: 11px 17px; font-weight: 600; }
        QPushButton:hover { background: #3E4A41; }
        QPushButton#Primary { background: #F28D73; color: #17201A; }
        QPushButton#Primary:hover { background: #FFA088; }
        QPushButton#Primary:disabled { background: #2B332E; color: #89958A; }
        QPushButton:disabled { color: #89958A; background: #2B332E; }
    )");

    auto *central = new QWidget(this);
    central->setObjectName("Root");
    setCentralWidget(central);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(31, 25, 31, 22);
    layout->setSpacing(14);

    auto *header = new QHBoxLayout();
    auto *logo = new QLabel(central);
    logo->setPixmap(QPixmap(QApplication::applicationDirPath() + "/resources/icon.png")
                        .scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    header->addWidget(logo);
    auto *wordmark = new QVBoxLayout();
    wordmark->setSpacing(0);
    auto *title = new QLabel("AirMirror", central);
    title->setStyleSheet("font-size: 29px; font-weight: 600;");
    wordmark->addWidget(title);
    auto *byline = new QLabel("BY FONIAS", central);
    byline->setObjectName("Eyebrow");
    wordmark->addWidget(byline);
    header->addLayout(wordmark);
    header->addStretch();
    auto *badge = new QLabel("IPHONE  →  WINDOWS", central);
    badge->setStyleSheet("background:#263129;color:#C7D2C9;border-radius:12px;padding:8px 11px;font-size:10px;");
    header->addWidget(badge);
    layout->addLayout(header);
    layout->addSpacing(8);

    auto makeCard = [central, layout]() -> QVBoxLayout * {
        auto *card = new QWidget(central);
        card->setObjectName("Card");
        auto *body = new QVBoxLayout(card);
        body->setContentsMargins(24, 20, 24, 20);
        body->setSpacing(8);
        layout->addWidget(card);
        return body;
    };
    auto makeEyebrow = [central](const QString &text) -> QLabel * {
        auto *label = new QLabel(text, central);
        label->setObjectName("Eyebrow");
        return label;
    };

    auto *receiverCard = makeCard();
    receiverCard->addWidget(makeEyebrow("RECEIVER STATUS"));
    m_statusLabel = new QLabel("Starting receiver...", central);
    m_statusLabel->setStyleSheet("font-size: 24px; font-weight: 600;");
    receiverCard->addWidget(m_statusLabel);
    m_detailLabel = new QLabel(central);
    m_detailLabel->setObjectName("Muted");
    m_detailLabel->setWordWrap(true);
    receiverCard->addWidget(m_detailLabel);
    auto *actions = new QHBoxLayout();
    actions->setSpacing(8);
    m_startButton = new QPushButton("Start receiving", central);
    m_startButton->setObjectName("Primary");
    m_stopButton = new QPushButton("Stop", central);
    m_settingsBtn = new QPushButton("Settings", central);
    auto *logButton = new QPushButton("Log", central);
    m_updateButton = new QPushButton("Updates", central);
    connect(m_startButton, &QPushButton::clicked, this, &MainWindow::startReceiver);
    connect(m_stopButton, &QPushButton::clicked, this, &MainWindow::stopReceiver);
    connect(m_settingsBtn, &QPushButton::clicked, this, &MainWindow::openSettingsDialog);
    connect(logButton, &QPushButton::clicked, this, &MainWindow::openCurrentLog);
    connect(m_updateButton, &QPushButton::clicked, this, &MainWindow::checkForUpdates);
    actions->addWidget(m_startButton);
    actions->addWidget(m_stopButton);
    actions->addWidget(m_settingsBtn);
    actions->addWidget(logButton);
    actions->addWidget(m_updateButton);
    actions->addStretch();
    receiverCard->addSpacing(8);
    receiverCard->addLayout(actions);

    auto *networkCard = makeCard();
    networkCard->addWidget(makeEyebrow("LOCAL NETWORK"));
    m_networkLabel = new QLabel("Checking local network...", central);
    m_networkLabel->setStyleSheet("font-size:14px;font-weight:600;");
    for (const QHostAddress &address : QNetworkInterface::allAddresses()) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol &&
            !address.isLoopback() && !address.isLinkLocal()) {
            m_networkLabel->setText(address.toString());
            break;
        }
    }
    networkCard->addWidget(m_networkLabel);
    auto *networkHint = new QLabel("Connect your iPhone and PC to the same Wi-Fi network.", central);
    networkHint->setObjectName("Muted");
    networkCard->addWidget(networkHint);

    auto *connectCard = makeCard();
    connectCard->addWidget(makeEyebrow("CONNECT YOUR IPHONE"));
    connectCard->addWidget(new QLabel("01   Open Control Center.", central));
    connectCard->addWidget(new QLabel("02   Tap Screen Mirroring.", central));
    m_connectLabel = new QLabel(central);
    connectCard->addWidget(m_connectLabel);
    auto *videoHint = new QLabel("The mirrored screen opens in a video window. Press Alt+Enter to toggle fullscreen.", central);
    videoHint->setObjectName("Muted");
    videoHint->setWordWrap(true);
    connectCard->addSpacing(6);
    connectCard->addWidget(videoHint);
    auto *footer = new QLabel("POWERED BY UXPLAY  ·  GPL-3.0  ·  RECEIVER RUNS ON THIS PC", central);
    footer->setStyleSheet("color:#87998A;font-size:10px;");
    layout->addWidget(footer);
    layout->addStretch();
    updateStatus();
}

void MainWindow::openSettingsFile() {
    QString filePath = activeArgumentsPath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
}

void MainWindow::openSettingsDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle("AirMirror settings");
    dialog.setWindowIcon(windowIcon());
    dialog.resize(680, 790);
    dialog.setMinimumSize(610, 650);
    dialog.setStyleSheet(R"(
        QDialog, QScrollArea, QWidget#SettingsContent { background:#101512;color:#F4F0E8; }
        QGroupBox { background:#1C241F;border:0;border-radius:14px;margin-top:10px;
                    padding:18px;color:#F28D73;font-size:11px;font-weight:700; }
        QGroupBox::title { subcontrol-origin:margin;left:16px;top:0; }
        QLabel, QCheckBox { color:#F4F0E8;background:transparent;font-size:13px; }
        QLabel#Hint { color:#AFC0B1;font-size:11px; }
        QLineEdit, QComboBox, QSpinBox { background:#29332C;color:#F4F0E8;
            border:1px solid #465249;border-radius:5px;padding:8px;min-height:22px; }
        QComboBox QAbstractItemView { background:#29332C;color:#F4F0E8;selection-background-color:#45584A; }
        QPushButton { background:#303A33;color:#F4F0E8;border:0;border-radius:8px;
                      padding:10px 17px;font-weight:600; }
        QPushButton:hover { background:#3E4A41; }
        QPushButton#Primary { background:#F28D73;color:#17201A; }
        QPushButton:disabled { background:#29312B;color:#89958A; }
    )");
    auto *outer = new QVBoxLayout(&dialog);
    outer->setContentsMargins(27, 22, 27, 18);
    outer->setSpacing(12);
    auto *title = new QLabel("Receiver settings", &dialog);
    title->setStyleSheet("font-size:25px;font-weight:600;");
    outer->addWidget(title);
    auto *subtitle = new QLabel("Make AirMirror feel right on your screen.", &dialog);
    subtitle->setObjectName("Hint");
    outer->addWidget(subtitle);

    auto *scroll = new QScrollArea(&dialog);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scroll);
    content->setObjectName("SettingsContent");
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 8, 0);
    contentLayout->setSpacing(12);
    auto addGroup = [content, contentLayout](const QString &name) -> QFormLayout * {
        auto *group = new QGroupBox(name, content);
        auto *form = new QFormLayout(group);
        form->setContentsMargins(17, 26, 17, 16);
        form->setVerticalSpacing(12);
        form->setHorizontalSpacing(15);
        form->setLabelAlignment(Qt::AlignLeft);
        contentLayout->addWidget(group);
        return form;
    };

    const QStringList args = getArgumentsFromFile();
    QString name = "AirMirror-by-Fonias";
    bool appendHost = true;
    QString resolution = "Default";
    int fps = 30;
    bool hevc = false;
    bool takeover = false;
    QString audio = "Auto";
    QStringList extra;
    for (int i = 0; i < args.size(); ++i) {
        const QString token = args[i];
        if (token == "-n" && i + 1 < args.size()) name = args[++i];
        else if (token == "-nh") appendHost = false;
        else if (token == "-s" && i + 1 < args.size()) resolution = args[++i];
        else if (token == "-fps" && i + 1 < args.size()) fps = args[++i].toInt();
        else if (token == "-h265") hevc = true;
        else if (token == "-nohold") takeover = true;
        else if (token == "-a") audio = "Muted";
        else if (token == "-as" && i + 1 < args.size()) {
            const QString sink = args[++i];
            if (sink == "wasapisink") audio = "WASAPI";
            else if (sink == "directsoundsink") audio = "DirectSound";
            else if (sink == "0") audio = "Muted";
            else extra << "-as" << sink;
        } else extra << token;
    }
    auto quote = [](const QString &value) -> QString {
        if (value.isEmpty() || value.contains(QRegularExpression("\\s")))
            return '"' + value + '"';
        return value;
    };
    QStringList quotedExtra;
    for (const QString &value : extra) quotedExtra << quote(value);

    QSettings settings;
    auto *display = addGroup("DISPLAY");
    auto *renderer = new QComboBox(content);
    renderer->addItem("Auto", "auto");
    renderer->addItem("D3D11", "d3d11");
    renderer->addItem("D3D12", "d3d12");
    renderer->addItem("Audio only", "audio-only");
    renderer->setCurrentIndex(qMax(0, renderer->findData(settings.value("renderer_mode", "auto"))));
    display->addRow("Video renderer", renderer);
    auto *fullscreen = new QCheckBox("Open the mirrored screen fullscreen", content);
    fullscreen->setChecked(settings.value("force_fs_enabled", false).toBool());
    display->addRow(fullscreen);
    auto *displayHint = new QLabel("Preserves the iPhone's shape. D3D11 or D3D12 is required; press Alt+Enter in the video window to toggle.", content);
    displayHint->setObjectName("Hint");
    displayHint->setWordWrap(true);
    display->addRow(displayHint);

    auto *identity = addGroup("AIRPLAY IDENTITY");
    auto *receiverName = new QLineEdit(name, content);
    receiverName->setMaxLength(64);
    identity->addRow("Name shown on iPhone", receiverName);
    auto *appendHostBox = new QCheckBox("Append PC hostname to receiver name", content);
    appendHostBox->setChecked(appendHost);
    identity->addRow(appendHostBox);

    auto *picture = addGroup("PICTURE & SOUND");
    auto *resolutionBox = new QComboBox(content);
    resolutionBox->setEditable(true);
    resolutionBox->addItems({"Default", "1920x1080", "2560x1440", "3840x2160"});
    resolutionBox->setCurrentText(resolution);
    picture->addRow("Maximum resolution", resolutionBox);
    auto *resolutionHint = new QLabel("The iPhone may choose a lower resolution. Enable HEVC above 1080p.", content);
    resolutionHint->setObjectName("Hint");
    resolutionHint->setWordWrap(true);
    picture->addRow(resolutionHint);
    auto *fpsBox = new QSpinBox(content);
    fpsBox->setRange(1, 255);
    fpsBox->setValue(qBound(1, fps, 255));
    picture->addRow("Maximum frame rate", fpsBox);
    auto *hevcBox = new QCheckBox("Allow HEVC / H.265 video (up to 4K)", content);
    hevcBox->setChecked(hevc);
    picture->addRow(hevcBox);
    auto *audioBox = new QComboBox(content);
    audioBox->addItems({"Auto", "WASAPI", "DirectSound", "Muted"});
    audioBox->setCurrentText(audio);
    picture->addRow("Audio output", audioBox);

    auto *connection = addGroup("CONNECTION");
    auto *takeoverBox = new QCheckBox("Let a new device replace the current connection", content);
    takeoverBox->setChecked(takeover);
    connection->addRow(takeoverBox);
    auto *autostartBox = new QCheckBox("Open AirMirror when I sign in", content);
    autostartBox->setChecked(isAutostartEnabled());
    connection->addRow(autostartBox);

    auto *advanced = addGroup("ADVANCED");
    auto *extraEdit = new QLineEdit(quotedExtra.join(' '), content);
    advanced->addRow("Additional UxPlay arguments", extraEdit);
    auto *extraHint = new QLabel("Other UxPlay arguments are preserved when you edit the controls above.", content);
    extraHint->setObjectName("Hint");
    extraHint->setWordWrap(true);
    advanced->addRow(extraHint);
    contentLayout->addStretch();
    scroll->setWidget(content);
    outer->addWidget(scroll, 1);

    const bool machineManaged = QFile::exists(machineArgumentsPath());
    auto *pathLabel = new QLabel(QDir::toNativeSeparators(activeArgumentsPath()), &dialog);
    pathLabel->setObjectName("Hint");
    pathLabel->setWordWrap(true);
    outer->addWidget(pathLabel);
    if (machineManaged) {
        auto *warning = new QLabel("A machine-wide arguments file takes priority. Edit it as an administrator to change receiver settings.", &dialog);
        warning->setStyleSheet("color:#F4B68B;font-size:12px;");
        warning->setWordWrap(true);
        outer->addWidget(warning);
    }
    auto *footer = new QHBoxLayout();
    auto *openFile = new QPushButton("Open arguments.txt", &dialog);
    connect(openFile, &QPushButton::clicked, &dialog, [this]() { openSettingsFile(); });
    footer->addWidget(openFile);
    footer->addStretch();
    auto *cancel = new QPushButton("Cancel", &dialog);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    footer->addWidget(cancel);
    auto *save = new QPushButton("Save and apply", &dialog);
    save->setObjectName("Primary");
    save->setEnabled(!machineManaged);
    footer->addWidget(save);
    outer->addLayout(footer);

    connect(save, &QPushButton::clicked, &dialog, [&]() {
        const QString nextName = receiverName->text().trimmed();
        if (nextName.isEmpty() || nextName.contains('"') || nextName.contains('\n')) {
            QMessageBox::warning(&dialog, "Invalid name", "Choose a receiver name without quotes or line breaks.");
            return;
        }
        const QString nextResolution = resolutionBox->currentText().trimmed();
        const QRegularExpression sizePattern("^([1-9][0-9]{0,3})x([1-9][0-9]{0,3})$");
        const auto sizeMatch = sizePattern.match(nextResolution);
        if (nextResolution != "Default" && !sizeMatch.hasMatch()) {
            QMessageBox::warning(&dialog, "Invalid resolution", "Use Default or WIDTHxHEIGHT, such as 2560x1440.");
            return;
        }
        if (sizeMatch.hasMatch() && sizeMatch.captured(2).toInt() > 1080 && !hevcBox->isChecked()) {
            QMessageBox::warning(&dialog, "HEVC required", "Enable HEVC for resolutions above 1080p.");
            return;
        }
        const QString renderMode = renderer->currentData().toString();
        if (fullscreen->isChecked() && renderMode != "d3d11" && renderMode != "d3d12") {
            QMessageBox::warning(&dialog, "Renderer required", "Choose D3D11 or D3D12 for fullscreen mirroring.");
            return;
        }
        const QStringList extras = QProcess::splitCommand(extraEdit->text());
        for (const QString &arg : extras) {
            if (QStringList{"-n", "-nh", "-s", "-fps", "-h265", "-nohold", "-a", "-as", "-ble"}.contains(arg)) {
                QMessageBox::warning(&dialog, "Duplicate option", "An additional argument is already controlled by a setting above: " + arg);
                return;
            }
            if (arg.contains('"') || arg.contains('\n')) {
                QMessageBox::warning(&dialog, "Invalid argument", "Additional arguments cannot contain quotes or line breaks.");
                return;
            }
        }
        QStringList next = {"-n", nextName};
        if (!appendHostBox->isChecked()) next << "-nh";
        if (hevcBox->isChecked()) next << "-h265";
        if (nextResolution != "Default") next << "-s" << nextResolution;
        next << "-fps" << QString::number(fpsBox->value());
        if (audioBox->currentText() == "WASAPI") next << "-as" << "wasapisink";
        else if (audioBox->currentText() == "DirectSound") next << "-as" << "directsoundsink";
        else if (audioBox->currentText() == "Muted") next << "-as" << "0";
        if (takeoverBox->isChecked()) next << "-nohold";
        next << extras;
        QStringList encoded;
        for (const QString &arg : next) encoded << quote(arg);
        const QString path = userArgumentsPath();
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
            QMessageBox::critical(&dialog, "Save failed", "Cannot create the settings folder.");
            return;
        }
        if (QFile::exists(path)) {
            QFile::remove(path + ".bak");
            QFile::copy(path, path + ".bak");
        }
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::critical(&dialog, "Save failed", file.errorString());
            return;
        }
        QTextStream stream(&file);
        stream << encoded.join(' ') << '\n';
        stream.flush();
        if (!file.commit()) {
            QMessageBox::critical(&dialog, "Save failed", file.errorString());
            return;
        }
        settings.setValue("renderer_mode", renderMode);
        settings.setValue("force_fs_enabled", fullscreen->isChecked());
        setAutostart(autostartBox->isChecked());
        dialog.accept();
        if (m_serverEnabled) {
            stopServer();
            startServer();
        }
        updateStatus();
    });
    QTimer::singleShot(0, &dialog, [&dialog]() { useAirMirrorTitleBar(&dialog); });
    dialog.exec();
}

void MainWindow::openListArgsFile() {
    QString filePath = QApplication::applicationDirPath() + "/resources/uxplay_arguments_list.txt";
    QFile::setPermissions(filePath, QFile::ReadOwner | QFile::ReadGroup | QFile::ReadOther);
    QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
}

void MainWindow::openCurrentLog() {
    const QString path = qApp->property("sessionLogPath").toString();
    if (path.isEmpty()) {
        QMessageBox::warning(this, "Logging unavailable", "No session log is available.");
        return;
    }
    if (!m_logViewer) {
        m_logViewer = new LogViewer(path, this);
        m_logViewer->setAttribute(Qt::WA_DeleteOnClose);
    }
    m_logViewer->showNormal();
    m_logViewer->raise();
    m_logViewer->activateWindow();
}

void MainWindow::setupTray() {
    m_tray = new QSystemTrayIcon(this);
    QIcon trayIcon;
    QString icoPath = QApplication::applicationDirPath() + "/resources/icon.ico";
    trayIcon = QIcon(icoPath);
    if (trayIcon.isNull()) {
        trayIcon = QApplication::style()->standardIcon(QStyle::SP_MediaPlay);
    }
    m_tray->setIcon(trayIcon);
    m_tray->setToolTip("AirMirror by Fonias");

    m_trayMenu = new QMenu(this);
    m_trayMenu->addAction("Open AirMirror", this, &MainWindow::showFromTray);
    m_trayMenu->addSeparator();
    m_startAction = m_trayMenu->addAction("Start receiving", this, &MainWindow::startReceiver);
    m_stopAction = m_trayMenu->addAction("Stop receiving", this, &MainWindow::stopReceiver);
    m_trayMenu->addAction("Settings", this, &MainWindow::openSettingsDialog);
    m_trayMenu->addAction("Open current log", this, &MainWindow::openCurrentLog);
    m_trayMenu->addAction("Check for updates", this, &MainWindow::checkForUpdates);
    m_trayMenu->addAction("UxPlay arguments reference", this, &MainWindow::openListArgsFile);
    m_trayMenu->addAction("License information", this, &MainWindow::showLicense);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction("Quit AirMirror", this, &MainWindow::quit);

    m_tray->setContextMenu(m_trayMenu);
    
    connect(m_tray, &QSystemTrayIcon::activated, this, &MainWindow::onTrayActivated);
    m_tray->show();
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger) {
        showFromTray();
    }
}

void MainWindow::showFromTray() {
    showNormal();
    raise();
    activateWindow();
}

void MainWindow::checkForUpdates() {
    if (m_hasUpdate) {
        showUpdatePrompt();
        return;
    }
    m_manualUpdateCheck = true;
    m_updateButton->setText("Checking...");
    m_updateButton->setEnabled(false);
    m_updateManager->check();
}

void MainWindow::showUpdatePrompt() {
    if (!m_hasUpdate || m_updateInProgress) return;
    showFromTray();
    const QString message = QString(
        "AirMirror %1 is available. Download the verified installer and install it now?\n\n"
        "Windows will request administrator approval. Mirroring will stop when installation begins."
    ).arg(m_availableUpdate.version);
    if (QMessageBox::question(this, "AirMirror update", message,
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::Yes) != QMessageBox::Yes) return;

    auto *progress = new QProgressDialog("Downloading AirMirror " + m_availableUpdate.version,
                                         QString(), 0, 100, this);
    progress->setWindowTitle("AirMirror update");
    progress->setCancelButton(nullptr);
    progress->setAutoClose(false);
    progress->setWindowModality(Qt::WindowModal);
    progress->setAttribute(Qt::WA_DeleteOnClose);
    connect(m_updateManager, &UpdateManager::downloadProgress, progress,
            [progress](qint64 received, qint64 total) {
        if (total > 0) progress->setValue(static_cast<int>(received * 100 / total));
    });
    connect(m_updateManager, &UpdateManager::downloadFailed, progress,
            [this, progress](const QString &error) {
        m_updateInProgress = false;
        m_updateButton->setEnabled(true);
        progress->close();
        QMessageBox::warning(this, "Update failed", error);
    });
    connect(m_updateManager, &UpdateManager::installerReady, progress,
            [this, progress](const QString &path) {
        m_updateInProgress = false;
        m_updateButton->setEnabled(true);
        progress->close();
        if (UpdateManager::launchInstaller(path)) quit();
        else QMessageBox::warning(this, "Update not started",
                                  "Windows did not start the installer. You can try again from Updates.");
    });
    progress->show();
    m_updateInProgress = true;
    m_updateButton->setEnabled(false);
    m_updateManager->download(m_availableUpdate);
}

void MainWindow::startReceiver() {
    if (m_serverEnabled && m_running) return;
    m_serverEnabled = true;
    startServer();
    updateStatus();
}

void MainWindow::stopReceiver() {
    m_serverEnabled = false;
    stopServer();
    updateStatus();
}

void MainWindow::applyRendererAndFullscreenArgs(QStringList &args) {
    QSettings settings;
    while (true) {
        int idx = args.indexOf("-fs");
        if (idx < 0) break;
        args.removeAt(idx);
    }
    if (settings.value("force_fs_enabled", false).toBool()) {
        args << "-fs";
    }

    QString mode = settings.value("renderer_mode", "auto").toString();

    if (mode == "auto") {
        return;
    }

    // An explicitly selected GUI renderer overrides any -vs option from the
    // arguments file. In Auto mode, keep the file's -vs option unchanged.
    for (int i = 0; i < args.size();) {
        if (args[i] == "-vs") {
            args.removeAt(i);
            if (i < args.size()) {
                args.removeAt(i);
            }
            continue;
        }
        ++i;
    }

    if (mode == "d3d11") {
        args << "-vs" << "d3d11videosink";
    } else if (mode == "d3d12") {
        args << "-vs" << "d3d12videosink";
    } else if (mode == "audio-only") {
        args << "-vs" << "0";
    }
}

void MainWindow::startServer() {
    if (!m_serverEnabled) return;
    if (m_worker && m_worker->isRunning()) return;

    QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appData);
    
    QStringList args = getArgumentsFromFile();
    
    int bleIdx = args.indexOf("-ble");
    if (bleIdx != -1) {
        args.removeAt(bleIdx);
        if (bleIdx < args.size() && !args[bleIdx].startsWith("-")) {
            args.removeAt(bleIdx);
        }
    }

    applyRendererAndFullscreenArgs(args);

    m_worker = new AirPlayWorker(this);
    m_worker->setArgs(args);

    connect(m_worker, &AirPlayWorker::started, this, &MainWindow::onAirplayStarted);
    connect(m_worker, &AirPlayWorker::stopped, this, &MainWindow::onAirplayStopped);
    connect(m_worker, &AirPlayWorker::errorOccurred, this, &MainWindow::onAirplayError);
    connect(m_worker, &AirPlayWorker::finished, m_worker.data(), &QObject::deleteLater);

    m_worker->start();
}

void MainWindow::stopServer() {
    if (m_worker) {
        m_worker->disconnect();
        if (m_worker->isRunning()) {
            m_worker->requestInterruption();
            m_worker->stopAirplay();
            if (!m_worker->wait(5000)) {
                qWarning("AirPlay worker did not stop gracefully; terminating it.");
                m_worker->terminate();
                m_worker->wait(1000);
            }
        }
        m_worker = nullptr;
    }
    m_running = false;
    updateStatus();
}


void MainWindow::onAirplayStarted() {
    m_running = true;
    updateStatus();

    qInfo("AirPlay server started.");

    // Only explicitly selected Direct3D sinks are known to support Alt+Enter.
    // Leave automatically selected GStreamer sink windows unchanged.
    if (QSettings().value("renderer_mode", "auto").toString() == "auto") {
        return;
    }

    // Rename the video window when it pops up.
    QTimer *monitorTimer = new QTimer(this);
    connect(monitorTimer, &QTimer::timeout, this, [this, monitorTimer]() {
        if (!m_running) {
            monitorTimer->stop();
            monitorTimer->deleteLater();
            return;
        }

        RenameData info;
        info.pid = GetCurrentProcessId();
        info.newTitle = "AirMirror video (Alt+Enter for fullscreen)";

        EnumWindows(EnumWindowsProcRename, reinterpret_cast<LPARAM>(&info));
    });
    monitorTimer->start(5000);

}

void MainWindow::onAirplayStopped() {
    m_running = false;
    updateStatus();
    
    if (!m_quitting && m_serverEnabled) {
        qDebug() << "Session ended, restarting server to stay ready...";
        QTimer::singleShot(1000, this, &MainWindow::startServer);
    }
}

void MainWindow::onAirplayError(const QString &message) {
    qCritical().noquote() << message;
    m_tray->showMessage("AirMirror by Fonias", message, QSystemTrayIcon::Warning, 3000);
}

bool MainWindow::isAutostartEnabled() const {
    QSettings reg("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    return reg.contains("AirMirror by Fonias");
}

void MainWindow::setAutostart(bool enabled) {
    QSettings reg("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    if (enabled) {
        QString path = QDir::toNativeSeparators(QApplication::applicationFilePath());
        reg.setValue("AirMirror by Fonias", "\"" + path + "\"");
    } else {
        reg.remove("AirMirror by Fonias");
    }
}

void MainWindow::showLicense() {
    QString path = QApplication::applicationDirPath() + "/LICENSE.rtf";
    if (QFile::exists(path)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    } else qDebug() << "License file not found:" << path;
}

void MainWindow::quit() {
    m_quitting = true;
    stopServer();
    QApplication::quit();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (!m_quitting) {
        hide();
        event->ignore();
    } else {
        event->accept();
    }
}

void MainWindow::updateStatus() {
    if (!m_statusLabel) return;
    const QStringList args = getArgumentsFromFile();
    QString receiverName = "AirMirror-by-Fonias";
    for (int i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == "-n") { receiverName = args[i + 1]; break; }
    }
    m_statusLabel->setText(m_running ? "Ready for your iPhone" :
                           (m_serverEnabled ? "Starting receiver..." : "Receiver stopped"));
    if (m_detailLabel) {
        m_detailLabel->setText(m_running ?
            "Receiver is running. Select " + receiverName + " in Screen Mirroring." :
            (m_serverEnabled ? "AirMirror is starting its built-in receiver." :
                               "Press Start receiving to make this PC visible."));
    }
    if (m_connectLabel) m_connectLabel->setText("03   Choose " + receiverName + ".");
    if (m_startButton) m_startButton->setEnabled(!m_serverEnabled);
    if (m_stopButton) m_stopButton->setEnabled(m_serverEnabled);
    if (m_startAction) m_startAction->setEnabled(!m_serverEnabled);
    if (m_stopAction) m_stopAction->setEnabled(m_serverEnabled);
}

bool MainWindow::isWindowsServicePresent(const std::wstring& serviceName) const {
    SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hSCM) return false;

    SC_HANDLE hSvc = OpenServiceW(hSCM, serviceName.c_str(), SERVICE_QUERY_STATUS);
    if (!hSvc) {
        DWORD err = GetLastError();
        CloseServiceHandle(hSCM);
        return err != ERROR_SERVICE_DOES_NOT_EXIST;
    }

    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);
    return true;
}

bool MainWindow::ensureBonjourServiceInstalled() {
    const std::wstring serviceName = L"Bonjour Service";

    if (isWindowsServicePresent(serviceName)) {
        return true;
    }

    int choice = QMessageBox::question(
        this,
        "Bonjour Service Not Installed",
        "Bonjour Service is not installed. Installing it is recommended for "
        "reliable AirPlay device discovery (mDNS).\n\n"
        "Do you want to install it now?",
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes
    );

    if (choice != QMessageBox::Yes) {
        QMessageBox::warning(
            this,
            "Continuing Without Bonjour",
            "You can continue without Bonjour, but the following conditions "
            "must be met:\n\n"
            "Both devices must be connected to the same local network.\n"
            "Without Bonjour, your iPhone may be unable to discover this PC."
        );
        return true;
    }

    QMessageBox::information(
        this,
        "Installing Bonjour Service",
        "Starting installation of 'Bonjour Service'. Follow any UAC prompt."
    );

    int rc = mdns::MdnsResponder::install();

    // Re-check after install to be safe.
    if (rc == 0 && isWindowsServicePresent(serviceName)) {
        QMessageBox::information(
            this,
            "Installation Complete",
            "Bonjour Service installed successfully.\nThe application will restart."
        );
        restartApplication();
        return false; // do not start server in this instance
    }

    QMessageBox::critical(
        this,
        "Installation Failed",
        "Failed to install 'Bonjour Service'.\n\n"
        "The application will now exit."
    );
    return false;
}

void MainWindow::restartApplication() {
    m_quitting = true;
    stopServer();

    // Let main() release the single-instance pipe before starting the
    // replacement process. Starting it here would make it mistake this
    // still-running process for the instance it should activate.
    QTimer::singleShot(0, qApp, []() {
        QCoreApplication::exit(MainWindow::RestartExitCode);
    });
}
