#include "testhooks.h"
#include "robot.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QMutex>
#include <QPointF>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSizeF>
#include <QSslSocket>
#include <QTimer>
#include <QVector3D>
#include <cstdio>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace {

// 메시지 핸들러는 전역 함수라 상태도 전역에 둔다(여러 스레드에서 불릴 수 있어 잠근다).
QMutex g_logLock;
QFile *g_logFile = nullptr;
bool g_toStderr = false;
QStringList g_warnings;
bool g_badWarning = false;

const QRegularExpression kBadWarning(QStringLiteral(
    "Binding loop|TypeError|ReferenceError|is not defined|is not a type|Cannot assign|Unable to assign"));

void messageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    static const char *const kTag[] = {"D", "W", "C", "F", "I"};
    const QByteArray line = QStringLiteral("[%1] %2 %3: %4\n")
                                .arg(QDateTime::currentDateTime().toString("HH:mm:ss.zzz"),
                                     QString::fromLatin1(kTag[type]),
                                     QString::fromLatin1(ctx.category ? ctx.category : "default"), msg)
                                .toUtf8();
    QMutexLocker lock(&g_logLock);
    if (g_logFile) {
        g_logFile->write(line);
        g_logFile->flush();
    }
    if (g_toStderr) {
        fwrite(line.constData(), 1, line.size(), stderr);
        fflush(stderr);
    }
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        g_warnings.append(msg);
        if (kBadWarning.match(msg).hasMatch())
            g_badWarning = true;
    }
}

// QVariant → JSON. 큰 목록은 개수와 마지막 값만, 바꿀 수 없는 형식은 문자열로.
QJsonValue toJson(const QVariant &v)
{
    if (!v.isValid())
        return QJsonValue();
    const QMetaType mt = v.metaType();
    if (mt.flags() & QMetaType::PointerToQObject) {
        const QObject *o = v.value<QObject *>();
        if (!o)
            return QJsonValue();
        return o->objectName().isEmpty() ? QString::fromLatin1(o->metaObject()->className()) : o->objectName();
    }
    if (mt == QMetaType::fromType<QList<QVector3D>>()) {
        const auto l = v.value<QList<QVector3D>>();
        QJsonObject r{{"count", l.size()}};
        if (!l.isEmpty())
            r["last"] = QJsonArray{l.last().x(), l.last().y(), l.last().z()};
        return r;
    }
    if (mt == QMetaType::fromType<QList<QPointF>>()) {
        const auto l = v.value<QList<QPointF>>();
        QJsonObject r{{"count", l.size()}};
        if (!l.isEmpty())
            r["last"] = QJsonArray{l.last().x(), l.last().y()};
        return r;
    }
    if (mt == QMetaType::fromType<QSizeF>()) {
        const QSizeF s = v.toSizeF();
        return QJsonObject{{"width", s.width()}, {"height", s.height()}};
    }
    if (mt == QMetaType::fromType<QImage>()) {
        const QImage img = v.value<QImage>();
        return QJsonObject{{"width", img.width()}, {"height", img.height()}};
    }
    const QJsonValue j = QJsonValue::fromVariant(v);
    if (!j.isNull() || v.isNull())
        return j;
    if (v.canConvert<QString>() && !v.toString().isEmpty())
        return v.toString();
    QString s;
    QDebug(&s).nospace() << v;
    return s;
}

bool isSimple(const QVariant &v)
{
    switch (v.metaType().id()) {
    case QMetaType::Bool: case QMetaType::Int: case QMetaType::UInt: case QMetaType::LongLong:
    case QMetaType::ULongLong: case QMetaType::Double: case QMetaType::Float: case QMetaType::QString:
        return true;
    default:
        return false;
    }
}

