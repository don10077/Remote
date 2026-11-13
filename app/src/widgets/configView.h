#pragma once

/**
 * @file configView.h
 * @brief Экран работы с конфигурацией.
 *
 * Слева — боковая панель (карточка пульта, устройства и активности плитками),
 * по центру — страница выбранного объекта с вкладками, справа — пульт
 * с живым экраном. Связывает модель с ModernShell и RemotePanel.
 */

#include "buttonRef.h"
#include "harmony/core/buttonOwner.h"
#include "harmony/core/model/remoteConfig.h"

#include "harmony/core/configDocument.h"

#include <QPointer>
#include <QTableWidget>
#include <QWidget>

#include <functional>
#include <optional>
#include <string>

class QLabel;
class QPushButton;
class QTabWidget;
class QTableWidget;

namespace harmony::app {

class ModernShell;
class RemotePanel;
class ToggleSwitch;

/**
 * @brief Экран конфигурации.
 *
 * Только показывает модель; модель принадлежит главному окну и должна жить,
 * пока она показана (или до следующего setConfig()).
 */
class ConfigView final : public QWidget {
    Q_OBJECT

public:
    /// Вид объекта, над которым просят действие.
    enum class Item { Activity, Device };

    /// Выбранный слева объект.
    struct Selection {
        Item kind = Item::Activity;
        std::string id;
    };

    /// @return Выбранная активность или устройство; nullopt — открыта страница «Пульт».
    [[nodiscard]] std::optional<Selection> selection() const {
        return hasSelection_ && config_ != nullptr ? std::optional(Selection{selectedKind_, selectedId_})
                                                   : std::nullopt;
    }

    /// Куда относятся «Вырезать / Копировать / Вставить»: открытая вкладка и выбранное в ней.
    struct ClipboardTarget {
        enum class Kind {
            None,     ///< Нечего копировать и некуда вставлять (страница «Пульт»)
            Buttons,  ///< Кнопки активности или устройства
            Commands, ///< Команды устройства
        };
        Kind kind = Kind::None;
        ButtonOwner owner;                 ///< Buttons: чьи кнопки
        std::optional<ButtonRef> button;   ///< Buttons: выбранная кнопка
        std::string deviceId;              ///< Commands: устройство
        std::vector<std::string> commands; ///< Commands: выбранные команды
    };

    /**
     * @return Цель буфера обмена: на вкладке «Кнопки» — кнопки, на «Команды» — команды;
     *         иначе по выбранному слева: у активности — кнопки, у устройства — команды.
     */
    [[nodiscard]] ClipboardTarget clipboardTarget() const;

    /// Открывает страницу «Пульт» (обзор конфигурации).
    void showOverview();

    /// Открытая страница и вкладка (для справки по F1).
    enum class Page {
        Overview,         ///< Страница «Пульт»
        ActivityDevices,  ///< Активность → «Устройства»
        ActivityButtons,  ///< Активность → «Кнопки»
        DeviceCommands,   ///< Устройство → «Команды»
        DeviceButtons,    ///< Устройство → «Кнопки»
        DeviceActivities, ///< Устройство → «Активности»
    };

    /// @return Что сейчас открыто справа.
    [[nodiscard]] Page currentPage() const;

    /**
     * @brief Создаёт пустой экран.
     * @param parent Родительский виджет.
     */
    explicit ConfigView(QWidget* parent = nullptr);

    /**
     * @brief Показывает конфигурацию и выбирает первую активность.
     * @param config Модель; nullptr — очистить экран.
     * @param name Имя конфигурации для карточки пульта.
     */
    void setConfig(const model::RemoteConfig* config, const QString& name = {});

    /// Обновляет имя конфигурации в карточке пульта (например, после «Сохранить как»).
    void setDocumentName(const QString& name);

    /**
     * @brief Перечитывает модель после правки, отмены или повтора.
     *
     * Выбранный объект остаётся выбранным (ищется по id); если его больше нет —
     * выбирается первая активность.
     */
    void refresh();

    /// Источник подробностей команд: устройство, имя → сигнал, протокол, код.
    using CommandLookup = std::function<std::optional<ConfigDocument::CommandDetails>(const std::string& deviceId,
                                                                                      const std::string& name)>;

    /// Задаёт источник подробностей команд (протокол, адрес, код, сигнал).
    void setCommandLookup(CommandLookup lookup) { commandLookup_ = std::move(lookup); }

    /// Причина отсутствия сигнала; показывается в подсказке строки команды.
    void setCommandErrorLookup(std::function<QString(const std::string&, const std::string&)> lookup) {
        commandErrorLookup_ = std::move(lookup);
    }

    /**
     * @brief Выделяет кнопку в таблице кнопок активности и на пульте (например, после правки).
     * @param button Кнопка; нет такой строки — ничего не меняется.
     */
    void selectButton(const ButtonRef& button);

