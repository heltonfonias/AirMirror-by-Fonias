#pragma once

#include <QObject>
#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QSaveFile>
#include <QUrl>

class QNetworkReply;

struct UpdateInfo {
    QString version;
    QString notes;
    QUrl downloadUrl;
    QByteArray sha256;
    qint64 size = 0;
};

class UpdateManager : public QObject {
    Q_OBJECT
public:
    explicit UpdateManager(QObject *parent = nullptr);
    void check();
    void download(const UpdateInfo &update);
    static bool parseRelease(const QByteArray &json, UpdateInfo *update, QString *error);
    static bool isNewer(const QString &version);
    static bool launchInstaller(const QString &path);
    static bool selfTest();

signals:
    void updateFound(const UpdateInfo &update);
    void upToDate();
    void checkFailed(const QString &message);
    void downloadProgress(qint64 received, qint64 total);
    void installerReady(const QString &path);
    void downloadFailed(const QString &message);

private:
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_checkReply;
    QPointer<QNetworkReply> m_downloadReply;
    QSaveFile *m_file = nullptr;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    QByteArray m_expectedDigest;
    qint64 m_expectedSize = 0;
    qint64 m_received = 0;
};