// C++ 형식(robot · bridge)은 QObject 위의 모든 속성, QML 형식(scene 등)은 그 QML 파일이 선언한 단순 속성만.
QJsonObject dumpObject(const QObject *obj, bool qmlDeclared)
{
    QJsonObject r;
    const QMetaObject *mo = obj->metaObject();
    const int start = qmlDeclared ? mo->propertyOffset() : QObject::staticMetaObject.propertyCount();
    for (int i = start; i < mo->propertyCount(); ++i) {
        const QMetaProperty p = mo->property(i);
        if (!p.isReadable())
            continue;
        const QVariant v = p.read(obj);
        if (qmlDeclared && !isSimple(v))
            continue;
        r[QString::fromLatin1(p.name())] = toJson(v);
    }
    return r;
}

} // namespace

TestHooks::TestHooks(const TestOptions &opt) : m_opt(opt)
{
    m_clock.start();
    {
        QMutexLocker lock(&g_logLock);
        if (!opt.logPath.isEmpty()) {
            g_logFile = new QFile(opt.logPath);
            if (!g_logFile->open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                delete g_logFile;
                g_logFile = nullptr;
            }
        }
        g_toStderr = opt.testMode;
    }
    qInstallMessageHandler(messageHandler);

    if (!opt.actionsPath.isEmpty()) {
        QFile f(opt.actionsPath);
        QJsonParseError err{};
        const QJsonDocument doc = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll(), &err) : QJsonDocument();
        if (!doc.isArray()) {
            fail(QStringLiteral("actions 파일을 읽을 수 없음: %1 %2").arg(opt.actionsPath, err.errorString()));
        } else {
            for (const QJsonValue &v : doc.array())
                m_actions.append({v.toObject(), v.toObject().value("t").toDouble()});
        }
    }
}

TestHooks::~TestHooks()
{
    qInstallMessageHandler(nullptr);
    QMutexLocker lock(&g_logLock);
    delete g_logFile;
    g_logFile = nullptr;
}

void TestHooks::fail(const QString &msg)
{
    m_actionsFailed = true;
    qCritical().noquote() << "[testhooks]" << msg;
}

void TestHooks::start(QQuickWindow *win)
{
    m_win = win;
    // actions 의 t · --quit-after · state 의 t 는 모두 QML 로드가 끝난 때부터 센다(로드 시간은 부하에 따라 2~10 s).
    qInfo().noquote() << QStringLiteral("[testhooks] QML 로드 %1 s").arg(m_clock.restart() / 1000.0);
    // Settings alias 가 창 크기를 되살린 뒤에 덮어쓴다.
    if (m_opt.size.isValid())
        QTimer::singleShot(0, this, [this] {
            m_win->setProperty("width", m_opt.size.width());
            m_win->setProperty("height", m_opt.size.height());
        });

    connect(win, &QQuickWindow::frameSwapped, this, [this] {
        const qint64 now = m_clock.elapsed();
        if (m_firstFrameMs < 0)
            m_firstFrameMs = m_bucketStartMs = now;
        m_lastFrameMs = now;
        ++m_frames;
        ++m_bucketFrames;
        if (now - m_bucketStartMs >= 1000) {
            const double fps = m_bucketFrames * 1000.0 / (now - m_bucketStartMs);
            m_fpsMin = m_fpsMin < 0 ? fps : qMin(m_fpsMin, fps);
            m_fpsLast = fps;
            m_bucketStartMs = now;
            m_bucketFrames = 0;
        }
    }, Qt::QueuedConnection);

    if (!m_opt.tracePath.isEmpty() && m_opt.traceHz > 0) {
        m_traceFile = new QFile(m_opt.tracePath, this);
        if (m_traceFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            auto *t = new QTimer(this);
            connect(t, &QTimer::timeout, this, &TestHooks::writeTrace);
            t->start(int(1000 / m_opt.traceHz));
            // 20 ms 타이머가 늦은 만큼 = GUI 스레드가 멈춘 시간. trace 줄마다 그 사이 최대값을 쓴다.
            auto *stall = new QTimer(this);
            stall->setTimerType(Qt::PreciseTimer);
            connect(stall, &QTimer::timeout, this, [this] {
                const qint64 now = m_clock.elapsed();
                if (m_stallLastMs >= 0)
                    m_stallMaxMs = qMax(m_stallMaxMs, now - m_stallLastMs - 20);
                m_stallLastMs = now;
            });
            stall->start(20);
            // 연결이 잠깐(주기보다 짧게) 끊겼다 붙어도 trace 에 남도록 바뀔 때마다 한 줄 더 쓴다
            if (QObject *b = findObject(QStringLiteral("bridge")))
                connect(b, SIGNAL(connectedChanged()), this, SLOT(writeTrace()));
        } else {
            qWarning().noquote() << "[testhooks] trace 파일을 열 수 없음:" << m_opt.tracePath;
        }
    }

    if (m_opt.quitAfter > 0)
        QTimer::singleShot(int(m_opt.quitAfter * 1000), this, [this] {
            finish(QStringLiteral("quit-after"));
            QCoreApplication::quit();
        });
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { finish(QStringLiteral("quit")); });

    QTimer::singleShot(0, this, &TestHooks::runNext);
}

