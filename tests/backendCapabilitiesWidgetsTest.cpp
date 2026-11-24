/**
 * @file backendCapabilitiesWidgetsTest.cpp
 * @brief B2: экран конфигурации спрашивает свойства формата, а не строку SourceFormat.
 *
 * Тесты фиксируют решения, которые видны пользователю: подпись об импорте в обзоре и
 * строку «питание не определено импортом» в составе активности. Формат задаётся данными
 * XML-проекта (как у сохранённого проекта двоичного импорта), поэтому ожидания не зависят
 * от файлов вне git (правило 19). До B2 те же решения принимались сравнением строки.
 */

#include "configFixtures.h"

#include "../app/src/remoteResources.h"
#include "../app/src/widgets/configView.h"

#include <QApplication>
#include <QLabel>
#include <QTableWidget>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace {

namespace app = harmony::app;
namespace resources = app::remoteResources;

/// Вставляет свойство формата в <Properties> корня (так его пишет сохранённый проект).
std::string withSourceFormat(std::string xml, std::string_view format) {
    constexpr std::string_view kProperties = "<Properties>";
    const auto at = xml.find(kProperties);
    EXPECT_NE(at, std::string::npos);
    if (at == std::string::npos) {
        return xml;
    }
    xml.insert(at + kProperties.size(),
               "<Property name=\"SourceFormat\">" + std::string(format) + "</Property>");
    return xml;
}

class BackendCapabilitiesWidgets : public testing::Test {
protected:
    QTemporaryDir root;

    void SetUp() override {
        ASSERT_TRUE(root.isValid());
        ASSERT_TRUE(resources::reload(root.path()).isEmpty());
    }

    void TearDown() override { resources::reload(root.path()); }

    /// Документ из фикстуры; @p format пусто — обычный XML (свойства формата нет).
    static harmony::ConfigDocument document(std::string_view format,
                                            const std::string& xml = harmony::test::kConfigXml) {
        const std::string text = format.empty() ? xml : withSourceFormat(xml, format);
        auto document = harmony::ConfigDocument::fromXml(text, "fixture");
        EXPECT_TRUE(document) << (document ? "" : document.error().message);
        return document ? std::move(*document) : harmony::ConfigDocument::createNew("fixture");
    }

    /// Активность, в которой устройство 200 используется, но не входит в списки питания.
    static std::string activityWithUnlistedDevice() {
        return harmony::test::replaceIn(harmony::test::kConfigXml, "Activity",
                                        harmony::test::kActivityId, "<On>200</On>", "");
    }

    /// Есть ли в таблицах экрана строка с таким текстом.
    static bool hasTableText(app::ConfigView& view, const QString& text) {
        for (const auto* table : view.findChildren<QTableWidget*>()) {
            for (int row = 0; row < table->rowCount(); ++row) {
                for (int column = 0; column < table->columnCount(); ++column) {
                    if (table->item(row, column) != nullptr && table->item(row, column)->text() == text) {
                        return true;
                    }
                }
            }
        }
        return false;
    }
};

// Обзор: подпись об импорте — решение формата документа, а не строка в UI.
TEST_F(BackendCapabilitiesWidgets, OverviewTellsAboutImportFromFormat) {
    auto imported = document("BinaryHid");
    app::ConfigView importView;
    importView.setConfig(&imported, "import");
    importView.showOverview();
    auto* importText = importView.findChild<QLabel*>("overviewText");
    ASSERT_NE(importText, nullptr);
    EXPECT_TRUE(importText->text().contains("Imported HID configuration")) << importText->text().toStdString();

    auto xml = document("");
    app::ConfigView xmlView;
    xmlView.setConfig(&xml, "xml");
    xmlView.showOverview();
    auto* xmlText = xmlView.findChild<QLabel*>("overviewText");
    ASSERT_NE(xmlText, nullptr);
    EXPECT_FALSE(xmlText->text().contains("Imported HID configuration")) << xmlText->text().toStdString();
}

// Состав активности: «питание не определено импортом» — только у формата с потерями.
TEST_F(BackendCapabilitiesWidgets, ActivityDevicesMarkUnknownPowerOnlyForImport) {
    const std::string xml = activityWithUnlistedDevice();

    auto imported = document("BinaryHid", xml);
    app::ConfigView importView;
    importView.setConfig(&imported, "import");
    importView.show();
    importView.select(app::ConfigView::Item::Activity, harmony::test::kActivityId);
    QApplication::processEvents();
    EXPECT_TRUE(hasTableText(importView, "not determined by import"));

    auto plain = document("", xml);
    app::ConfigView plainView;
    plainView.setConfig(&plain, "plain");
    plainView.show();
    plainView.select(app::ConfigView::Item::Activity, harmony::test::kActivityId);
    QApplication::processEvents();
    EXPECT_FALSE(hasTableText(plainView, "not determined by import"));
}

} // namespace