    /**
     * @brief Выбирает объект и открывает его страницу (например, только что добавленный).
     * @param item Активность или устройство.
     * @param id Идентификатор; нет такого — ничего не меняется.
     */
    void select(Item item, const std::string& id);

    /// Открывает устройство на вкладке команд и выделяет команду @p name.
    void selectCommand(const std::string& deviceId, const std::string& name);

signals:
    void chooseRemoteRequested(); ///< Сменить модель со страницы сводки пульта.
    /// Пояснение для строки состояния (например, по щелчку на системную кнопку пульта).
    void message(const QString& text);

    /// Просьба переименовать объект (кнопка «Переименовать» или двойной щелчок по заголовку).
    void renameRequested(harmony::app::ConfigView::Item item, const QString& id);

    /// Просьба удалить объект.
    void removeRequested(harmony::app::ConfigView::Item item, const QString& id);

    /// Просьба добавить активность или устройство (кнопка «+» под разделом).
    void addRequested(harmony::app::ConfigView::Item item);

    /// Просьба назначить или изменить кнопку активности или устройства (открыть окно кнопки).
    /// Переместить устройство на указанную позицию.
    void deviceMoveRequested(const QString& id, int index);

    void buttonEditRequested(const harmony::ButtonOwner& owner, const harmony::app::ButtonRef& button);

    /**
     * @brief Просят окно «Копировать кнопки» активности.
     * @param activityId Откуда копировать.
     * @param button Выбранная кнопка (можно скопировать только её); hasButton = false — не выбрана.
     */
    void buttonCopyRequested(const QString& activityId, bool hasButton, const harmony::app::ButtonRef& button);

    /// Просьба снять назначение аппаратной кнопки или удалить экранную.
    void buttonRemoveRequested(const harmony::ButtonOwner& owner, const harmony::app::ButtonRef& button);

    /// Кнопку перетащили на пульте: перенести (или скопировать) назначение.
    void buttonMoveRequested(const harmony::ButtonOwner& owner, const harmony::app::ButtonRef& from,
                             const harmony::app::ButtonRef& to, bool copy);

    /// Команду устройства перетащили на кнопку пульта: назначить её.
    void commandAssignRequested(const harmony::ButtonOwner& owner, const harmony::app::ButtonRef& button,
                                const QString& deviceId, const QString& command);

    /// Просьба изменить состав активности (устройства, роли, входы).
    void activityEditRequested(const QString& activityId);

    /// Просят окно «Питание» устройства (как пульт включает и выключает его).
    void devicePowerRequested(const QString& deviceId);

    /// Просят окно «Входы» устройства (как пульт переключает его входы).
    void deviceInputsRequested(const QString& deviceId);

    /// Просят окно «Заменить устройство» (новое — на место @p deviceId в активностях).
    void replaceDeviceRequested(const QString& deviceId);

    /// Просят окно «Сравнить устройства» (слева — @p deviceId).
    void compareDevicesRequested(const QString& deviceId);

    /// Просят окно «Свойства» устройства или активности.
    void propertiesRequested(harmony::app::ConfigView::Item item, const QString& id);

    /// Переключили «Не отправлять на пульт» у устройства или активности.
    void notSentToggled(harmony::app::ConfigView::Item item, const QString& id, bool notSent);

    /// Активность перетащили на позицию @p index в списке.
    void activityMoveRequested(const QString& id, int index);

    /**
     * @brief Нажали «Обучить…» у устройства.
     * @param deviceId Устройство.
     * @param commands Команды, выделенные в таблице (их переучить); пусто — выбор по умолчанию.
     */
    void learnRequested(const QString& deviceId, const QStringList& commands);

    /// Просят окно «Команда вручную» (протокол, адрес, код); @p command — выбранная (пусто — новая).
    void protocolCommandRequested(const QString& deviceId, const QString& command);

    /// Двойной щелчок по команде или «Подробно…».
    void commandDetailsRequested(const QString& deviceId, const QString& command);

    /// Нажали «Удалить команду».
    void commandRemoveRequested(const QString& deviceId, const QString& command);

protected:
    /**
     * @brief Delete в таблице кнопок или команд удаляет выделенную строку.
     *
     * Без этого клавишу перехватывает оконное «Правка → Удалить…» и предлагает удалить
     * объект слева целиком. Таблица забирает Delete через ShortcutOverride и удаляет строку
     * так же, как кнопка «Удалить» под ней.
     */
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// Удаляет выделенную строку таблицы кнопок или команд, как кнопка под таблицей.
    void deleteSelectedRow(const QObject* table);

    /// Открывает выделенную строку (Enter): окно кнопки или подробности команды.
    void openSelectedRow(const QObject* table);

    /// Заполняет плитки боковой панели из модели.
    void fillSidebar();

