#pragma once

/**
 * @file configFixtures.h
 * @brief Конфигурация пульта, собранная в коде (правило 19): тесты на ней зелёные на любой машине,
 *        потому что ожидания следуют из конструкции фикстуры.
 *
 * Настоящие файлы пульта проверяют отдельные тесты через `dumpFiles.h` — без `dumps/` они
 * пропускаются и остаются регрессией на машине владельца.
 *
 * Фикстура записана без переносов внутри элементов: `toXml()` сохраняет текст как есть, поэтому
 * сравнение «изменилась только одна строка» работает против самой фикстуры.
 */

#include "formatFixtures.h"

#include "harmony/core/configDocument.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

namespace harmony::test {

inline constexpr const char* kTvId = "100";        ///< Television
inline constexpr const char* kReceiverId = "200";  ///< Receiver
inline constexpr const char* kPlayerId = "400";    ///< MediaPlayer
inline constexpr const char* kActivityId = "300";  ///< Обычная активность
inline constexpr const char* kDvdActivityId = "500";
inline constexpr const char* kPowerOffId = "-1";   ///< Служебная «Выключить всё»

/// Минимальная UserConfiguration.xml: три устройства, две активности и служебная.
constexpr const char* kConfigXml =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<Root><Properties><Property name=\"version\">1.0</Property>"
    "<Property name=\"ProtocolCacheHash\">0x00000000</Property></Properties><User><Id>0</Id></User>"
    "<Device><Id>100</Id><Type>Television</Type><Manufacturer>LG</Manufacturer><Model>55UK</Model>"
    "<Properties><Property name=\"AlwaysOn\">false</Property><Property name=\"IsNewDevice\">true</Property></Properties>"
    "<Presentation><Label>Телевизор</Label>"
    "<ControlGroup name=\"Misc\">"
    "<Button><Label></Label><Icon>aspect</Icon><Position>1</Position><ActionId>100_PowerToggle_Hold</ActionId></Button>"
    "<Button><Label>Текст</Label><Position>2</Position><ActionId>100_Text_Press</ActionId></Button>"
    "</ControlGroup>"
    "<ControlGroup name=\"HardButtons\"><Button name=\"Power\"><Label>Питание</Label><Position>3</Position>"
    "<ActionId>100_PowerToggle_Press</ActionId></Button></ControlGroup>"
    "</Presentation>"
    "<States><State><Id>Power</Id><RelativeActions/></State><State><Id>Input</Id>"
    "<Value>HDMI3</Value><Value>HDMI1</Value></State></States>"
    "<Commands><Properties><Property name=\"PressPreSilence\">70</Property><Property name=\"HoldInterKey\">100</Property></Properties>"
    "<Command><Name>PowerToggle</Name><Data><Protocol>0</Protocol><Code>0x00000000010001000300</Code></Data></Command>"
    "<Command><Name>Text</Name><Data><Protocol>0</Protocol><Code>0x00000000010001000300</Code></Data></Command>"
    "<Command><Name>VolumeUp</Name><Data><Protocol>0</Protocol><Code>0x00000000010001000300</Code></Data></Command>"
    "<Command><Name>Aspect</Name><Data><Protocol>0</Protocol><Code>0x00000000010001000300</Code></Data></Command>"
    "<Command><Name>Home</Name><Data><Protocol>0</Protocol><Code>0x00000000010001000300</Code></Data></Command>"
    "</Commands></Device>"
    "<Device><Id>200</Id><Type>Receiver</Type><Manufacturer>Denon</Manufacturer><Model>AVR-X4000</Model>"
    "<Presentation><Label>Ресивер</Label>"
    "<ControlGroup name=\"Misc\"><Button><Label>Громкость</Label><Position>1</Position>"
    "<ActionId>200_VolumeUp_Hold</ActionId></Button></ControlGroup>"
    "<ControlGroup name=\"HardButtons\"><Button name=\"VolumeMute\"><Label>Тишина</Label><Position>2</Position>"
    "<ActionId>200_Mute_Press</ActionId></Button></ControlGroup>"
    "</Presentation>"
    "<States><State><Id>Power</Id><RelativeActions/></State></States>"
    "<Commands><Command><Name>PowerToggle</Name><Data><Protocol>0</Protocol><Code>0x00000000010001000300</Code></Data></Command>"
    "<Command><Name>VolumeUp</Name><Data><Protocol>0</Protocol><Code>0x00000000010001000300</Code></Data></Command>"
    "<Command><Name>Mute</Name><Data><Protocol>0</Protocol><Code>0x00000000010001000300</Code></Data></Command>"
    "</Commands></Device>"
    "<Device><Id>400</Id><Type>MediaPlayer</Type><Manufacturer>Dune</Manufacturer><Model>HD D7 Pro</Model>"
    "<Presentation><Label>Плеер</Label></Presentation>"
    "<States><State><Id>Power</Id><RelativeActions/></State></States>"
    "<Commands><Command><Name>PowerToggle</Name><Data><Protocol>0</Protocol><Code>0x00000000010001000300</Code></Data></Command>"
    "</Commands></Device>"
    "<Activity><Id>300</Id><Type>VirtualTelevisionN</Type>"
    "<Properties><Property name=\"HideModeControl\">False</Property></Properties>"
    "<Presentation><Label>Смотреть ТВ</Label><ChannelList/>"
    "<ControlGroup name=\"Misc\"><Button><Label>Питание</Label><Position>1</Position>"
    "<ActionId>100_PowerToggle_Press</ActionId></Button>"
    "<Button><Label>Ракурс</Label><Position>2</Position><ActionId>100_Aspect_Press</ActionId></Button>"
    "<Button><Label>Громкость</Label><Position>4</Position><ActionId>200_VolumeUp_Hold</ActionId></Button>"
    "</ControlGroup>"
    "<ControlGroup name=\"HardButtons\"><Button name=\"VolumeUp\"><Label>Громкость</Label><Position>3</Position>"
    "<ActionId>200_VolumeUp_Hold</ActionId></Button></ControlGroup>"
    "</Presentation>"
    "<EnterActions>"
    "<Action><Target>Device</Target><Operation><Name>SetValue</Name><Parameter name=\"DeviceId\">100</Parameter>"
    "<Parameter name=\"State\">Input</Parameter><Parameter name=\"Value\">HDMI3</Parameter></Operation></Action>"
    "<Action><Target>Device</Target><Operation><Name>SendCommand</Name><Parameter name=\"DeviceId\">100</Parameter>"
    "<Parameter name=\"Command\">Home</Parameter></Operation></Action>"
    "</EnterActions>"
    "<Role><Name>DEFAULT</Name><DeviceId>100</DeviceId><Presentation/></Role>"
    "<Role><Name>VOLUME</Name><DeviceId>200</DeviceId><Presentation/></Role>"
    "<Power><On>100</On><On>200</On><Off>400</Off></Power></Activity>"
    "<Activity><Id>500</Id><Type>VirtualDvd</Type><Presentation><Label>Плеер</Label>"
    "<ControlGroup name=\"Misc\"><Button><Label>Плей</Label><Position>1</Position>"
    "<ActionId>400_PowerToggle_Press</ActionId></Button></ControlGroup></Presentation>"
    "<Role><Name>DEFAULT</Name><DeviceId>400</DeviceId><Presentation/></Role>"
    "<Power><On>400</On><Off>100</Off><Off>200</Off></Power></Activity>"
    "<Activity><Id>-1</Id><Type>PowerOff</Type><Presentation><Label>Выключить всё</Label></Presentation>"
    "<Power><Off>100</Off><Off>200</Off><Off>400</Off></Power></Activity>"
    "</Root>";

/**
 * @brief Документ из @p xml; таблицы — собранные в коде (`formatFixtures.h`).
 * @param tables false — документ без ИК-таблиц (для тестов проверки и восстановления).
 */
inline harmony::ConfigDocument openFixture(const std::string& xml, bool tables = true) {
    harmony::ConfigDocument::Files files;
    if (tables) {
        files = codeTables();
    }
    files.userConfiguration = xml;
    auto document = harmony::ConfigDocument::fromFiles(files, "fixture");
    EXPECT_TRUE(document) << (document ? "" : document.error().message);
    return std::move(*document);
}

/// Документ, собранный из фикстуры.
inline harmony::ConfigDocument configFixture() {
    return openFixture(kConfigXml);
}

/// Заменяет первое @p from на @p to внутри элемента @p tag с id @p id.
inline std::string replaceIn(const std::string& xml, const std::string& tag, const std::string& id,
                             const std::string& from, const std::string& to) {
    const auto begin = xml.find("<" + tag + "><Id>" + id + "</Id>");
    EXPECT_NE(begin, std::string::npos) << id;
    const auto end = xml.find("</" + tag + ">", begin);
    const auto at = xml.find(from, begin);
    EXPECT_TRUE(at != std::string::npos && at < end) << from;
    if (at != std::string::npos && at < end) {
        std::string copy = xml;
        copy.replace(at, from.size(), to);
        return copy;
    }
    return xml;
}

/// Значение <Property name="ProtocolCacheHash"> в фикстуре (0x00000000).
inline std::string withHash(const std::string& xml, const std::string& hash) {
    const auto at = xml.find("0x00000000");
    EXPECT_NE(at, std::string::npos);
    std::string copy = xml;
    copy.replace(at, 10, hash);
    return copy;
}

/**
 * @brief Папка конфигурации из фикстуры: UserConfiguration.xml и таблицы, собранные в коде.
 *
 * Нужна тестам, которые проверяют чтение с диска и сохранение. Ожидания следуют из фикстуры,
 * поэтому тест зелёный на любой машине (правило 19).
 */
class ConfigFixtureFolder {
public:
    explicit ConfigFixtureFolder(std::string_view name = "config") : folder_(name) {
        const auto files = codeTables();
        writeFile(folder_.path() / "UserConfiguration.xml", kConfigXml);
        writeFile(folder_.path() / "IrProto.bin", std::string(files.irProto.begin(), files.irProto.end()));
        writeFile(folder_.path() / "SsIr.bin", std::string(files.ssIr.begin(), files.ssIr.end()));
    }

    const std::filesystem::path& path() const { return folder_.path(); }

private:
    TempFolder folder_;
};

} // namespace harmony::test
