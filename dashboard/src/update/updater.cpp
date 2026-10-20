#include "updater.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

namespace {
const char *kLatestUrl = "https://api.github.com/repos/ghlee0786/ros-mobile-robot/releases/latest";
constexpr int kFirstCheckMs = 10 * 1000;
constexpr int kEveryMs = 6 * 60 * 60 * 1000;

QList<int> versionParts(QString v)
{
    v = v.trimmed();
    if (v.startsWith('v') || v.startsWith('V'))
        v.remove(0, 1);
    v = v.section('+', 0, 0).section('-', 0, 0);
    QList<int> out;
    for (const QString &p : v.split('.'))
        out.append(p.toInt());
    while (out.size() < 3)
        out.append(0);
    return out;
}

QNetworkRequest request(const QUrl &url)
{
    QNetworkRequest q(url);
    // GitHub API 는 User-Agent 가 없으면 403 을 준다
    q.setHeader(QNetworkRequest::UserAgentHeader, "RobotDashboard/" + QCoreApplication::applicationVersion());
    q.setTransferTimeout(30000);
    return q;
}
}

int Updater::compareVersions(const QString &a, const QString &b)
{
    const QList<int> x = versionParts(a), y = versionParts(b);
    for (int i = 0; i < qMax(x.size(), y.size()); ++i) {
        const int d = x.value(i) - y.value(i);
        if (d != 0)
            return d < 0 ? -1 : 1;
    }
    return 0;
}

Updater::Release Updater::parseRelease(const QByteArray &json, bool *ok)
{
    Release r;
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    r.tag = o.value("tag_name").toString();
    r.body = o.value("body").toString();
    for (const QJsonValue &v : o.value("assets").toArray()) {
        const QJsonObject a = v.toObject();
        const QString name = a.value("name").toString();
        if (name.startsWith("RobotDashboard-Setup-") && name.endsWith(".exe", Qt::CaseInsensitive)) {
            r.assetName = name;
            r.assetUrl = a.value("browser_download_url").toString();
            r.assetSize = a.value("size").toInteger(-1);
            break;
        }
    }
    if (ok)
        *ok = !r.tag.isEmpty();
    return r;
}

Updater::Updater(QObject *parent) : QObject(parent)
{
    // 첫 조회는 시작 10 s 뒤, 그 뒤 6 시간마다. 자동 시험(ROBOT_DASHBOARD_TEST=1)에서는 주소를 따로 주지 않으면 조회하지 않는다.
    const bool test = qEnvironmentVariableIntValue("ROBOT_DASHBOARD_TEST") == 1
                      && qEnvironmentVariableIsEmpty("ROBOT_DASHBOARD_UPDATE_URL");
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        m_timer.start(kEveryMs);
        if (checkEnabled())
            check(false);
    });
    if (!test)
        m_timer.start(kFirstCheckMs);
}

QString Updater::currentVersion() const { return QCoreApplication::applicationVersion(); }

bool Updater::checkEnabled() const { return QSettings().value("updateCheck", true).toBool(); }

void Updater::setCheckEnabled(bool on)
{
    QSettings().setValue("updateCheck", on);
    emit changed();
}

void Updater::check(bool manual)
{
    if (m_reply || m_download)
        return;
    const QString env = qEnvironmentVariable("ROBOT_DASHBOARD_UPDATE_URL");
    m_state = "checking";
    if (manual)
        m_message.clear();
    emit changed();
    QNetworkRequest q = request(QUrl(env.isEmpty() ? QString(kLatestUrl) : env));
    q.setRawHeader("Accept", "application/vnd.github+json");
    QNetworkReply *r = m_net.get(q);
    m_reply = r;
    connect(r, &QNetworkReply::finished, this, [this, r, manual] { finishCheck(r, manual); });
}

