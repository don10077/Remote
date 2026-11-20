#include "zoneEditing.h"

#include "harmony/core/keyNames.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QLockFile>
#include <memory>
#include <vector>
#include <QHash>
#include <QJsonDocument>
#include <QJsonValue>

#include <algorithm>
#include <cmath>

namespace harmony::app::zoneEditing {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("zoneEditing", text); }

// ── Разбор текста JSON без построения дерева: только границы значений ────────

/// Значение в тексте: [begin, end).
struct Span {
    qsizetype begin = -1;
    qsizetype end = -1;
    bool operator==(const Span&) const = default;
};

/// Член объекта: ключ, начало ключа и границы значения.
struct Member {
    QString key;
    qsizetype keyBegin = -1; ///< позиция открывающей кавычки ключа
    Span value;
};

qsizetype skipSpace(const QByteArray& t, qsizetype i) {
    while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\r' || t[i] == '\n')) ++i;
    return i;
}

/// Позиция после строки, начинающейся с кавычки в `i`; -1 — строка не закрыта.
qsizetype skipString(const QByteArray& t, qsizetype i) {
    for (++i; i < t.size(); ++i) {
        if (t[i] == '\\') ++i;
        else if (t[i] == '"') return i + 1;
    }
    return -1;
}

/// Позиция после значения, начинающегося в `i`; -1 — текст оборван.
qsizetype skipValue(const QByteArray& t, qsizetype i) {
    if (i >= t.size()) return -1;
    if (t[i] == '"') return skipString(t, i);
    if (t[i] == '{' || t[i] == '[') {
        int depth = 0;
        while (i < t.size()) {
            const char c = t[i];
            if (c == '"') {
                i = skipString(t, i);
                if (i < 0) return -1;
                continue;
            }
            if (c == '{' || c == '[') ++depth;
            else if ((c == '}' || c == ']') && --depth == 0) return i + 1;
            ++i;
        }
        return -1;
    }
    static const QByteArray stops(",]} \t\r\n");
    while (i < t.size() && !stops.contains(t[i])) ++i;
    return i;
}

/// Элементы массива, начинающегося с `[` в `open`; nullopt — не массив или оборван.
std::optional<QList<Span>> elements(const QByteArray& t, qsizetype open) {
    if (open < 0 || open >= t.size() || t[open] != '[') return std::nullopt;
    QList<Span> result;
    qsizetype i = skipSpace(t, open + 1);
    if (i < t.size() && t[i] == ']') return result;
    while (i < t.size()) {
        const qsizetype end = skipValue(t, i);
        if (end < 0) return std::nullopt;
        result.append({i, end});
        i = skipSpace(t, end);
        if (i >= t.size()) return std::nullopt;
        if (t[i] == ']') return result;
        if (t[i] != ',') return std::nullopt;
        i = skipSpace(t, i + 1);
    }
    return std::nullopt;
}

/// Члены объекта, начинающегося с `{` в `open`; nullopt — не объект или оборван.
std::optional<QList<Member>> members(const QByteArray& t, qsizetype open) {
    if (open < 0 || open >= t.size() || t[open] != '{') return std::nullopt;
    QList<Member> result;
    qsizetype i = skipSpace(t, open + 1);
    if (i < t.size() && t[i] == '}') return result;
    while (i < t.size() && t[i] == '"') {
        const qsizetype keyBegin = i;
        const qsizetype keyEnd = skipString(t, i);
        if (keyEnd < 0) return std::nullopt;
        const auto key = QJsonDocument::fromJson("[" + t.mid(i, keyEnd - i) + "]").array().at(0).toString();
        i = skipSpace(t, keyEnd);
        if (i >= t.size() || t[i] != ':') return std::nullopt;
        i = skipSpace(t, i + 1);
        const qsizetype end = skipValue(t, i);
        if (end < 0) return std::nullopt;
        result.append({key, keyBegin, {i, end}});
        i = skipSpace(t, end);
        if (i >= t.size()) return std::nullopt;
        if (t[i] == '}') return result;
        if (t[i] != ',') return std::nullopt;
        i = skipSpace(t, i + 1);
    }
    return std::nullopt;
}

