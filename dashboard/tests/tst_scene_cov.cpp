// 공분산 2σ 타원(닫힌 해 고유분해)과 AmclPose 의 map→odom 변환.
#include "scene/amclpose.h"
#include "scene/scenemath.h"

#include <QJsonArray>
#include <QtTest>
#include <cmath>

static QJsonObject poseMsg(const QString &frame, double x, double y, double yaw, const QList<double> &cov36)
{
    QJsonArray cov;
    for (double v : cov36)
        cov.append(v);
    return {{"header", QJsonObject{{"frame_id", frame}}},
            {"pose", QJsonObject{{"pose", QJsonObject{{"position", QJsonObject{{"x", x}, {"y", y}, {"z", 0}}},
                                                      {"orientation", QJsonObject{{"z", std::sin(yaw / 2)},
                                                                                  {"w", std::cos(yaw / 2)}}}}},
                                 {"covariance", cov}}}};
}

static QList<double> cov(double xx, double xy, double yy, double yawyaw)
{
    QList<double> c(36, 0.0);
    c[0] = xx, c[1] = c[6] = xy, c[7] = yy, c[35] = yawyaw;
    return c;
}

class TstSceneCov : public QObject
{
    Q_OBJECT
private slots:
    void diagonal()
    {
        const scene::Ellipse e = scene::covEllipse(0.04, 0, 0.01);
        QCOMPARE(e.major, 0.4);   // 2·sqrt(0.04)
        QCOMPARE(e.minor, 0.2);
        QCOMPARE(e.angle, 0.0);
        const scene::Ellipse t = scene::covEllipse(0.01, 0, 0.04);   // y 가 더 크면 긴 축은 90°
        QCOMPARE(t.major, 0.4);
        QVERIFY(qAbs(t.angle - M_PI / 2) < 1e-12);
    }

    void rotated()
    {
        // R(θ) diag(l1, l2) R(θ)^T 를 다시 분해하면 l1 · l2 · θ 가 나온다
        const double l1 = 0.09, l2 = 0.01, th = 0.6, c = std::cos(th), s = std::sin(th);
        const double a = l1 * c * c + l2 * s * s, b = (l1 - l2) * c * s, d = l1 * s * s + l2 * c * c;
        const scene::Ellipse e = scene::covEllipse(a, b, d);
        QVERIFY(qAbs(e.major - 2 * std::sqrt(l1)) < 1e-12);
        QVERIFY(qAbs(e.minor - 2 * std::sqrt(l2)) < 1e-12);
        QVERIFY(qAbs(e.angle - th) < 1e-12);
    }

    void circle()
    {
        const scene::Ellipse e = scene::covEllipse(0.25, 0, 0.25);
        QCOMPARE(e.major, 1.0);
        QCOMPARE(e.minor, 1.0);
    }

    void amclInMapToOdom()
    {
        AmclPose p;
        // map→odom = (1, 2, 90°)
        const QJsonObject tr{{"header", QJsonObject{{"frame_id", "map"}}}, {"child_frame_id", "odom"},
                             {"transform", QJsonObject{{"translation", QJsonObject{{"x", 1}, {"y", 2}, {"z", 0}}},
                                                       {"rotation", QJsonObject{{"z", std::sin(M_PI / 4)},
                                                                                {"w", std::cos(M_PI / 4)}}}}}};
        p.feed("/tf", {{"transforms", QJsonArray{tr}}});
        p.feed("/amcl_pose", poseMsg("map", 1, 3, M_PI / 2, cov(0.04, 0, 0.01, 0.0685)));
        QVERIFY(p.valid() && p.received());
        // map (1, 3) - (1, 2) = (0, 1) 을 -90° 돌리면 odom (1, 0)
        QVERIFY(qAbs(p.x() - 1) < 1e-9 && qAbs(p.y()) < 1e-9);
        QVERIFY(qAbs(p.yaw()) < 1e-9);
        QVERIFY(qAbs(p.angle() + M_PI / 2) < 1e-9);   // 긴 축(map x)도 -90° 돈다
        QVERIFY(qAbs(p.axisMajor() - 0.4) < 1e-12);
        QVERIFY(qAbs(p.yawSpread() - 2 * std::sqrt(0.0685)) < 1e-12);
    }

    void odomZeroCovarianceInvalid()
    {
        AmclPose p;
        p.setTopic("/odom");
        p.feed("/odom", poseMsg("odom", 1, 1, 0, cov(0, 0, 0, 0)));
        QVERIFY(p.received());
        QVERIFY(!p.valid());   // 공분산이 모두 0 이면 타원을 그리지 않는다
        p.feed("/amcl_pose", poseMsg("map", 5, 5, 0, cov(1, 0, 1, 1)));   // 다른 토픽은 무시
        QCOMPARE(p.x(), 1.0);
    }
};

QTEST_MAIN(TstSceneCov)
#include "tst_scene_cov.moc"
