/**
 * @file profileCheckTest.cpp
 * @brief C2, этап А: места экранных кнопок — только при профиле.
 *
 * Здесь только ожидания (правило 2 [00-INDEX.md]), кода шага ещё нет. Красные — решения,
 * где появляется новое поведение; зелёные — факты, которые красными быть не могут
 * (перечислены в отчёте по шагу).
 *
 * Решения шага:
 * 1. Ядро сообщает только то, что видно в конфигурации (`Position`). Страницу считает
 *    приложение, потому что места задаёт профиль, содержимого которого ядро не видит.
 * 2. Проблемы профиля — отдельная группа `Problem::Where::Profile`: слоты для команд
 *    не заданы, и число ячеек экрана не равно сеткой. В окне проверки — в конце списка.
 * 3. Подсказка «страница N, место M» — поле `Problem::hint`, которое заполняет
 *    приложение по экрану того режима, где показываются кнопки владельца (V6):
 *    активность → роль `activity`, устройство → роль `devices`. Страница и место —
 *    с единицы, как остальные места в проверке.
 * 4. Профиль противоречит сам себе — предупреждение с обеими величинами. Кнопок больше,
 *    чем мест на странице, — это страницы, а не противоречие; пропуск места — норма.
 * 5. Ячейки-панели (`ScreenPanel`) в счёт мест не участвуют; экран, где экранных кнопок
 *    нет (роль не из `kScreenRoles`), не проверяется.
 *
 * Тексты — шаблоны `say()` (контекст `core` через `labels::errorText`), список строк —
 * в отчёте шага.
 */

#include "configFixtures.h"

#include "../app/src/profileCheck.h"
#include "../app/src/labels.h"
#include "../app/src/widgets/checkDialog.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTreeWidget>
#include <QPushButton>
#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

namespace app = harmony::app;

/// Экран профиля: роль, сетка и ячейки, как в файле профиля.
QJsonObject screen(const QString& role, int cols, int rows, const QJsonArray& cells) {
    QJsonObject object{{"id", role}, {"role", role}, {"ref", QJsonArray{172, 182}}};
    if (cols > 0 && rows > 0) object.insert("grid", QJsonObject{{"cols", cols}, {"rows", rows}});
    object.insert("cells", cells);
    return object;
}

QJsonObject slot(const QString& name) {
    return QJsonObject{{"id", name}, {"kind", "ScreenSlot"}, {"radius", 0.0},
                       {"rect", QJsonArray{0, 0, 20, 20}}};
}

QJsonObject text(const QString& name) {
    return QJsonObject{{"id", name}, {"kind", "ScreenLabel"}, {"radius", 0.0},
                       {"rect", QJsonArray{0, 0, 60, 20}}};
}

QJsonObject band(const QString& name) {
    return QJsonObject{{"id", name}, {"kind", "ScreenPanel"}, {"role", "band"}, {"radius", 0.0},
                       {"rect", QJsonArray{0, 0, 80, 30}}};
}

/// Профиль модели: id, name и экраны. `hardware.zones` пустой — проверка экранов этого не требует.
QJsonObject profile(const QString& id, const QJsonArray& screens) {
    return QJsonObject{{"id", id}, {"name", "Harmony Profile"},
                       {"hardware", QJsonObject{{"zones", QJsonArray{}}}},
                       {"screens", screens}};
}

/// Результат проверки ядра плюс проблемы профиля (C2) — так их собирает приложение.
std::vector<harmony::Problem> checkWithProfile(const harmony::ConfigDocument& document,
                                               const QJsonObject& remote) {
    auto problems = document.check();
    app::profileCheck(problems, remote);
    return problems;
}

/// Дубликат места у активности: две кнопки на `Position` 4.
harmony::ConfigDocument duplicateActivity() {
    const std::string xml = harmony::test::replaceIn(
        harmony::test::kConfigXml, "Activity", harmony::test::kActivityId,
        "<Button><Label>Громкость</Label><Position>4</Position><ActionId>200_VolumeUp_Hold</ActionId></Button>",
        "<Button><Label>Громкость</Label><Position>4</Position><ActionId>200_VolumeUp_Hold</ActionId></Button>"
        "<Button><Label>Плей</Label><Position>4</Position><ActionId>100_Aspect_Press</ActionId></Button>");
    return harmony::test::openFixture(xml);
}