std::optional<Span> memberValue(const QByteArray& t, qsizetype open, const QString& key) {
    const auto list = members(t, open);
    if (!list) return std::nullopt;
    for (const auto& m : *list)
        if (m.key == key) return m.value;
    return std::nullopt;
}

/// Начало корневого значения (после BOM и пробелов).
qsizetype rootStart(const QByteArray& t) {
    return skipSpace(t, t.startsWith("\xEF\xBB\xBF") ? 3 : 0);
}

QByteArray newline(const QByteArray& t) { return t.contains("\r\n") ? QByteArray("\r\n") : QByteArray("\n"); }

/// Отступ строки, в которой стоит `pos`.
QByteArray lineIndent(const QByteArray& t, qsizetype pos) {
    qsizetype start = pos;
    while (start > 0 && t[start - 1] != '\n') --start;
    qsizetype end = start;
    while (end < t.size() && (t[end] == ' ' || t[end] == '\t')) ++end;
    return t.mid(start, end - start);
}

/// Объект экрана `screenId` в массиве `screens` remote.json; nullopt — такого экрана нет.
std::optional<Span> screenSpan(const QByteArray& t, const QString& screenId) {
    const auto screens = memberValue(t, rootStart(t), QStringLiteral("screens"));
    if (!screens) return std::nullopt;
    const auto list = elements(t, screens->begin);
    if (!list) return std::nullopt;
    for (const auto& span : *list) {
        const auto id = memberValue(t, span.begin, QStringLiteral("id"));
        if (!id) continue;
        const auto doc = QJsonDocument::fromJson("[" + t.mid(id->begin, id->end - id->begin) + "]");
        if (doc.array().at(0).toString() == screenId) return span;
    }
    return std::nullopt;
}

/// Массив зон в remote.json: `hardware.zones` при пустом `screenId`, иначе `cells` экрана `screenId`.
std::optional<Span> zonesSpan(const QByteArray& t, const QString& screenId) {
    if (screenId.isEmpty()) {
        const auto hardware = memberValue(t, rootStart(t), QStringLiteral("hardware"));
        if (!hardware) return std::nullopt;
        return memberValue(t, hardware->begin, QStringLiteral("zones"));
    }
    const auto screen = screenSpan(t, screenId);
    if (!screen) return std::nullopt;
    return memberValue(t, screen->begin, QStringLiteral("cells"));
}

/**
 * Новый текст массива `[...]` на месте `array` с элементами `lines`.
 * Отступы элементов и закрывающей скобки берутся из прежнего текста массива;
 * у пустого массива — от строки, где стоит `[`.
 */
QByteArray arrayText(const QByteArray& t, Span array, const QList<QByteArray>& lines) {
    if (lines.isEmpty()) return "[]";
    const QByteArray nl = newline(t);
    QByteArray head;
    QByteArray tail;
    const auto old = elements(t, array.begin);
    if (old && !old->isEmpty()) {
        head = t.mid(array.begin + 1, old->first().begin - array.begin - 1);
        tail = t.mid(old->last().end, array.end - 1 - old->last().end);
    }
    if (!head.contains('\n')) {
        const QByteArray indent = lineIndent(t, array.begin);
        head = nl + indent + "  ";
        tail = nl + indent;
    }
    const QByteArray indent = head.mid(head.lastIndexOf('\n') + 1);
    return "[" + head + lines.join("," + nl + indent) + tail + "]";
}

QByteArray number(qreal value) {
    const qreal rounded = round2(value);
    if (rounded == std::floor(rounded) && std::abs(rounded) < 1e9) return QByteArray::number(qint64(rounded));
    return QByteArray::number(rounded, 'g', 12);
}