    /// Заполняет страницу обзора (открывается карточкой пульта).
    void fillOverview();

    /// Показывает активность с индексом @p index.
    void showActivity(int index);

    /// Показывает устройство с индексом @p index.
    void showDevice(int index);

    /**
     * @brief Заполняет таблицу кнопок. В строках запоминается кнопка пульта или
     *        позиция экранной кнопки, чтобы подсветить её на пульте.
     */
    void fillButtons(QTableWidget* table, const std::vector<model::ControlGroup>& groups) const;

    /// Заменяет вкладки, сохраняя выбранную по смыслу («Кнопки» остаются «Кнопками»).
    static void replaceTabs(QTabWidget* tabs, const QList<QPair<QWidget*, QString>>& pages);

    /// Подключает подсветку кнопки на пульте при выборе строки таблицы кнопок.
    void linkToRemote(QTableWidget* table);

    /**
     * @brief Вкладка кнопок: действия над таблицей и сама таблица.
     * @param owner Активность или устройство.
     * @param groups Его кнопки.
     * @param canAssign Есть кому слать команды (у активности — устройства, у устройства — команды).
     */
    QWidget* makeButtonsPage(const ButtonOwner& owner, const std::vector<model::ControlGroup>& groups,
                             bool canAssign);

    /// @return Владелец кнопок открытой страницы (активность или устройство).
    [[nodiscard]] ButtonOwner currentOwner() const;

    /// @return У модели пульта есть зоны кнопок — кнопки можно назначать.
    [[nodiscard]] bool remoteHasKeys() const;

    /// Команду бросили на кнопку картинки пульта.
    void commandDropped(const ButtonRef& button, const QString& deviceId, const QString& command);

    /// Сбрасывает «щёлкнутую свободную кнопку», если открыт другой объект.
    void resetPending(Item kind, const std::string& id);

    /// @return Кнопка выделенной строки таблицы кнопок (nullopt — ничего не выделено).
    [[nodiscard]] std::optional<ButtonRef> selectedButton() const;

    /// Обновляет доступность «Изменить» и «Удалить» по выделению.
    void updateButtonActions();

    /// Открывает вкладку «Кнопки» у страницы с таблицей кнопок.
    void showButtonsTab();

    /// Кнопку пульта щёлкнули на картинке: выделить строку или подсказать, как назначить.
    void remoteButtonClicked(const ButtonRef& button);

    /// Выбирает первую «настоящую» активность (или обзор, если активностей нет).
    void selectFirst();

    /// Обновляет кнопки действий под выбранный объект.
    void updateActions();

    const model::RemoteConfig* config_ = nullptr;
    CommandLookup commandLookup_; ///< Подробности команд
    std::function<QString(const std::string&, const std::string&)> commandErrorLookup_;

    // Выбранный объект — по id, а не по индексу: индексы меняются при правке.
    bool hasSelection_ = false;
    Item selectedKind_ = Item::Activity;
    std::string selectedId_;

    // Таблица кнопок открытого объекта (пересоздаётся при каждом показе).
    QTableWidget* buttonsTable_ = nullptr;
    QPointer<QTableWidget> commandsTable_; ///< Команды открытого устройства (пересоздаётся)
    QPushButton* editButtonAction_ = nullptr;
    QPushButton* removeButtonAction_ = nullptr;
    ButtonRef pendingKey_ = ButtonRef::hard({});  ///< Свободная кнопка, щёлкнутая на пульте
    int pendingSoft_ = -1;                        ///< Свободное место экрана, щёлкнутое на пульте

    QWidget* actions_ = nullptr; ///< Кнопки над выбранным объектом (в заголовке страницы)
    QPushButton* renameButton_ = nullptr;
    QPushButton* editActivityButton_ = nullptr;
    QPushButton* compareDevicesButton_ = nullptr; ///< «Сравнить» у устройства
    QPushButton* propertiesButton_ = nullptr;     ///< «Свойства» устройства или активности
    QPushButton* replaceDeviceButton_ = nullptr;  ///< «Заменить» у устройства
    ToggleSwitch* activeSwitch_ = nullptr;        ///< «Активно»: объект отправляется на пульт
    QPushButton* removeButton_ = nullptr;

    ModernShell* shell_ = nullptr;
    RemotePanel* remote_ = nullptr;
    QTabWidget* activityTabs_ = nullptr;
    QTabWidget* deviceTabs_ = nullptr;
    QLabel* overview_ = nullptr;

    // Над вкладками активности: «не отправляется: устройство неактивно» и кнопка включить его.
    QWidget* activityNotice_ = nullptr;
    QLabel* activityNoticeText_ = nullptr;
    QPushButton* activityNoticeButton_ = nullptr;
    std::string noticeDeviceId_; ///< Устройство, которое включает кнопка полосы
};

} // namespace harmony::app
