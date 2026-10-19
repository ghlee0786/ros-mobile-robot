#pragma once

#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QVector3D>

// 3D 장면 모델들이 함께 쓰는 계산(좌표 · 색 · 공분산).
namespace scene {

double yawOf(const QJsonObject &q);   // geometry_msgs/Quaternion → yaw rad

// RViz getRainbowColor: value 0..1 을 h=value*5+1 인 6 구간 HSV 무지개로.
QColor rainbow(double value);

// 2x2 공분산 [[a, b], [b, c]] 의 2σ 타원. major ≥ minor 는 반축 m, angle 은 긴 축 방향 rad.
struct Ellipse { double major = 0, minor = 0, angle = 0; };
Ellipse covEllipse(double a, double b, double c);

// a → b 를 step m 간격 점으로 이어 out 에 더한다(a 는 넣지 않음). PointInstancing 으로 선을 그릴 때 쓴다.
void appendDense(QList<QVector3D> &out, QVector3D a, QVector3D b, float step = 0.02f);

}
