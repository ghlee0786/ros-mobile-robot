#include "messagepath.h"

#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <functional>

namespace {

constexpr double kPi = 3.14159265358979323846;
const QStringList kModifiers = {"derivative", "abs", "negative", "rad2deg", "deg2rad", "yaw", "roll", "pitch"};

bool identStart(QChar c) { return c.isLetter() || c == '_'; }
bool identChar(QChar c) { return c.isLetterOrNumber() || c == '_'; }

QJsonValue follow(QJsonValue v, const QStringList &fields, int from = 0)
{
    for (int i = from; i < fields.size(); ++i)
        v = v.toObject().value(fields[i]);
    return v;
}

bool matches(const QJsonValue &v, const MessagePath::Filter &f)
{
    using Op = MessagePath::Op;
    int c;
    if (f.isString) {
        if (!v.isString())
            return false;
        c = QString::compare(v.toString(), f.str);
    } else {
        if (!v.isDouble() && !v.isBool())
            return false;
        const double d = v.isBool() ? (v.toBool() ? 1 : 0) : v.toDouble();
        c = d < f.num ? -1 : (d > f.num ? 1 : 0);
    }
    switch (f.op) {
    case Op::Eq: return c == 0;
    case Op::Ne: return c != 0;
    case Op::Lt: return c < 0;
    case Op::Le: return c <= 0;
    case Op::Gt: return c > 0;
    case Op::Ge: return c >= 0;
    }
    return false;
}

// 쿼터니언(x,y,z,w) → roll/pitch/yaw [rad]
double quatAngle(const QJsonObject &q, const QString &which)
{
    const double x = q["x"].toDouble(), y = q["y"].toDouble(), z = q["z"].toDouble(), w = q["w"].toDouble();
    if (which == "roll")
        return std::atan2(2 * (w * x + y * z), 1 - 2 * (x * x + y * y));
    if (which == "pitch")
        return std::asin(std::clamp(2 * (w * y - z * x), -1.0, 1.0));
    return std::atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z));
}

} // namespace

bool MessagePath::fail(int pos, const QString &why)
{
    m_ok = false;
    m_errorPos = pos;
    m_error = why;
    return false;
}