QByteArray jsonValue(const QJsonValue& value) {
    if (value.isDouble()) return number(value.toDouble());
    if (value.isArray()) {
        QByteArrayList parts;
        for (const auto& item : value.toArray()) parts.append(jsonValue(item));
        return "[" + parts.join(", ") + "]";
    }
    if (value.isObject()) {
        // В одну строку, с пробелами, как зоны в файлах ресурсов: {"color": "#fff", "image": "…"}.
        const QJsonObject object = value.toObject();
        QByteArrayList parts;
        for (auto it = object.begin(); it != object.end(); ++it)
            parts.append(jsonValue(QJsonValue(it.key())) + ": " + jsonValue(it.value()));
        return "{" + parts.join(", ") + "}";
    }
    const auto compact = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return compact.mid(1, compact.size() - 2);
}

QByteArray rectText(const QRectF& r, bool multiline, const QByteArray& nl, const QByteArray& indent) {
    const QByteArrayList parts{number(r.x()), number(r.y()), number(r.width()), number(r.height())};
    if (!multiline) return "[" + parts.join(", ") + "]";
    return "[" + nl + indent + "  " + parts.join("," + nl + indent + "  ") + nl + indent + "]";
}

QByteArray replaced(const QByteArray& t, Span span, const QByteArray& value) {
    return t.left(span.begin) + value + t.mid(span.end);
}

/**
 * Дописать член `"key": value` последним в объект, начинающийся в `open`.
 * Отступ — как у последнего члена; у пустого объекта член ставится в ту же строку.
 */
QByteArray withMember(const QByteArray& t, qsizetype open, const QList<Member>& list, const QString& key,
                      const QByteArray& value) {
    const QByteArray member = jsonValue(QJsonValue(key)) + ": " + value;
    if (list.isEmpty()) return replaced(t, {open, skipValue(t, open)}, "{" + member + "}");
    const Member& last = list.last();
    // Многострочный объект — перевод строки между `{` и первым ключом; вложенные значения не в счёт.
    const bool multiline = t.mid(open, list.first().keyBegin - open).contains('\n');
    const QByteArray separator = multiline ? "," + newline(t) + lineIndent(t, last.keyBegin) : QByteArray(", ");
    return t.left(last.value.end) + separator + member + t.mid(last.value.end);
}

/// Убрать член номер `index` вместе с запятой; последний член оставляет пустой объект `{}`.
QByteArray withoutMember(const QByteArray& t, qsizetype open, const QList<Member>& list, qsizetype index) {
    if (list.size() == 1) return replaced(t, {open, skipValue(t, open)}, "{}");
    if (index > 0) return t.left(list[index - 1].value.end) + t.mid(list[index].value.end);
    return t.left(list[0].keyBegin) + t.mid(list[1].keyBegin);
}

/// Порядок ключей нового экрана: как в штатных файлах, ячейки последними.
const QStringList& screenKeyOrder() {
    static const QStringList order{"id", "role", "ref", "grid", "background", "cell", "panel",
                                   "centerLastRow", "compose", "layers", "cells"};
    return order;
}

/// Доля площади пересечения от меньшей из зон.
qreal overlapShare(const QRectF& a, const QRectF& b) {
    const QRectF common = a.intersected(b);
    if (common.isEmpty()) return 0;
    const qreal smaller = std::min(a.width() * a.height(), b.width() * b.height());
    return smaller > 0 ? common.width() * common.height() / smaller : 0;
}

/// Пересечения больше четверти меньшей зоны: мелкие нахлёсты в штатной разметке допустимы.
constexpr qreal OVERLAP_LIMIT = 0.25;

QString label(const QJsonObject& zone, int index) {
    const QString id = zone.value("id").toString();
    return id.isEmpty() ? tr("Zone %1").arg(index + 1) : id;
}

} // namespace

const QStringList& bodyKinds() {
    static const QStringList kinds{"Key", "System", "ScreenActs", "ScreenSoft", "PagePrev", "PageNext"};
    return kinds;
}

const QStringList& screenKinds() {
    static const QStringList kinds{"ScreenSlot", "ScreenKey", "ScreenSide", "ScreenLabel", "ScreenPanel"};
    return kinds;
}

bool isClickableScreenKind(const QString& kind) {
    return kind == "ScreenSlot" || kind == "ScreenKey" || kind == "ScreenSide";
}