// 1. Без профиля — ничего нового (факт, красным быть не может).
TEST(ProfileCheck, WithoutProfileAddsNothing) {
    const auto document = harmony::test::configFixture();
    const auto base = document.check();
    EXPECT_TRUE(base.empty());  // фикстура без проблем: видна только работа профиля
    const auto problems = checkWithProfile(document, QJsonObject{});
    EXPECT_EQ(problems.size(), base.size());
}

// 2. Экран режима есть, ячеек ScreenSlot нет — проблема профиля (красное).
TEST(ProfileCheck, ScreenWithoutCommandSlotsWarnsInProfileGroup) {
    const auto document = harmony::test::configFixture();
    const auto remote = profile("900", QJsonArray{screen("command", 0, 0, QJsonArray{text("Title")})});
    const auto problems = checkWithProfile(document, remote);
    ASSERT_EQ(problems.size(), 1U);
    const harmony::Problem& problem = problems.front();
    EXPECT_EQ(problem.severity, harmony::Problem::Severity::Warning);
    EXPECT_EQ(problem.where, harmony::Problem::Where::Profile);
    EXPECT_EQ(problem.id, "900");
    EXPECT_EQ(problem.place.args, (std::vector<std::string>{"command"}));
    EXPECT_EQ(problem.fix, harmony::Problem::Fix::None);
    EXPECT_EQ(problem.what.format, "profile: %1 has no command slots");
    EXPECT_EQ(problem.what.args, (std::vector<std::string>{"command"}));
}

// 3. Ячеек не равно сетке — предупреждение с обеими величинами (красное).
TEST(ProfileCheck, CellsAndGridDisagreeWarnsInProfileGroup) {
    const auto document = harmony::test::configFixture();
    const QJsonArray cells{slot("1"), slot("2"), slot("3"), slot("4"), slot("5"), slot("6"), slot("7")};
    const auto remote = profile("1100", QJsonArray{screen("command", 2, 3, cells)});
    const auto problems = checkWithProfile(document, remote);
    ASSERT_EQ(problems.size(), 1U);
    const harmony::Problem& problem = problems.front();
    EXPECT_EQ(problem.where, harmony::Problem::Where::Profile);
    EXPECT_EQ(problem.what.format, "profile: %1 describes %2 cells, the grid has %3 places");
    EXPECT_EQ(problem.what.args, (std::vector<std::string>{"command", "7", "6"}));
}

// 4. Панели в счёт мест не участвуют: шесть ScreenSlot и сетка 2×3 — противоречия нет.
TEST(ProfileCheck, PanelsDoNotCountAsCommandSlots) {
    const auto document = harmony::test::configFixture();
    const QJsonArray cells{slot("1"), slot("2"), slot("3"), slot("4"), slot("5"), slot("6"),
                            band("Band"), text("Title")};
    const auto remote = profile("1100", QJsonArray{screen("command", 2, 3, cells)});
    const auto base = document.check();
    const auto problems = checkWithProfile(document, remote);
    EXPECT_EQ(problems.size(), base.size());
    EXPECT_TRUE(std::ranges::none_of(problems, [](const harmony::Problem& p) {
        return p.where == harmony::Problem::Where::Profile;
    }));
}

// 5. Экран без экранных кнопок (роль не из kScreenRoles) не проверяется.
TEST(ProfileCheck, ScreenWithoutScreenButtonsIsIgnored) {
    const auto document = harmony::test::configFixture();
    const auto remote = profile("1100", QJsonArray{screen("home", 2, 3, QJsonArray{text("Title")})});
    const auto base = document.check();
    const auto problems = checkWithProfile(document, remote);
    EXPECT_EQ(problems.size(), base.size());
    EXPECT_TRUE(std::ranges::none_of(problems, [](const harmony::Problem& p) {
        return p.where == harmony::Problem::Where::Profile;
    }));
}