void TestHooks::writeTrace()
{
    if (m_finished)   // 끝내며 연결을 닫는 것은 trace 에 넣지 않는다
        return;
    m_traceFile->write(QJsonDocument(snapshot(false)).toJson(QJsonDocument::Compact) + '\n');
    m_traceFile->flush();
    m_stallMaxMs = 0;
}

QObject *TestHooks::findObject(const QString &path) const
{
    const QStringList parts = path.split('.');
    QObject *o = nullptr;
    if (parts[0] == "win")
        o = m_win;
    else if (parts[0] == "bridge")
        o = findObject(QStringLiteral("robot.bridge"));
    else
        o = m_win->findChild<QObject *>(parts[0]);
    for (int i = 1; o && i < parts.size(); ++i)
        o = o->property(parts[i].toLatin1()).value<QObject *>();
    return o;
}

// actions 를 차례로: 각 항목은 자기 t(실행 뒤 초)가 되고 앞 항목(wait 포함)이 끝난 뒤에 돈다.
void TestHooks::runNext()
{
    if (m_finished || m_next >= m_actions.size())
        return;
    const Action &a = m_actions[m_next];
    const double now = m_clock.elapsed() / 1000.0;
    if (now < a.t) {
        QTimer::singleShot(int((a.t - now) * 1000) + 1, this, &TestHooks::runNext);
        return;
    }
    const double wall = QDateTime::currentMSecsSinceEpoch() / 1000.0;   // 실행 직전(보낸 메시지보다 앞선다)
    if (a.o.contains("wait")) {
        const QString path = a.o.value("wait").toString();
        const int dot = path.lastIndexOf('.');
        QObject *o = dot > 0 ? findObject(path.left(dot)) : nullptr;
        if (!o || o->metaObject()->indexOfProperty(path.mid(dot + 1).toLatin1()) < 0) {
            fail(QStringLiteral("action %1: wait 대상 없음 %2").arg(m_next).arg(path));
        } else if (!o->property(path.mid(dot + 1).toLatin1()).toBool()) {
            if (m_waitStart < 0)
                m_waitStart = now;
            if (now - m_waitStart < a.o.value("timeout_s").toDouble(10)) {
                QTimer::singleShot(50, this, &TestHooks::runNext);
                return;
            }
            fail(QStringLiteral("action %1: wait 시간 초과 %2").arg(m_next).arg(path));
        }
        m_waitStart = -1;
    } else {
        QString err;
        if (!execute(a.o, &err))
            fail(QStringLiteral("action %1: %2").arg(m_next).arg(err));
    }
    // 실행기는 call · set 이름과 wall(epoch 초)로 서버 로그와 맞춰 본다(가짜 대시보드와 같은 모양).
    QJsonObject done = a.o;
    done["i"] = m_next;
    done["t"] = a.t;
    done["elapsed"] = now;
    done["wall"] = wall;
    m_done.append(done);
    ++m_next;
    QTimer::singleShot(0, this, &TestHooks::runNext);
}