qreal round2(qreal value) { return std::round(value * 100.0) / 100.0; }

QRectF rectOf(const QJsonObject& zone) {
    const auto a = zone.value("rect").toArray();
    if (a.size() != 4) return {};
    for (const auto& n : a)
        if (!n.isDouble() || !std::isfinite(n.toDouble())) return {};
    return {a[0].toDouble(), a[1].toDouble(), a[2].toDouble(), a[3].toDouble()};
}

void setRect(QJsonObject& zone, const QRectF& rect) {
    zone.insert("rect", QJsonArray{round2(rect.x()), round2(rect.y()), round2(rect.width()), round2(rect.height())});
}

QJsonArray autoZones(const QJsonArray& boxes, const QJsonArray& existing, const QString& base,
                     const std::optional<QRectF>& screen) {
    QJsonArray result;
    QSet<QString> taken;
    // Занятые места: зоны на экране редактора, рамка экрана и уже принятые рамки.
    QList<QRectF> occupied;
    for (const auto& value : existing) {
        const QJsonObject zone = value.toObject();
        taken.insert(zone.value(QStringLiteral("id")).toString());
        const QRectF rect = rectOf(zone);
        if (rect.width() > 0 && rect.height() > 0) occupied.append(rect);
    }
    if (screen && screen->width() > 0 && screen->height() > 0) occupied.append(*screen);
    for (const auto& value : boxes) {
        const QRectF rect = rectOf(QJsonObject{{QStringLiteral("rect"), value}});
        if (rect.width() <= 0 || rect.height() <= 0) continue;
        // Касание краями допустимо, любое пересечение по площади — нет.
        const bool overlapped = std::any_of(occupied.cbegin(), occupied.cend(),
                                            [&rect](const QRectF& other) { return overlapShare(rect, other) > 0; });
        if (overlapped) continue;
        occupied.append(rect);
        QString id = base;
        for (int n = 2; taken.contains(id); ++n) id = base + QString::number(n);
        taken.insert(id);
        QJsonObject zone;
        zone.insert(QStringLiteral("id"), id);
        setRect(zone, rect);
        const qreal minSide = qMin(rect.width(), rect.height());
        zone.insert(QStringLiteral("radius"), qAbs(rect.width() - rect.height()) <= 0.15 * minSide ? -0.5
                                                                                                     : round2(0.30 * minSide));
        zone.insert(QStringLiteral("kind"), QStringLiteral("Key"));
        result.append(zone);
    }
    return result;
}

QList<Issue> checkBody(const QJsonArray& zones, const QSizeF& photo, const std::optional<QRectF>& screen) {
    QList<Issue> issues;
    const QRectF bounds(QPointF(0, 0), photo);
    const auto error = [&issues](int zone, const QString& text) {
        issues.append({Issue::Level::Error, zone, text});
    };
    const auto warning = [&issues](int zone, const QString& text) {
        issues.append({Issue::Level::Warning, zone, text});
    };
    if (screen) {
        if (!screen->isValid()) error(-1, tr("Screen area: width and height must be positive"));
        else if (!bounds.contains(*screen)) error(-1, tr("Screen area goes beyond the photo"));
    }
    QHash<QString, int> seen;
    bool hasKey = false;
    for (int i = 0; i < zones.size(); ++i) {
        const auto zone = zones[i].toObject();
        const QString name = label(zone, i);
        const QString id = zone.value("id").toString();
        const QRectF r = rectOf(zone);
        const QString kind = zone.value("kind").toString();
        if (id.isEmpty()) error(i, tr("%1: id is empty").arg(name));
        else if (seen.contains(id)) error(i, tr("%1: the id is used twice").arg(name));
        else seen.insert(id, i);
        if (!r.isValid()) error(i, tr("%1: width and height must be positive").arg(name));
        else if (!bounds.contains(r)) error(i, tr("%1 goes beyond the photo").arg(name));
        if (!zone.value("radius").isDouble()) error(i, tr("%1: corner radius is missing").arg(name));
        else if (const qreal radius = zone.value("radius").toDouble();
                 r.isValid() && radius > std::min(r.width(), r.height()) / 2 + 0.5)
            warning(i, tr("%1: corner radius is larger than half the side").arg(name));
        if (!bodyKinds().contains(kind)) error(i, tr("%1: unknown kind \"%2\"").arg(name, kind));
        // Имя зоны Key берётся из общего списка имён клавиш (harmony/core/keyNames.h), а не
        // придумывается: иначе имя перестаёт совпадать с именами в конфигурации других пультов.
        if (kind == "Key" && !id.isEmpty() && !harmony::isKnownKeyName(id.toStdString()))
            warning(i, tr("%1: not in the shared key-name list, use a name from it").arg(name));
        hasKey = hasKey || kind == "Key";
        if (r.isValid() && screen && screen->isValid() && overlapShare(r, *screen) > OVERLAP_LIMIT)
            warning(i, tr("%1 covers the screen area").arg(name));
        for (int j = 0; j < i; ++j) {
            const QRectF other = rectOf(zones[j].toObject());
            if (r.isValid() && other.isValid() && overlapShare(r, other) > OVERLAP_LIMIT)
                warning(i, tr("%1 overlaps %2").arg(name, label(zones[j].toObject(), j)));
        }
    }
    if (!hasKey)
        warning(-1, tr("No Key zones: button assignment will be unavailable for this remote"));
    return issues;
}

