#pragma once

/**
 * @file remoteConfig.h
 * @brief Модель конфигурации пульта: устройства, команды, кнопки, активности.
 *
 * Модель — простые структуры без зависимостей от формата файлов и от GUI.
 * Поля соответствуют тому, что хранится в UserConfiguration.xml; всё,
 * что редактор пока не использует, при чтении пропускается.
 */

#include <string>
#include <utility>
#include <vector>

namespace harmony::model {

/**
 * @brief Как команда доставляется устройству.
 *
 * У Harmony 900 всё инфракрасное; хаб умеет и остальное.
 */
enum class Transport {
    Ir,            ///< Инфракрасный сигнал
    Bluetooth,     ///< Клавиатура или пульт Bluetooth HID
    Network,       ///< Устройство по сети (например, Android TV)
    HomeAssistant, ///< Устройство Home Assistant
    Unknown,       ///< Канал не указан в исходной конфигурации
};

/**
 * @brief Команда устройства («PowerToggle», «VolumeUp» и т.п.).
 *
 * Поля @c protocol и @c code — представление Harmony 900: номер записи в
 * `IrProto.bin` и ссылка в `SsIr.bin`. У других форматов они не заполняются;
 * там команду описывают @c protocolName и @c native.
 */
struct Command {
    std::string name; ///< Имя команды; по нему на команду ссылаются кнопки и активности
    Transport transport = Transport::Ir; ///< Канал доставки
    int protocol = 0; ///< Harmony 900: номер ИК-протокола (описание — в IrProto.bin)
    std::string code; ///< Harmony 900: код команды в шестнадцатеричном виде, как в файле («0x...»)
    std::string protocolName; ///< Имя протокола текстом («Toshiba 32 Bit»); пусто — формат задаёт протокол иначе
    std::string native;       ///< Исходная запись кода в терминах формата; пусто — нет
    /// Код подобран из базы ИК-кодов по модели и именам команд, а не прочитан с хаба;
    /// настоящий код на хабе может отличаться.
    bool fromDatabase = false;
};

/**
 * @brief Кнопка на экране пульта или привязка аппаратной клавиши.
 */
struct Button {
    std::string name;     ///< Аппаратная кнопка пульта («VolumeUp», «Menu»); пусто — экранная
    std::string label;    ///< Подпись; может быть пустой, если задан значок
    std::string icon;     ///< Имя значка (например, «teletext_off»), может быть пустым
    int position = 0;     ///< Позиция кнопки внутри группы
    std::string actionId; ///< Действие: «<id устройства>_<команда>_<модификатор>»
};

/**
 * @brief Группа кнопок (страница или раздел экрана пульта).
 */
struct ControlGroup {
    std::string name;            ///< Имя группы из файла (например, «Misc»)
    std::vector<Button> buttons; ///< Кнопки группы в порядке файла
};

/**
 * @brief Устройство, которым управляет пульт (ТВ, ресивер, медиаплеер).
 */
struct Device {
    std::string id;           ///< Уникальный идентификатор внутри конфигурации
    std::string type;         ///< Тип устройства («Television», «Receiver», ...)
    std::string manufacturer; ///< Производитель
    std::string model;        ///< Модель
    std::string label;        ///< Имя, которое видит пользователь на пульте
    std::vector<ControlGroup> controlGroups; ///< Кнопки устройства на экране пульта
    std::vector<Command> commands;           ///< ИК-команды устройства
    std::vector<std::string> inputs; ///< Входы, которые пульт умеет переключать («HDMI1», ...)
    bool powerControlled = false;    ///< Пульт знает, включено ли оно (есть состояние Power)
    bool notSent = false;            ///< «Не отправлять на пульт» (см. kNotSentMarker)

    /**
     * @brief Ищет команду по имени.
     * @param commandName Имя команды.
     * @return Указатель на команду или nullptr, если такой нет.
     */
    [[nodiscard]] const Command* findCommand(const std::string& commandName) const;

