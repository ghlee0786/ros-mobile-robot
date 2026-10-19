#pragma once

#include <QPointF>
#include <algorithm>
#include <cmath>
#include <QString>
#include <deque>
#include <memory>
#include <optional>

// SISO 증분 변환. push() 는 마지막으로 처리한 시각보다 뒤의 점만 계산하고(앞선 점은 무시),
// 결과 점이 없으면 nullopt 를 돌려준다. reset() 은 상태를 처음으로 돌린다.
class Transform
{
public:
    virtual ~Transform() = default;
    std::optional<QPointF> push(QPointF p)
    {
        if (m_any && p.x() <= m_lastT)
            return std::nullopt;
        m_any = true;
        m_lastT = p.x();
        return step(p);
    }
    virtual void reset() { m_any = false; }

protected:
    virtual std::optional<QPointF> step(QPointF p) = 0;

private:
    bool m_any = false;
    double m_lastT = 0;
};

// 이름 문자열로 변환을 만든다. 모르는 이름이면 nullptr 과 error.
//   d/dt · d/dt(0.01)(고정 dt) · avg(5) · avg(5,center) · rms(5) · int · scale(100,0[,offsetX]) ·
//   abs · dt(앞 점과의 시간 차) · outlier(100)
std::unique_ptr<Transform> makeTransform(const QString &spec, QString *error = nullptr);

class DerivativeTransform : public Transform
{
public:
    explicit DerivativeTransform(double fixedDt = 0) : m_fixedDt(fixedDt) {}
    void reset() override { Transform::reset(); m_prev.reset(); }
protected:
    std::optional<QPointF> step(QPointF p) override;
private:
    double m_fixedDt;   // 0 이면 실제 dt
    std::optional<QPointF> m_prev;
};

class MovingAverageTransform : public Transform
{
public:
    MovingAverageTransform(int n, bool centered = false, bool rms = false) : m_n(std::max(1, n)), m_centered(centered), m_rms(rms) {}
    void reset() override { Transform::reset(); m_win.clear(); m_sum = 0; }
protected:
    std::optional<QPointF> step(QPointF p) override;
private:
    int m_n;
    bool m_centered, m_rms;
    std::deque<QPointF> m_win;
    double m_sum = 0;
};

class IntegralTransform : public Transform
{
public:
    void reset() override { Transform::reset(); m_prev.reset(); m_acc = 0; }
protected:
    std::optional<QPointF> step(QPointF p) override;
private:
    std::optional<QPointF> m_prev;
    double m_acc = 0;
};

class ScaleOffsetTransform : public Transform
{
public:
    ScaleOffsetTransform(double scale, double offsetY = 0, double offsetX = 0) : m_scale(scale), m_offY(offsetY), m_offX(offsetX) {}
protected:
    std::optional<QPointF> step(QPointF p) override { return QPointF(p.x() + m_offX, p.y() * m_scale + m_offY); }
private:
    double m_scale, m_offY, m_offX;
};

class AbsTransform : public Transform
{
protected:
    std::optional<QPointF> step(QPointF p) override { return QPointF(p.x(), std::abs(p.y())); }
};

class TimeSincePreviousTransform : public Transform
{
public:
    void reset() override { Transform::reset(); m_prev.reset(); }
protected:
    std::optional<QPointF> step(QPointF p) override;
private:
    std::optional<double> m_prev;
};

// PlotJuggler 방식: 4점 창에서 부호가 바뀌는 튐이 앞 변화량의 factor 배를 넘으면 버린다.
// 판정에 다음 점이 필요하므로 한 점 늦게 내보낸다.
class OutlierRemovalTransform : public Transform
{
public:
    explicit OutlierRemovalTransform(double factor = 100) : m_factor(factor) {}
    void reset() override { Transform::reset(); m_win.clear(); }
protected:
    std::optional<QPointF> step(QPointF p) override;
private:
    double m_factor;
    std::deque<QPointF> m_win;
};