QList<Issue> checkScreen(const QJsonArray& zones, const QSizeF& ref, const QSet<QString>& bodyIds,
                         const QSet<QString>& modes) {
    QList<Issue> issues;
    const QRectF bounds(QPointF(0, 0), ref);
    const auto error = [&issues](int zone, const QString& text) {
        issues.append({Issue::Level::Error, zone, text});
    };
    const auto warning = [&issues](int zone, const QString& text) {
        issues.append({Issue::Level::Warning, zone, text});
    };
    QSet<QString> seen;
    for (int i = 0; i < zones.size(); ++i) {
        const auto zone = zones[i].toObject();
        const QString name = label(zone, i);
        const QString id = zone.value("id").toString();
        const QRectF r = rectOf(zone);
        const QString kind = zone.value("kind").toString();
        if (id.isEmpty()) warning(i, tr("%1: id is empty").arg(name));
        else if (seen.contains(id)) warning(i, tr("%1: the id is used twice").arg(name));
        seen.insert(id);
        // Так же проверяет загрузка ресурсов: без корректного rect весь remote.json отвергается.
        if (!r.isValid()) error(i, tr("%1: width and height must be positive").arg(name));
        else if (!bounds.contains(r) && !(kind == "ScreenPanel" && zone.value("role") == "band"))
            warning(i, tr("%1 goes beyond the screen").arg(name));
        if (!screenKinds().contains(kind)) warning(i, tr("%1: unknown kind \"%2\"").arg(name, kind));
        if (const QString button = zone.value("button").toString(); !button.isEmpty() && !bodyIds.contains(button))
            warning(i, tr("%1: the remote has no button \"%2\"").arg(name, button));
        if (const QString target = zone.value("goto").toString(); !target.isEmpty() && !modes.contains(target))
            warning(i, tr("%1: this remote has no screen \"%2\", the key stays a picture").arg(name, target));
        if (!r.isValid() || !isClickableScreenKind(kind)) continue;
        for (int j = 0; j < i; ++j) {
            const auto other = zones[j].toObject();
            const QRectF o = rectOf(other);
            if (o.isValid() && isClickableScreenKind(other.value("kind").toString()) &&
                overlapShare(r, o) > OVERLAP_LIMIT)
                warning(i, tr("%1 overlaps %2").arg(name, label(other, j)));
        }
    }
    return issues;
}

