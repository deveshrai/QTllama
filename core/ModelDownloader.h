#pragma once

#include <QFile>
#include <QObject>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace qtllama {

// Downloads a model file to <dest>.part and renames it on success.
// Interrupted downloads resume from the existing .part file.
class ModelDownloader : public QObject
{
    Q_OBJECT

public:
    explicit ModelDownloader(QObject *parent = nullptr);
    ~ModelDownloader() override;

    bool isRunning() const { return m_reply != nullptr; }
    QString destination() const { return m_dest; }

public Q_SLOTS:
    void start(const QUrl &url, const QString &destination);
    void cancel();

Q_SIGNALS:
    void progress(qint64 received, qint64 total); // total is -1 when unknown
    void finished(const QString &path);
    void failed(const QString &error);

private:
    void onReadyRead();
    void onFinished();

    QNetworkAccessManager *m_net = nullptr;
    QNetworkReply *m_reply = nullptr;
    QFile m_file;
    QString m_dest;
    qint64 m_offset = 0;
    bool m_cancelled = false;
};

} // namespace qtllama
