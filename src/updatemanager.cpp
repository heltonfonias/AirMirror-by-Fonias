#include "updatemanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVersionNumber>

#include <windows.h>
#include <shellapi.h>

namespace {
constexpr auto apiUrl = "https://api.github.com/repos/heltonfonias/AirMirror-by-Fonias/releases/latest";
constexpr auto assetName = "AirMirror-by-Fonias-x64.msi";
constexpr qint64 maxInstallerBytes = 500LL * 1024 * 1024;

QNetworkRequest requestFor(const QUrl &url) {
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      "AirMirror-by-Fonias/" + QCoreApplication::applicationVersion());
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(30000);
    return request;
}

bool validVersion(const QString &version) {
    static const QRegularExpression pattern("^v[0-9]+\\.[0-9]+\\.[0-9]+$");
    return pattern.match(version).hasMatch();
}
}

UpdateManager::UpdateManager(QObject *parent) : QObject(parent) {}

bool UpdateManager::parseRelease(const QByteArray &json, UpdateInfo *update, QString *error) {
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if (!document.isObject()) {
        if (error) *error = "GitHub returned an invalid release response.";
        return false;
    }
    const QJsonObject release = document.object();
    const QString tag = release.value("tag_name").toString();
    if (release.value("draft").toBool() || release.value("prerelease").toBool() ||
        !validVersion(tag)) {
        if (error) *error = "The latest release has no valid stable version.";
        return false;
    }
    for (const QJsonValue &value : release.value("assets").toArray()) {
        const QJsonObject asset = value.toObject();
        if (asset.value("name").toString() != assetName) continue;
        const QString digest = asset.value("digest").toString();
        const qint64 size = static_cast<qint64>(asset.value("size").toDouble());
        const QUrl url(asset.value("browser_download_url").toString());
        const QString expectedPath = "/heltonfonias/AirMirror-by-Fonias/releases/download/" +
                                     tag + "/" + assetName;
        static const QRegularExpression hashPattern("^sha256:[0-9a-fA-F]{64}$");
        if (!hashPattern.match(digest).hasMatch() || size <= 0 || size > maxInstallerBytes ||
            url.scheme() != "https" || url.host() != "github.com" ||
            url.path() != expectedPath) {
            if (error) *error = "The release installer or its SHA-256 digest is invalid.";
            return false;
        }
        if (update) {
            update->version = tag.mid(1);
            update->notes = release.value("body").toString();
            update->downloadUrl = url;
            update->sha256 = QByteArray::fromHex(digest.mid(7).toLatin1());
            update->size = size;
        }
        return true;
    }
    if (error) *error = "The latest release has no x64 installer.";
    return false;
}

bool UpdateManager::isNewer(const QString &version) {
    const QVersionNumber current = QVersionNumber::fromString(
        QCoreApplication::applicationVersion());
    const QVersionNumber latest = QVersionNumber::fromString(version);
    return !current.isNull() && !latest.isNull() && latest > current;
}

void UpdateManager::check() {
    if (m_checkReply) return;
    QNetworkReply *reply = m_network.get(requestFor(QUrl(QString::fromLatin1(apiUrl))));
    m_checkReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_checkReply = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status != 200) {
            emit checkFailed(reply->error() == QNetworkReply::NoError
                                 ? QString("GitHub returned HTTP %1.").arg(status)
                                 : reply->errorString());
        } else {
            UpdateInfo update;
            QString error;
            if (!parseRelease(reply->readAll(), &update, &error)) emit checkFailed(error);
            else if (isNewer(update.version)) emit updateFound(update);
            else emit upToDate();
        }
        reply->deleteLater();
    });
}

void UpdateManager::download(const UpdateInfo &update) {
    if (m_downloadReply || m_file) return;
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                           "/updates";
    if (!QDir().mkpath(folder)) {
        emit downloadFailed("Could not create the update folder.");
        return;
    }
    const QString path = folder + "/AirMirror-by-Fonias-x64-" + update.version + ".msi";
    m_file = new QSaveFile(path);
    if (!m_file->open(QIODevice::WriteOnly)) {
        const QString error = m_file->errorString();
        delete m_file;
        m_file = nullptr;
        emit downloadFailed(error);
        return;
    }
    m_hash.reset();
    m_expectedDigest = update.sha256;
    m_expectedSize = update.size;
    m_received = 0;
    QNetworkReply *reply = m_network.get(requestFor(update.downloadUrl));
    m_downloadReply = reply;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        const QByteArray bytes = reply->readAll();
        m_received += bytes.size();
        if (m_received > maxInstallerBytes || !m_file || m_file->write(bytes) != bytes.size()) {
            reply->abort();
            return;
        }
        m_hash.addData(bytes);
        emit downloadProgress(m_received, m_expectedSize);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, path]() {
        m_downloadReply = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool valid = reply->error() == QNetworkReply::NoError && status == 200 &&
                           m_received == m_expectedSize &&
                           m_hash.result() == m_expectedDigest;
        if (valid && m_file && m_file->commit()) {
            emit installerReady(path);
        } else {
            if (m_file) m_file->cancelWriting();
            emit downloadFailed(reply->error() == QNetworkReply::NoError
                                    ? "The installer failed its size or SHA-256 check."
                                    : reply->errorString());
        }
        delete m_file;
        m_file = nullptr;
        reply->deleteLater();
    });
}

bool UpdateManager::launchInstaller(const QString &path) {
    if (!QFileInfo::exists(path)) return false;
    const QString parameters = "/i \"" + QDir::toNativeSeparators(path) + "\"";
    const HINSTANCE result = ShellExecuteW(nullptr, L"runas", L"msiexec.exe",
                                           reinterpret_cast<LPCWSTR>(parameters.utf16()),
                                           nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}

bool UpdateManager::selfTest() {
    const QByteArray valid = R"({"tag_name":"v9.2.3","draft":false,"prerelease":false,
      "assets":[{"name":"AirMirror-by-Fonias-x64.msi","size":1234,
      "digest":"sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
      "browser_download_url":"https://github.com/heltonfonias/AirMirror-by-Fonias/releases/download/v9.2.3/AirMirror-by-Fonias-x64.msi"}]})";
    UpdateInfo info;
    QString error;
    if (!parseRelease(valid, &info, &error) || info.version != "9.2.3" ||
        info.sha256.size() != 32 || !isNewer(info.version)) return false;
    if (parseRelease(QByteArray(valid).replace("sha256:aaaa", "sha256:xxxx"),
                     &info, &error)) return false;
    if (parseRelease(QByteArray(valid).replace("github.com/heltonfonias",
                                             "example.com/heltonfonias"),
                     &info, &error)) return false;
    if (parseRelease(QByteArray(valid).replace("\"prerelease\":false",
                                             "\"prerelease\":true"),
                     &info, &error)) return false;
    return true;
}
