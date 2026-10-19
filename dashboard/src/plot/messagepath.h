#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

// Lichtblick message path 문법의 부분집합.
//   /topic.field.sub[3].x      필드 · 인덱스(음수는 뒤에서)
//   /topic.arr[1:-1].y         슬라이스(양끝 포함, 빈 쪽은 처음 · 끝)
//   /topic.arr{name=="left"}.v 필터(== != < <= > >=, 숫자 · 문자열). 객체에 붙이면 맞을 때만 통과
//   /topic.velocity[name=="r"] 병렬 배열 필터: 형제 배열 name 에서 맞는 인덱스의 값을 쓴다(JointState)
//   ....@abs                   끝에 모디파이어 하나: derivative abs negative rad2deg deg2rad yaw roll pitch
class MessagePath
{
public:
    enum class Op { Eq, Ne, Lt, Le, Gt, Ge };
    struct Filter {
        QStringList field;  // a.b → {"a","b"}
        Op op = Op::Eq;
        bool isString = false;
        QString str;
        double num = 0;
    };
    struct Part {
        enum Kind { Field, Index, Slice, Filter, ParallelFilter } kind = Field;
        QString name;           // Field
        int index = 0;          // Index · Slice 시작
        int end = -1;           // Slice 끝(포함)
        bool hasStart = false, hasEnd = false;
        MessagePath::Filter filter;
    };

    bool parse(const QString &text);
    bool ok() const { return m_ok; }
    int errorPos() const { return m_errorPos; }
    QString error() const { return m_error; }
    QString topic() const { return m_topic; }
    const QList<Part> &parts() const { return m_parts; }
    QString modifier() const { return m_modifier; }

    // 메시지에서 숫자들을 꺼낸다(배열 · 슬라이스면 여러 개). @derivative 는 시간이 필요하므로
    // 여기서는 적용하지 않는다 — 부르는 쪽(PlotSeries)이 미분 변환으로 처리한다.
    QList<double> evaluate(const QJsonObject &msg) const;

private:
    bool fail(int pos, const QString &why);

    bool m_ok = false;
    int m_errorPos = -1;
    QString m_error;
    QString m_topic;
    QList<Part> m_parts;
    QString m_modifier;
};

// 하위 순서(subsequence) 퍼지 매칭. 맞지 않으면 -1, 클수록 잘 맞는다(연속 · 단어 시작 · 앞부분 가점).
int fuzzyScore(const QString &query, const QString &text);

// 메시지 하나를 숫자 값 경로 목록으로 펼친다("/topic.a.b", "/topic.arr[0]"). 배열은 maxArray 개까지.
QStringList flattenNumericPaths(const QString &topic, const QJsonObject &msg, int maxArray = 32);