QByteArray zoneLine(const QJsonObject& zone) {
    // Порядок ключей как в штатных файлах: действие до rect, button и role после kind.
    static const QStringList order{"id", "key", "goto", "page", "system", "rect", "radius", "kind", "button", "role"};
    QByteArrayList parts;
    QStringList keys = zone.keys();
    std::stable_sort(keys.begin(), keys.end(), [](const QString& a, const QString& b) {
        const auto rank = [](const QString& key) {
            const auto i = order.indexOf(key);
            return i < 0 ? order.size() : i;
        };
        return rank(a) < rank(b);
    });
    for (const QString& key : keys)
        parts.append(jsonValue(QJsonValue(key)) + ": " + jsonValue(zone.value(key)));
    return "{" + parts.join(", ") + "}";
}

std::optional<QList<QByteArray>> zoneTexts(const QByteArray& text, const QString& screenId) {
    const auto zones = zonesSpan(text, screenId);
    if (!zones) return std::nullopt;
    const auto list = elements(text, zones->begin);
    if (!list) return std::nullopt;
    QList<QByteArray> result;
    for (const auto& span : *list) result.append(text.mid(span.begin, span.end - span.begin));
    return result;
}

QJsonArray parseZones(const QByteArray& text) {
    QByteArray body = text.trimmed();
    if (body.endsWith(',')) body.chop(1);
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(body, &error);
    // Строки, скопированные из файла: «{…},\n{…}» — без скобок массива.
    if (error.error != QJsonParseError::NoError) doc = QJsonDocument::fromJson("[" + body + "]", &error);
    if (error.error != QJsonParseError::NoError) return {};
    const QJsonArray items = doc.isArray() ? doc.array() : QJsonArray{doc.object()};
    QJsonArray zones;
    for (const auto& item : items) {
        const auto zone = item.toObject();
        if (zone.contains("id") && rectOf(zone).isValid()) zones.append(zone);
    }
    return zones;
}

std::optional<QByteArray> patchZones(const QByteArray& text, const QString& screenId, const QList<QByteArray>& lines) {
    const auto zones = zonesSpan(text, screenId);
    if (!zones || text[zones->begin] != '[') return std::nullopt;
    return replaced(text, *zones, arrayText(text, *zones, lines));
}

std::optional<QByteArray> patchGrid(const QByteArray& text, const QString& screenId, int cols, int rows,
                                    std::optional<qreal> gap) {
    const auto screen = screenSpan(text, screenId);
    if (!screen) return std::nullopt;
    const auto grid = memberValue(text, screen->begin, QStringLiteral("grid"));
    if (!grid) return text; // у экрана нет сетки: файл не меняется
    // Остальные поля сетки — только cols, rows и gap; без нового зазора сохраняется прежний.
    const auto old = QJsonDocument::fromJson("[" + text.mid(grid->begin, grid->end - grid->begin) + "]")
                         .array().at(0).toObject();
    const bool hadGap = old.contains(QStringLiteral("gap"));
    QByteArray value = "{\"cols\": " + QByteArray::number(cols) + ", \"rows\": " + QByteArray::number(rows);
    if (gap && (hadGap || round2(*gap) != kDefaultGap)) value += ", \"gap\": " + number(*gap);
    else if (!gap && hadGap) value += ", \"gap\": " + number(old.value(QStringLiteral("gap")).toDouble());
    // Зона плиток пишется отдельно (patchValue), здесь она только сохраняется.
    if (old.contains(QStringLiteral("area"))) value += ", \"area\": " + jsonValue(old.value(QStringLiteral("area")));
    value += "}";
    return replaced(text, *grid, value);
}

