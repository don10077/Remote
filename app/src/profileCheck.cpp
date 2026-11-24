/**
 * @file profileCheck.cpp
 * @brief C2: места экранных кнопок по профилю модели.
 */

#include "profileCheck.h"
#include "widgets/zoneEditorShared.h"

#include <QString>
#include <algorithm>
#include <vector>

namespace harmony::app {

namespace {

/// Число мест экрана: только ячейки ScreenSlot, панели не участвуют (правило V6).
int screenSlots(const QJsonObject& screen) {
    int count = 0;
    for (const auto& value : screen.value("cells").toArray()) {
        if (value.toObject().value("kind").toString() == QLatin1String("ScreenSlot")) ++count;
    }
    return count;
}

/// Экран профиля с такой ролью; пустой объект — экрана этого режима нет.
QJsonObject screenOf(const QJsonObject& remote, const QString& role) {
    for (const auto& value : remote.value("screens").toArray()) {
        const auto screen = value.toObject();
        if (screen.value("role").toString() == role) return screen;
    }
    return {};
}

/// Роль экрана того режима, где показываются кнопки этого владельца (V6).
QString ownerRole(const Problem& problem) {
    switch (problem.where) {
    case Problem::Where::Activity: return QStringLiteral("activity");
    case Problem::Where::Device:   return QStringLiteral("devices");
    default: return QString();
    }
}

bool isScreenRole(const QString& role) {
    return std::ranges::any_of(zoneEditorDetail::kScreenRoles,
                               [&role](const auto& entry) { return QString::fromUtf8(entry.role) == role; });
}

} // namespace

int commandSlots(const QJsonObject& remote, const QString& role) {
    return screenSlots(screenOf(remote, role));
}

void profileCheck(std::vector<Problem>& problems, const QJsonObject& remote) {
    if (remote.isEmpty()) return;

    // 1. Проблемы профиля: экран режима и его места.
    for (const auto& value : remote.value("screens").toArray()) {
        const auto screen = value.toObject();
        const QString role = screen.value("role").toString();
        if (!isScreenRole(role)) continue;  // экран, где экранных кнопок нет
        const int cells = screenSlots(screen);
        const auto grid = screen.value("grid").toObject();
        const int places = grid.value("cols").toInt() * grid.value("rows").toInt();
        Problem problem;
        problem.where = Problem::Where::Profile;
        problem.id = remote.value("id").toString().toStdString();
        problem.place = say("profile: %1", {role.toStdString()});
        if (cells == 0) {
            problem.what = say("profile: %1 has no command slots", {role.toStdString()});
            problems.push_back(std::move(problem));
            continue;
        }
        if (places > 0 && cells != places) {
            problem.what = say("profile: %1 describes %2 cells, the grid has %3 places",
                               {role.toStdString(), std::to_string(cells), std::to_string(places)});
            problems.push_back(std::move(problem));
        }
    }

    // 2. Подсказка страницы и места — по экрану того режима, где показываются кнопки владельца.
    for (Problem& problem : problems) {
        if (problem.softPosition < 0) continue;
        const int capacity = commandSlots(remote, ownerRole(problem));
        if (capacity == 0) continue;
        problem.hint = say("page %1, place %2",
                           {std::to_string(problem.softPosition / capacity + 1),
                            std::to_string(problem.softPosition % capacity + 1)});
    }
}

} // namespace harmony::app