    /**
     * @brief Имя для показа пользователю.
     * @return Подпись устройства, а если она пустая — «производитель модель».
     */
    [[nodiscard]] std::string displayName() const;
};

/**
 * @brief Пометка «не отправлять на пульт»: XML-комментарий первым в <Device> или <Activity>.
 *
 * Комментарий любой разборщик XML пропускает, так что пометка безвредна, даже
 * если файл попадёт на пульт другим путём; при записи из программы помеченное
 * в конфигурацию для пульта не входит вовсе.
 */
inline constexpr const char* kNotSentMarker = "harmony:not-sent";

/**
 * @brief Роль устройства в активности: какое устройство отвечает за звук, картинку и т.д.
 */
struct ActivityRole {
    std::string name;     ///< Имя роли («DEFAULT», «VOLUME», «DISPLAY», ...)
    std::string deviceId; ///< Устройство, выполняющее роль
};

/**
 * @brief Активность («Смотреть ТВ», «Слушать музыку»).
 */
struct Activity {
    std::string id;                          ///< Уникальный идентификатор («-1» — «Выключить всё»)
    std::string type;                        ///< Тип активности («VirtualTelevisionN», «PowerOff», ...)
    std::string label;                       ///< Имя, которое видит пользователь
    std::vector<ActivityRole> roles;         ///< Роли устройств
    std::vector<std::string> powerOn;        ///< Устройства, которые включаются
    std::vector<std::string> powerOff;       ///< Устройства, которые выключаются
    std::vector<ControlGroup> controlGroups; ///< Кнопки активности на экране пульта
    /// Входы, которые активность переключает при запуске: устройство → вход.
    std::vector<std::pair<std::string, std::string>> inputs;
    bool notSent = false; ///< «Не отправлять на пульт» (см. kNotSentMarker)

    /**
     * @brief Устройство роли.
     * @param roleName Имя роли («DEFAULT», «VOLUME», «DISPLAY»).
     * @return Id устройства; пусто — роли нет.
     */
    [[nodiscard]] std::string roleDevice(const std::string& roleName) const;

    /// @return true для служебной активности «Выключить всё».
    [[nodiscard]] bool isPowerOff() const { return type == "PowerOff"; }

    /**
     * @brief Участвует ли устройство в активности (включается или выполняет роль).
     * @param deviceId Идентификатор устройства.
     * @return true, если устройство есть в powerOn или в ролях.
     */
    [[nodiscard]] bool usesDevice(const std::string& deviceId) const;
};

/// Свойство «имя = значение» из раздела <Properties>.
using Property = std::pair<std::string, std::string>;

/**
 * @brief Полная конфигурация пульта.
 */
struct RemoteConfig {
    std::vector<Property> properties; ///< Служебные свойства файла (версия, хеш, дата)
    std::vector<Device> devices;      ///< Все устройства в порядке файла
    std::vector<Activity> activities; ///< Все активности в порядке файла

    /**
     * @brief Значение свойства файла.
     * @param name Имя свойства (например, «LastUpdated»).
     * @return Значение или пустая строка, если свойства нет.
     */
    [[nodiscard]] std::string property(const std::string& name) const;

    /**
     * @brief Ищет устройство по идентификатору.
     * @param deviceId Идентификатор устройства.
     * @return Указатель на устройство или nullptr. Указатель действителен,
     *         пока не изменяется вектор devices.
     */
    [[nodiscard]] Device* findDevice(const std::string& deviceId);

    /// @copydoc findDevice(const std::string&)
    [[nodiscard]] const Device* findDevice(const std::string& deviceId) const;

    /**
     * @brief Ищет активность по идентификатору.
     * @param activityId Идентификатор активности.
     * @return Указатель на активность или nullptr.
     */
    [[nodiscard]] const Activity* findActivity(const std::string& activityId) const;

    /**
     * @brief Проверяет, используется ли устройство хотя бы в одной активности.
     * @param deviceId Идентификатор устройства.
     * @return true, если устройство включается или выполняет роль в какой-либо активности.
     */
    [[nodiscard]] bool isDeviceUsed(const std::string& deviceId) const;

    /// @return Общее число ИК-команд всех устройств.
    [[nodiscard]] std::size_t commandCount() const;

    /// @return Число активностей без служебной «кнопки Off» (PowerOff).
    [[nodiscard]] std::size_t userActivityCount() const;

    /**
     * @brief Не уйдёт ли активность на пульт: помечена сама или ей нужно (включение,
     *        роль, кнопки) устройство с пометкой «не отправлять на пульт».
     */
    [[nodiscard]] bool isActivityNotSent(const Activity& activity) const;

    /**
     * @brief Нужно ли активности устройство: включает его, даёт ему роль или шлёт ему команды кнопками.
     * @param activity Активность.
     * @param deviceId Идентификатор устройства.
     */
    [[nodiscard]] static bool activityNeedsDevice(const Activity& activity, const std::string& deviceId);

    /**
     * @brief Устройства с пометкой «не отправлять на пульт», из-за которых не уйдёт активность.
     * @param activity Активность.
     * @return Устройства в порядке файла; пусто — устройства активность не держат.
     */
    [[nodiscard]] std::vector<const Device*> notSentDevicesOf(const Activity& activity) const;
};

} // namespace harmony::model