bool MessagePath::parse(const QString &text)
{
    *this = MessagePath();
    const int n = text.size();
    int p = 0;
    auto at = [&](int i) { return i < n ? text[i] : QChar(); };
    auto skipWs = [&] { while (p < n && text[p].isSpace()) ++p; };
    auto ident = [&] {
        const int s = p;
        if (p < n && identStart(text[p]))
            while (p < n && identChar(text[p])) ++p;
        return text.mid(s, p - s);
    };
    auto integer = [&](int &out) {
        const int s = p;
        if (at(p) == '-') ++p;
        while (p < n && text[p].isDigit()) ++p;
        bool ok = false;
        out = text.mid(s, p - s).toInt(&ok);
        if (!ok) p = s;
        return ok;
    };
    // field (. field)* op value
    auto filterExpr = [&](Filter &f) {
        skipWs();
        for (;;) {
            const int s = p;
            const QString id = ident();
            if (id.isEmpty())
                return fail(s, QStringLiteral("필터에 필드 이름이 필요합니다"));
            f.field << id;
            if (at(p) != '.') break;
            ++p;
        }
        skipWs();
        static const QList<QPair<QString, Op>> ops = {{"==", Op::Eq}, {"!=", Op::Ne}, {"<=", Op::Le},
                                                       {">=", Op::Ge}, {"<", Op::Lt}, {">", Op::Gt}};
        bool found = false;
        for (const auto &o : ops)
            if (text.mid(p, o.first.size()) == o.first) {
                f.op = o.second;
                p += o.first.size();
                found = true;
                break;
            }
        if (!found)
            return fail(p, QStringLiteral("비교 연산자(== != < <= > >=)가 필요합니다"));
        skipWs();
        if (at(p) == '"' || at(p) == '\'') {
            const QChar q = text[p];
            const int close = text.indexOf(q, p + 1);
            if (close < 0)
                return fail(p, QStringLiteral("문자열이 닫히지 않았습니다"));
            f.isString = true;
            f.str = text.mid(p + 1, close - p - 1);
            p = close + 1;
        } else {
            const int s = p;
            if (at(p) == '-' || at(p) == '+') ++p;
            while (p < n && (text[p].isDigit() || text[p] == '.' || text[p] == 'e' || text[p] == 'E'
                             || ((text[p] == '-' || text[p] == '+') && (text[p - 1] == 'e' || text[p - 1] == 'E'))))
                ++p;
            bool ok = false;
            f.num = text.mid(s, p - s).toDouble(&ok);
            if (!ok)
                return fail(s, QStringLiteral("숫자나 따옴표 문자열이 필요합니다"));
        }
        skipWs();
        return true;
    };

    if (at(0) != '/')
        return fail(0, QStringLiteral("토픽은 '/' 로 시작해야 합니다"));
    p = 1;
    while (p < n && (identChar(text[p]) || text[p] == '/')) ++p;
    m_topic = text.left(p);
    if (m_topic.size() < 2)
        return fail(1, QStringLiteral("토픽 이름이 필요합니다"));

    while (p < n) {
        const QChar c = text[p];
        if (c == '.') {
            ++p;
            if (at(p) == '@') {
                ++p;
                const int s = p;
                const QString m = ident();
                if (!kModifiers.contains(m))
                    return fail(s, QStringLiteral("알 수 없는 모디파이어 '@%1'").arg(m));
                if (p < n)
                    return fail(p, QStringLiteral("모디파이어 뒤에는 아무것도 올 수 없습니다"));
                m_modifier = m;
                break;
            }
            Part part;
            part.name = ident();
            if (part.name.isEmpty())
                return fail(p, QStringLiteral("필드 이름이 필요합니다"));
            m_parts << part;
        } else if (c == '[') {
            ++p;
            skipWs();
            Part part;
            if (identStart(at(p))) {
                part.kind = Part::ParallelFilter;
                if (!filterExpr(part.filter))
                    return false;
            } else {
                part.kind = Part::Index;
                part.hasStart = integer(part.index);
                skipWs();
                if (at(p) == ':') {
                    ++p;
                    skipWs();
                    part.kind = Part::Slice;
                    part.hasEnd = integer(part.end);
                    skipWs();
                } else if (!part.hasStart) {
                    return fail(p, QStringLiteral("인덱스 · 슬라이스 · 필터가 필요합니다"));
                }
            }
            if (at(p) != ']')
                return fail(p, QStringLiteral("']' 가 필요합니다"));
            ++p;
            m_parts << part;
        } else if (c == '{') {
            ++p;
            Part part;
            part.kind = Part::Filter;
            if (!filterExpr(part.filter))
                return false;
            if (at(p) != '}')
                return fail(p, QStringLiteral("'}' 가 필요합니다"));
            ++p;
            m_parts << part;
        } else {
            return fail(p, QStringLiteral("예상하지 못한 문자 '%1'").arg(c));
        }
    }
    m_ok = true;
    return true;
}

