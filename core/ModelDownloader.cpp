#include "ModelDownloader.h"

#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace qtllama {

ModelDownloader::ModelDownloader(QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{
}

ModelDownloader::~ModelDownloader()
{
    cancel();
}

void ModelDownloader::start(const QUrl &url, const QString &destination)
{
    if (m_reply) {
        Q_EMIT failed(tr("A download is already running"));
        return;
    }
    if (!url.isValid()) {
        Q_EMIT failed(tr("No download URL for this model"));
        return;
    }

    m_dest = destination;
    m_cancelled = false;
    QDir().mkpath(QFileInfo(destination).absolutePath());

    m_file.setFileName(destination + QStringLiteral(".part"));
    if (!m_file.open(QIODevice::ReadWrite)) {
        Q_EMIT failed(tr("Cannot write %1: %2").arg(m_file.fileName(), m_file.errorString()));
        return;
    }
    m_offset = m_file.size();
    m_file.seek(m_offset);

    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("QTllama/" QTLLAMA_VERSION));
    if (m_offset > 0)
        req.setRawHeader("Range", "bytes=" + QByteArray::number(m_offset) + '-');

    m_reply = m_net->get(req);
    connect(m_reply, &QNetworkReply::readyRead, this, &ModelDownloader::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &ModelDownloader::onFinished);
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        Q_EMIT progress(m_offset + got, total > 0 ? m_offset + total : -1);
    });
}

void ModelDownloader::cancel()
{
    if (!m_reply)
        return;
    m_cancelled = true;
    m_reply->abort(); // emits finished()
}

void ModelDownloader::onReadyRead()
{
    // A server that ignores Range sends the whole file with 200: start over.
    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (m_offset > 0 && status == 200) {
        m_file.resize(0);
        m_file.seek(0);
        m_offset = 0;
    }
    m_file.write(m_reply->readAll());
}

void ModelDownloader::onFinished()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() == QNetworkReply::NoError)
        m_file.write(reply->readAll());
    m_file.close();

    if (m_cancelled) {
        Q_EMIT failed(tr("Download cancelled"));
        return;
    }
    // 416: the .part file is already complete.
    if (reply->error() != QNetworkReply::NoError && status != 416) {
        Q_EMIT failed(reply->errorString());
        return;
    }

    QFile::remove(m_dest);
    if (!QFile::rename(m_file.fileName(), m_dest)) {
        Q_EMIT failed(tr("Cannot rename %1 to %2").arg(m_file.fileName(), m_dest));
        return;
    }
    Q_EMIT finished(m_dest);
}

} // namespace qtllama