// 6. Дубликат места: страница и место — по экрану режима активности (красное).
TEST(ProfileCheck, DuplicatePlaceHintUsesActivityScreen) {
    const auto document = duplicateActivity();
    const QJsonArray cells{slot("1"), slot("2"), slot("3"), slot("4")};
    const auto remote = profile("900", QJsonArray{screen("activity", 2, 2, cells)});
    const auto problems = checkWithProfile(document, remote);
    ASSERT_EQ(problems.size(), 1U);
    const harmony::Problem& problem = problems.front();
    EXPECT_EQ(problem.where, harmony::Problem::Where::Activity);
    EXPECT_EQ(problem.fix, harmony::Problem::Fix::MoveSoftButton);
    EXPECT_EQ(problem.softPosition, 4);
    EXPECT_EQ(problem.hint.format, "page %1, place %2");
    EXPECT_EQ(problem.hint.args, (std::vector<std::string>{"2", "1"}));
}

// 7. Тот же дубликат на экране с шестью местами: Position 13 — третья страница, второе место.
TEST(ProfileCheck, HintCountsPagesByScreenCapacity) {
    const std::string xml = harmony::test::replaceIn(
        harmony::test::kConfigXml, "Activity", harmony::test::kActivityId,
        "<Button><Label>Громкость</Label><Position>4</Position><ActionId>200_VolumeUp_Hold</ActionId></Button>",
        "<Button><Label>Громкость</Label><Position>13</Position><ActionId>200_VolumeUp_Hold</ActionId></Button>"
        "<Button><Label>Громкость</Label><Position>13</Position><ActionId>200_VolumeUp_Hold</ActionId></Button>");
    const auto document = harmony::test::openFixture(xml);
    const QJsonArray cells{slot("1"), slot("2"), slot("3"), slot("4"), slot("5"), slot("6")};
    const auto remote = profile("1100", QJsonArray{screen("activity", 2, 3, cells)});
    const auto problems = checkWithProfile(document, remote);
    ASSERT_EQ(problems.size(), 1U);
    EXPECT_EQ(problems.front().softPosition, 13);
    EXPECT_EQ(problems.front().hint.args, (std::vector<std::string>{"3", "2"}));
}

// 8. Устройство — по экрану своей роли (красное).
TEST(ProfileCheck, DeviceHintUsesDevicesScreen) {
    const std::string xml = harmony::test::replaceIn(
        harmony::test::kConfigXml, "Device", harmony::test::kTvId,
        "<Button><Label>Текст</Label><Position>2</Position><ActionId>100_Text_Press</ActionId></Button>",
        "<Button><Label>Текст</Label><Position>2</Position><ActionId>100_Text_Press</ActionId></Button>"
        "<Button><Label>Ракурс</Label><Position>2</Position><ActionId>100_Aspect_Press</ActionId></Button>");
    const auto document = harmony::test::openFixture(xml);
    const QJsonArray cells{slot("1"), slot("2"), slot("3"), slot("4"), slot("5"), slot("6")};
    const auto remote = profile("1100", QJsonArray{screen("devices", 3, 2, cells)});
    const auto problems = checkWithProfile(document, remote);
    ASSERT_EQ(problems.size(), 1U);
    const harmony::Problem& problem = problems.front();
    EXPECT_EQ(problem.where, harmony::Problem::Where::Device);
    EXPECT_EQ(problem.hint.args, (std::vector<std::string>{"1", "3"}));
}

// 9. Экрана этого режима в профиле нет — подсказки нет (красное: проблема профиля новая).
TEST(ProfileCheck, OwnerWithoutScreenHasNoHint) {
    const auto document = duplicateActivity();
    const auto remote = profile("900", QJsonArray{screen("command", 2, 2, QJsonArray{slot("1")})});
    const auto problems = checkWithProfile(document, remote);
    ASSERT_EQ(problems.size(), 2U);  // дубликат + проблема профиля (1 ячейка != сетка)
    EXPECT_EQ(problems.back().where, harmony::Problem::Where::Profile);
    for (const harmony::Problem& problem : problems) {
        if (problem.where != harmony::Problem::Where::Profile) {
            EXPECT_TRUE(problem.hint.args.empty());
        }
    }
}