QList<double> MessagePath::evaluate(const QJsonObject &msg) const
{
    QList<double> out;
    if (!m_ok)
        return out;
    struct Node { QJsonValue v; QJsonObject parent; };
    QList<Node> cur{{msg, {}}};
    for (const Part &part : m_parts) {
        QList<Node> next;
        for (const Node &nd : std::as_const(cur)) {
            const QJsonArray a = nd.v.toArray();
            const int size = a.size();
            switch (part.kind) {
            case Part::Field:
                if (nd.v.isObject() && nd.v.toObject().contains(part.name))
                    next.append({nd.v.toObject().value(part.name), nd.v.toObject()});
                break;
            case Part::Index: {
                const int i = part.index < 0 ? size + part.index : part.index;
                if (nd.v.isArray() && i >= 0 && i < size)
                    next.append({a[i], nd.parent});
                break;
            }
            case Part::Slice: {
                int s = part.hasStart ? (part.index < 0 ? size + part.index : part.index) : 0;
                int e = part.hasEnd ? (part.end < 0 ? size + part.end : part.end) : size - 1;
                for (int i = std::max(s, 0); i <= std::min(e, size - 1); ++i)
                    next.append({a[i], nd.parent});
                break;
            }
            case Part::Filter:
                if (nd.v.isArray()) {
                    for (const QJsonValue &e : a)
                        if (matches(follow(e, part.filter.field), part.filter))
                            next.append({e, nd.parent});
                } else if (matches(follow(nd.v, part.filter.field), part.filter)) {
                    next.append(nd);
                }
                break;
            case Part::ParallelFilter: {
                const QJsonArray keys = nd.parent.value(part.filter.field.first()).toArray();
                for (int i = 0; i < std::min(size, int(keys.size())); ++i)
                    if (matches(follow(keys[i], part.filter.field, 1), part.filter))
                        next.append({a[i], nd.parent});
                break;
            }
            }
        }
        cur = next;
    }

    const bool rpy = m_modifier == "yaw" || m_modifier == "roll" || m_modifier == "pitch";
    auto add = [&](const QJsonValue &v) {
        if (v.isDouble())
            out << v.toDouble();
        else if (v.isBool())
            out << (v.toBool() ? 1.0 : 0.0);
    };
    for (const Node &nd : std::as_const(cur)) {
        if (rpy) {
            if (nd.v.isObject())
                out << quatAngle(nd.v.toObject(), m_modifier);
        } else if (nd.v.isArray()) {
            for (const QJsonValue &e : nd.v.toArray())
                add(e);
        } else {
            add(nd.v);
        }
    }
    for (double &d : out) {
        if (m_modifier == "abs") d = std::abs(d);
        else if (m_modifier == "negative") d = -d;
        else if (m_modifier == "rad2deg") d = d * 180.0 / kPi;
        else if (m_modifier == "deg2rad") d = d * kPi / 180.0;
    }
    return out;
}

int fuzzyScore(const QString &query, const QString &text)
{
    int score = 0, ti = 0, prev = -2;
    for (const QChar qc : query) {
        if (qc.isSpace())
            continue;
        const QChar lq = qc.toLower();
        while (ti < text.size() && text[ti].toLower() != lq) ++ti;
        if (ti == text.size())
            return -1;
        int s = 1;
        if (ti == prev + 1) s += 5;                                       // 연속
        if (ti == 0 || QStringLiteral("/._[ ").contains(text[ti - 1])) s += 3; // 단어 시작
        score += s;
        prev = ti++;
    }
    return std::max(0, score * 10 - int(text.size()));
}

QStringList flattenNumericPaths(const QString &topic, const QJsonObject &msg, int maxArray)
{
    QStringList out;
    std::function<void(const QString &, const QJsonValue &)> walk = [&](const QString &prefix, const QJsonValue &v) {
        if (v.isObject()) {
            const QJsonObject o = v.toObject();
            for (auto it = o.begin(); it != o.end(); ++it)
                walk(prefix + '.' + it.key(), it.value());
            if (o.size() == 4 && o.contains("x") && o.contains("y") && o.contains("z") && o.contains("w"))
                out << prefix + ".@roll" << prefix + ".@pitch" << prefix + ".@yaw";
        } else if (v.isArray()) {
            const QJsonArray a = v.toArray();
            for (int i = 0; i < std::min(int(a.size()), maxArray); ++i)
                walk(prefix + QStringLiteral("[%1]").arg(i), a[i]);
        } else if (v.isDouble() || v.isBool()) {
            out << prefix;
        }
    };
    walk(topic, msg);
    return out;
}