void Updater::finishCheck(QNetworkReply *r, bool manual)
{
    r->deleteLater();
    m_reply = nullptr;
    const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    bool ok = false;
    const Release rel = r->error() == QNetworkReply::NoError ? parseRelease(r->readAll(), &ok) : Release{};
    if (!ok) {
        // 오프라인 · 404(릴리스 없음) · 403(rate limit) · 깨진 JSON: 조용히 넘기고 로그만 남긴다
        qInfo().noquote() << "업데이트 확인 실패:" << status << r->errorString();
        m_state = "failed";
        if (manual)
            m_message = "업데이트를 확인하지 못했습니다(" + (status ? QString::number(status) : r->errorString()) + ")";
        emit changed();
        return;
    }
    m_rel = rel;
    if (compareVersions(rel.tag, currentVersion()) <= 0)
        m_state = "current";
    else
        m_state = QSettings().value("skipVersion").toString() == rel.tag ? "skipped" : "available";
    if (manual) {
        m_dismissed = false;
        if (m_state == "current")
            m_message = "최신 버전(v" + currentVersion() + ")을 쓰고 있습니다";
        else if (m_state == "skipped")
            m_state = "available";   // 직접 확인하면 건너뛴 버전도 다시 보여 준다
    }
    qInfo().noquote() << "업데이트 확인:" << rel.tag << "· 현재" << currentVersion() << "→" << m_state;
    emit changed();
}

void Updater::skip()
{
    QSettings().setValue("skipVersion", m_rel.tag);
    m_state = "skipped";
    emit changed();
}

void Updater::later()
{
    m_dismissed = true;
    emit changed();
}

void Updater::dismissMessage() { setMessage({}); }

void Updater::setMessage(const QString &m)
{
    m_message = m;
    emit changed();
}

void Updater::downloadAndInstall()
{
    if (m_download)
        return;
    if (m_driving)
        return setMessage("주행을 멈춘 뒤 설치하세요");
    if (m_rel.assetUrl.isEmpty())
        return setMessage("이 릴리스에 설치 파일(RobotDashboard-Setup-*.exe)이 없습니다");
    const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).absoluteFilePath(m_rel.assetName);
    m_file = new QFile(path, this);
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        delete m_file;
        m_file = nullptr;
        return setMessage("임시 파일을 만들지 못했습니다: " + path);
    }
    m_download = true;
    m_progress = 0;
    m_message.clear();
    emit changed();
    QNetworkRequest q = request(QUrl(m_rel.assetUrl));
    q.setTransferTimeout(120000);
    QNetworkReply *r = m_net.get(q);   // GitHub 는 다른 호스트로 넘긴다(기본 리다이렉트 정책이 따라간다)
    m_reply = r;
    connect(r, &QNetworkReply::readyRead, this, [this, r] { m_file->write(r->readAll()); });
    connect(r, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        const qint64 t = total > 0 ? total : m_rel.assetSize;
        m_progress = t > 0 ? double(got) / t : 0;
        emit changed();
    });
    connect(r, &QNetworkReply::finished, this, &Updater::finishDownload);
}

void Updater::finishDownload()
{
    QNetworkReply *r = m_reply;
    m_reply = nullptr;
    r->deleteLater();
    m_file->write(r->readAll());
    m_file->close();
    const QString path = m_file->fileName();
    const qint64 size = m_file->size();
    m_file->deleteLater();
    m_file = nullptr;
    m_download = false;
    if (r->error() != QNetworkReply::NoError || size != m_rel.assetSize) {
        QFile::remove(path);
        return setMessage(r->error() != QNetworkReply::NoError
                              ? "내려받지 못했습니다: " + r->errorString()
                              : QString("받은 파일 크기가 맞지 않습니다(%1 / %2 바이트)").arg(size).arg(m_rel.assetSize));
    }
    if (m_driving)
        return setMessage("주행을 멈춘 뒤 설치하세요");
    // /RELAUNCH=1: 무인 설치가 끝나면 대시보드를 다시 켠다(installer/robot_dashboard.iss 의 WantRun)
    const QStringList args{"/SILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/CLOSEAPPLICATIONS", "/RESTARTAPPLICATIONS", "/RELAUNCH=1"};
    if (!QProcess::startDetached(path, args))
        return setMessage("설치 파일을 실행하지 못했습니다: " + path);
    qInfo().noquote() << "업데이트 설치 시작:" << path;
    QCoreApplication::quit();
}