bool TestHooks::execute(const QJsonObject &a, QString *err)
{
    qInfo().noquote() << "[action]" << QJsonDocument(a).toJson(QJsonDocument::Compact);
    if (a.contains("call")) {
        const QString path = a.value("call").toString();
        const int dot = path.lastIndexOf('.');
        QObject *o = dot > 0 ? findObject(path.left(dot)) : nullptr;
        if (!o)
            return *err = "객체 없음: " + path, false;
        const QByteArray name = path.mid(dot + 1).toLatin1();
        const QJsonArray args = a.value("args").toArray();
        const QMetaObject *mo = o->metaObject();
        QMetaMethod m;
        for (int i = 0; i < mo->methodCount() && !m.isValid(); ++i)
            if (mo->method(i).name() == name && mo->method(i).parameterCount() == args.size())
                m = mo->method(i);
        if (!m.isValid() && a.value("optional").toBool()) {   // 이 빌드에 없는 메서드: 건너뛴다
            qInfo().noquote() << "[action] 없는 메서드 건너뜀:" << path;
            return true;
        }
        if (!m.isValid())
            return *err = QStringLiteral("메서드 없음: %1(인자 %2개)").arg(path).arg(args.size()), false;
        QList<QVariant> vals;
        QList<void *> argv{nullptr};
        for (int i = 0; i < args.size(); ++i) {
            QVariant v = args[i].toVariant();
            const QMetaType t = m.parameterMetaType(i);
            if (t != QMetaType::fromType<QVariant>() && !v.convert(t))
                return *err = QStringLiteral("%1 의 인자 %2 를 %3 로 바꿀 수 없음").arg(path).arg(i).arg(t.name()), false;
            vals.append(v);
        }
        for (int i = 0; i < vals.size(); ++i)
            argv.append(m.parameterMetaType(i) == QMetaType::fromType<QVariant>() ? static_cast<void *>(&vals[i]) : vals[i].data());
        QVariant ret(m.returnMetaType().isValid() && m.returnMetaType().id() != QMetaType::Void ? m.returnMetaType() : QMetaType());
        argv[0] = ret.isValid() ? ret.data() : nullptr;
        QMetaObject::metacall(o, QMetaObject::InvokeMetaMethod, m.methodIndex(), argv.data());
        return true;
    }
    if (a.contains("set")) {
        const QString path = a.value("set").toString();
        const int dot = path.lastIndexOf('.');
        QObject *o = dot > 0 ? findObject(path.left(dot)) : nullptr;
        const QByteArray name = path.mid(dot + 1).toLatin1();
        if (!o || o->metaObject()->indexOfProperty(name) < 0)
            return *err = "속성 없음: " + path, false;
        if (!o->setProperty(name, a.value("value").toVariant()))
            return *err = "속성을 바꿀 수 없음: " + path, false;
        return true;
    }
    if (a.contains("shot")) {
        const QString file = a.value("shot").toString();
        if (!m_win->grabWindow().save(file))
            return *err = "캡처 저장 실패: " + file, false;
        return true;
    }
    if (a.contains("key")) {
        const QString key = a.value("key").toString();
        const QKeySequence seq(key);
        if (seq.isEmpty())
            return *err = "알 수 없는 키: " + key, false;
        const QKeyCombination kc = seq[0];
        const QString text = key.size() == 1 ? key.toLower() : QString();
        QKeyEvent press(QEvent::KeyPress, kc.key(), kc.keyboardModifiers(), text);
        QGuiApplication::sendEvent(m_win, &press);
        QTimer::singleShot(a.value("hold_ms").toInt(100), this, [this, kc, text] {
            QKeyEvent release(QEvent::KeyRelease, kc.key(), kc.keyboardModifiers(), text);
            QGuiApplication::sendEvent(m_win, &release);
        });
        return true;
    }
    return *err = "알 수 없는 action", false;
}