// 10. Кнопок больше, чем мест на странице, — это страницы, не противоречие (факт).
TEST(ProfileCheck, ButtonsOverOnePageAreNotAProblem) {
    const auto document = harmony::test::configFixture();  // у активности 3 кнопки, экран 2×3
    const QJsonArray cells{slot("1"), slot("2"), slot("3"), slot("4"), slot("5"), slot("6")};
    const auto remote = profile("1100", QJsonArray{screen("activity", 2, 3, cells)});
    const auto base = document.check();
    const auto problems = checkWithProfile(document, remote);
    EXPECT_EQ(problems.size(), base.size());
    EXPECT_TRUE(std::ranges::none_of(problems, [](const harmony::Problem& p) {
        return p.where == harmony::Problem::Where::Profile;
    }));
}

// 11. Слоты не заданы — места не вычисляются, подсказки нет (красное: проблема профиля новая).
TEST(ProfileCheck, ScreenWithoutSlotsHasNoHint) {
    const auto document = duplicateActivity();
    const auto remote = profile("900", QJsonArray{screen("activity", 2, 3, QJsonArray{text("Title")})});
    const auto problems = checkWithProfile(document, remote);
    ASSERT_EQ(problems.size(), 2U);
    EXPECT_EQ(problems.back().where, harmony::Problem::Where::Profile);
    for (const harmony::Problem& problem : problems) {
        if (problem.where != harmony::Problem::Where::Profile) {
            EXPECT_TRUE(problem.hint.args.empty());
        }
    }
}

// 12. Окно проверки: подсказка страницы видна в тексте, проблема профиля — последней строкой,
// «Показать» для неё неактивно (красное).
TEST(ProfileCheck, CheckDialogShowsProfileProblemsAtEnd) {
    const auto document = duplicateActivity();
    const auto remote = profile("900", QJsonArray{
        screen("activity", 2, 2, QJsonArray{slot("1"), slot("2"), slot("3"), slot("4")}),
        screen("command", 2, 3, QJsonArray{text("Title")})});

    app::CheckDialog dialog(document, [](const QString&, const auto&) { return false; }, nullptr, remote);
    auto* list = dialog.findChild<QTreeWidget*>();
    ASSERT_NE(list, nullptr);
    const auto buttons = dialog.findChildren<QPushButton*>();
    const auto show = std::ranges::find_if(buttons, [](const QPushButton* button) {
        return button->text() == "Show";
    });
    ASSERT_NE(show, buttons.end());

    ASSERT_EQ(list->topLevelItemCount(), 2);
    // Факт: страница и место видны в тексте проблемы конфигурации.
    EXPECT_EQ(list->topLevelItem(0)->text(1),
              "two screen buttons at place 5, only one is shown — page 2, place 1");
    EXPECT_TRUE((*show)->isEnabled());
    // Красное: проблема профиля — в конце списка, и её место показать нельзя.
    EXPECT_EQ(list->topLevelItem(1)->text(0), "profile: command");
    EXPECT_EQ(list->topLevelItem(1)->text(1), "profile: command has no command slots");
    list->setCurrentItem(list->topLevelItem(1));
    EXPECT_FALSE((*show)->isEnabled());
}

// 13. Профильная проблема одна: группа «профиль», «Показать» неактивно (красное).
TEST(ProfileCheck, CheckDialogShowsProfileProblemAndDisablesShow) {
    const auto document = harmony::test::configFixture();
    const auto remote = profile("900", QJsonArray{screen("command", 0, 0, QJsonArray{text("Title")})});

    app::CheckDialog dialog(document, [](const QString&, const auto&) { return false; }, nullptr, remote);
    auto* list = dialog.findChild<QTreeWidget*>();
    ASSERT_NE(list, nullptr);
    const auto buttons = dialog.findChildren<QPushButton*>();
    const auto show = std::ranges::find_if(buttons, [](const QPushButton* button) {
        return button->text() == "Show";
    });
    ASSERT_NE(show, buttons.end());

    ASSERT_EQ(list->topLevelItemCount(), 1);
    EXPECT_EQ(list->topLevelItem(0)->text(0), "profile: command");
    EXPECT_EQ(list->topLevelItem(0)->text(1), "profile: command has no command slots");
    EXPECT_FALSE((*show)->isEnabled());
}

} // namespace