std::optional<QByteArray> patchValue(const QByteArray& text, const QString& screenId, const QStringList& keys,
                                     const QJsonValue& value) {
    if (keys.isEmpty()) return std::nullopt;
    qsizetype open = rootStart(text);
    if (!screenId.isEmpty()) {
        const auto screen = screenSpan(text, screenId);
        if (!screen) return std::nullopt;
        open = screen->begin;
    }
    // Объекты на пути: начало объекта, его члены и номер нужного члена (-1 — ключа нет).
    struct Level {
        qsizetype open;
        QList<Member> list;
        qsizetype index;
    };
    QList<Level> path;
    for (qsizetype k = 0; k < keys.size(); ++k) {
        const auto list = members(text, open);
        if (!list) return std::nullopt;
        qsizetype index = -1;
        for (qsizetype i = 0; i < list->size(); ++i)
            if (list->at(i).key == keys[k]) index = i;
        path.append({open, *list, index});
        if (index < 0) break;
        if (k + 1 < keys.size()) open = list->at(index).value.begin;
    }
    const Level& last = path.last();
    const bool found = path.size() == keys.size() && last.index >= 0;

    if (value.isUndefined()) {
        if (!found) return text; // удалять нечего
        // Опустевший вложенный объект убирается вместе с ключом: {"panel": {"color": …}} → без "panel".
        qsizetype depth = path.size() - 1;
        while (depth > 0 && path[depth].list.size() == 1) --depth;
        return withoutMember(text, path[depth].open, path[depth].list, path[depth].index);
    }
    if (found) return replaced(text, last.list[last.index].value, jsonValue(value));
    // Ключа нет: дописать его в последний найденный объект, недостающие уровни — вложенными объектами.
    const qsizetype depth = path.size() - 1;
    QJsonValue nested = value;
    for (qsizetype j = keys.size() - 1; j > depth; --j) nested = QJsonObject{{keys[j], nested}};
    return withMember(text, last.open, last.list, keys[depth], jsonValue(nested));
}

std::optional<QByteArray> appendScreen(const QByteArray& text, const QJsonObject& screen) {
    const auto screens = memberValue(text, rootStart(text), QStringLiteral("screens"));
    if (!screens || text[screens->begin] != '[') return std::nullopt;
    const auto list = elements(text, screens->begin);
    if (!list) return std::nullopt;
    const QByteArray nl = newline(text);
    // Отступы файла: у элементов массива и шаг вложенности (у штатных файлов — один пробел).
    const QByteArray outer = lineIndent(text, screens->begin);
    const QByteArray element = list->isEmpty() ? outer + "  " : lineIndent(text, list->first().begin);
    const QByteArray step = element.startsWith(outer) && element.size() > outer.size() ? element.mid(outer.size())
                                                                                        : QByteArray("  ");
    const QByteArray inner = element + step;

    QStringList keys = screen.keys();
    std::stable_sort(keys.begin(), keys.end(), [](const QString& a, const QString& b) {
        const auto rank = [](const QString& key) {
            const auto i = screenKeyOrder().indexOf(key);
            return i < 0 ? screenKeyOrder().size() - 1 : i; // неизвестные ключи — перед ячейками
        };
        return rank(a) < rank(b);
    });
    QByteArrayList parts;
    for (const QString& key : keys) {
        const QJsonValue item = screen.value(key);
        QByteArray value;
        if (key == QLatin1String("cells") || key == QLatin1String("layers")) {
            // Ячейки и слои — по одной в строке, как в штатных файлах.
            QByteArrayList lines;
            for (const auto& entry : item.toArray()) lines.append(inner + step + zoneLine(entry.toObject()));
            value = lines.isEmpty() ? QByteArray("[]") : "[" + nl + lines.join("," + nl) + nl + inner + "]";
        } else {
            value = jsonValue(item);
        }
        parts.append(inner + jsonValue(QJsonValue(key)) + ": " + value);
    }
    const QByteArray object = "{" + nl + parts.join("," + nl) + nl + element + "}";
    if (list->isEmpty())
        return replaced(text, *screens, "[" + nl + element + object + nl + outer + "]");
    const qsizetype end = list->last().end;
    return text.left(end) + "," + nl + element + object + text.mid(end);
}

std::optional<QByteArray> removeScreen(const QByteArray& text, const QString& screenId) {
    const auto screens = memberValue(text, rootStart(text), QStringLiteral("screens"));
    if (!screens) return std::nullopt;
    const auto list = elements(text, screens->begin);
    const auto screen = screenSpan(text, screenId);
    if (!list || !screen) return std::nullopt;
    const qsizetype index = list->indexOf(*screen);
    if (index < 0) return std::nullopt;
    if (list->size() == 1) return replaced(text, *screens, "[]");
    // Вместе с запятой: у первого экрана — следующей за ним, у остальных — перед ним.
    if (index > 0) return text.left(list->at(index - 1).end) + text.mid(list->at(index).end);
    return text.left(list->at(0).begin) + text.mid(list->at(1).begin);
}