QJsonObject TestHooks::snapshot(bool full) const
{
    QJsonObject r{{"t", m_clock.elapsed() / 1000.0}, {"wall", QDateTime::currentMSecsSinceEpoch() / 1000.0},
                  {"win", QJsonObject{{"page", m_win->property("page").toInt()},
                                      {"width", m_win->width()}, {"height", m_win->height()}}}};
    if (QObject *o = findObject(QStringLiteral("robot")))
        r["robot"] = dumpObject(o, false);
    if (QObject *o = findObject(QStringLiteral("bridge")))
        r["bridge"] = dumpObject(o, false);
    // 메모리 · fps 는 trace 줄마다 넣어 시계열로 본다(fps: 직전 1 s 구간)
    QJsonObject proc;
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc)) {
        proc["rssMB"] = pmc.WorkingSetSize / 1048576.0;
        proc["peakMB"] = pmc.PeakWorkingSetSize / 1048576.0;
    }
#endif
    r["proc"] = proc;
    const double span = (m_lastFrameMs - m_firstFrameMs) / 1000.0;
    r["render"] = QJsonObject{{"fpsAvg", span > 0 ? (m_frames - 1) / span : 0.0}, {"fps", m_fpsLast},
                              {"fpsMin", m_fpsMin < 0 ? 0.0 : m_fpsMin}, {"frames", m_frames},
                              {"stallMs", m_stallMaxMs}};   // 직전 trace 줄 뒤 GUI 스레드가 가장 오래 멈춘 ms
    if (!full)
        return r;
    // 배포 점검(설치본이 개발 배치와 같게 도는지): 데이터 · 기록 폴더, 글꼴 · SVG 아이콘 · TLS 플러그인
    QJsonArray fonts;
    for (const QString &fam : QFontDatabase::families())
        if (fam.startsWith("Pretendard") || fam.startsWith("JetBrains Mono"))
            fonts.append(fam);
    r["deploy"] = QJsonObject{
        {"exeDir", QCoreApplication::applicationDirPath()}, {"dataDir", Robot::dataDir()},
        {"installed", Robot::installed()}, {"recordsPath", Robot::recordsPath()}, {"fonts", fonts},
        {"svg", QImageReader::supportedImageFormats().contains("svg")},
        {"iconLoads", !QImage(":/icons/plug.svg").isNull()},
        {"tlsBackends", QJsonArray::fromStringList(QSslSocket::availableBackends())},
        {"tls", QSslSocket::supportsSsl()}};
    for (const char *name : {"scene", "inspector", "dash"})
        if (QObject *o = findObject(QString::fromLatin1(name)))
            r[QString::fromLatin1(name)] = dumpObject(o, true);
    return r;
}

void TestHooks::finish(const QString &reason)
{
    if (m_finished || !m_win)
        return;
    m_finished = true;
    if (m_next < m_actions.size())
        fail(QStringLiteral("끝나기 전에 actions %1/%2 만 실행됨").arg(m_next).arg(m_actions.size()));
    if (!m_opt.screenshot.isEmpty() && !m_win->grabWindow().save(m_opt.screenshot))
        qWarning().noquote() << "[testhooks] 캡처 저장 실패:" << m_opt.screenshot;
    if (m_opt.statePath.isEmpty())
        return;

    QJsonObject s = snapshot(true);
    s["actions_done"] = m_done;
    {
        QMutexLocker lock(&g_logLock);
        s["warnings"] = QJsonArray::fromStringList(g_warnings);
    }
    s["exit"] = reason;
    QFile f(m_opt.statePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(QJsonDocument(s).toJson()) < 0)
        qWarning().noquote() << "[testhooks] state 저장 실패:" << m_opt.statePath;
}

int TestHooks::exitCode(int rc) const
{
    if (m_actionsFailed)
        return 3;
    QMutexLocker lock(&g_logLock);
    if (m_opt.failOnWarning && g_badWarning)
        return 2;
    return rc;
}
