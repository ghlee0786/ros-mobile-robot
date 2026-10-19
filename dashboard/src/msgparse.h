#pragma once

#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QVector3D>

// ROS 메시지(rosbridge JSON)를 해석하는 순수 함수 모음. 상태가 없어 어느 스레드에서도 부를 수 있다.
// 좌표는 ROS 기준(x 앞 · y 왼쪽, m · rad).
namespace msgparse {

// 평면 변환(평행 이동 x, y + 회전 yaw). 기본값은 항등.
struct Pose2D {
    double x = 0, y = 0, yaw = 0;
};

// 쿼터니언 {x,y,z,w} → yaw
double yawOf(const QJsonObject &q);
// yaw → 쿼터니언 {x:0,y:0,z:sin(θ/2),w:cos(θ/2)}
QJsonObject quatFromYaw(double yaw);
// t 를 p 에 적용: t * p (p 를 t 의 자식 프레임에서 부모 프레임으로)
Pose2D transform2D(const Pose2D &t, const Pose2D &p);
// map 프레임의 점을 odom 프레임으로: p_odom = (map->odom)^-1 * p_map
Pose2D mapToOdom(const Pose2D &mapOdom, const Pose2D &pMap);
// odom 프레임의 점을 map 프레임으로: p_map = (map->odom) * p_odom
Pose2D odomToMap(const Pose2D &mapOdom, const Pose2D &pOdom);

struct Scan {
    QList<QVector3D> points;   // base 프레임(장착 변환 적용 뒤)
    double nearestRange = 0;   // m, 0 = 없음
    double nearestAngle = 0;   // 도, base 프레임 기준
    double scanTime = 0;       // 한 바퀴 시간 s
};
// sensor_msgs/LaserScan → 점. mount 는 base_link → 레이저 프레임 변환, selfHide m 안의 점은 버린다.
Scan parseScan(const QJsonObject &m, const Pose2D &mount = {}, double selfHide = 0);

// OccupancyGrid data → RGBA 이미지. 0 행이 이미지 아래가 된다(이미지 위 = +y, 오른쪽 = +x).
// scheme: "map" · "costmap" · "raw". binaryThreshold >= 0 이면 0..100 값을 그 값 이상 = 검정, 미만 = 흰색으로 그린다.
// data 가 w*h 보다 짧으면 빈 이미지를 돌려준다.
QImage occupancyToImage(const QJsonArray &data, int w, int h, const QString &scheme = QStringLiteral("map"),
                        int binaryThreshold = -1);

// "pkg/msg/Type" → "pkg/Type"
QString shortType(QString t);
// 경로를 2 cm 간격 점으로 채운다(인스턴스 점으로 선처럼 보이게). a 는 넣지 않는다.
void appendDense(QList<QVector3D> &out, QVector3D a, QVector3D b);
// 메시지를 "a.b.c" 열 이름 → 값으로 펼친다. 숫자 배열은 공백으로 이어 한 칸에 둔다.
void flatten(const QJsonValue &v, const QString &key, QHash<QString, QString> &out);
// CSV 칸 하나(쉼표 · 따옴표 · 줄바꿈이 있으면 따옴표로 감싼다)
QString csvCell(QString s);

// geometry_msgs/PoseWithCovarianceStamped 의 covariance(36개): x · y 0.25 m², yaw (π/12)²
QJsonArray initialPoseCovariance();

} // namespace msgparse