std::optional<QByteArray> patchDisplay(const QByteArray& text, const std::optional<QRectF>& rect) {
    const auto display = memberValue(text, rootStart(text), QStringLiteral("display"));
    const auto value = display ? memberValue(text, display->begin, QStringLiteral("rect")) : std::nullopt;
    if (!value) {
        // В заготовке (createStub) display ещё нет: создаём display.rect, а при снятой рамке писать нечего.
        if (!rect) return text;
        return patchValue(text, {}, {QStringLiteral("display"), QStringLiteral("rect")},
                          QJsonArray{rect->x(), rect->y(), rect->width(), rect->height()});
    }
    if (!rect) return replaced(text, *value, "null");
    // Многострочный вид, как у прежнего значения; у `null` — однострочный.
    const bool multiline = text.mid(value->begin, value->end - value->begin).contains('\n');
    return replaced(text, *value, rectText(*rect, multiline, newline(text), lineIndent(text, value->begin)));
}


QString saveFiles(const QString& root, const QHash<QString, QByteArray>& expected,
                  const QHash<QString, QByteArray>& updates) {
    QLockFile lock(QDir(root).filePath(".zone-editor.lock"));
    if (!lock.tryLock(0)) return tr("Another editor is saving resources. Try again shortly.");
    const auto unchanged = [&]() -> QString {
        for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
            QFile file(QDir(root).filePath(it.key()));
            if (!file.open(QIODevice::ReadOnly) || file.readAll() != it.value())
                return tr("The file %1 changed outside the editor. Nothing was saved.").arg(it.key());
        }
        return {};
    };
    if (const QString error = unchanged(); !error.isEmpty()) return error;
    auto paths = updates.keys();
    paths.sort();
    for (const auto& path : paths)
        if (!expected.contains(path) || QDir::isAbsolutePath(path) || QDir::cleanPath(path).startsWith("../"))
            return tr("Cannot save an unknown resource: %1").arg(path);
    QTemporaryDir backup(QDir(root).filePath(".zone-editor-backup-XXXXXX"));
    if (!backup.isValid()) return tr("Could not create a backup folder. Nothing was saved.");
    std::vector<std::unique_ptr<QSaveFile>> staged;
    for (const auto& path : paths) {
        const auto backupPath = QDir(backup.path()).filePath(path);
        if (!QDir().mkpath(QFileInfo(backupPath).path())) return tr("Could not create a backup folder. Nothing was saved.");
        QFile copy(backupPath);
        if (!copy.open(QIODevice::WriteOnly) || copy.write(expected.value(path)) != expected.value(path).size() || !copy.flush())
            return tr("Could not back up %1. Nothing was saved.").arg(path);
        auto file = std::make_unique<QSaveFile>(QDir(root).filePath(path));
        if (!file->open(QIODevice::WriteOnly) || file->write(updates.value(path)) != updates.value(path).size())
            return tr("Could not prepare %1 for saving: %2. Nothing was saved.").arg(path, file->errorString());
        staged.push_back(std::move(file));
    }
    if (const QString error = unchanged(); !error.isEmpty()) return error;
    for (std::size_t i = 0; i < staged.size(); ++i) {
        if (staged[i]->commit()) continue;
        bool restored = true;
        for (std::size_t j = 0; j < i; ++j) {
            QSaveFile file(QDir(root).filePath(paths[qsizetype(j)]));
            const auto bytes = expected.value(paths[qsizetype(j)]);
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) restored = false;
        }
        if (!restored) {
            backup.setAutoRemove(false);
            return tr("Saving failed. Original files are available in %1.").arg(backup.path());
        }
        return tr("Saving failed for %1. Previous files were restored; your edits remain in the editor.").arg(paths[qsizetype(i)]);
    }
    return {};
}

} // namespace harmony::app::zoneEditing
