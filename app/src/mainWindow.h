#pragma once

/**
 * @file mainWindow.h
 * @brief Главное окно: шапка, стартовый экран и экран конфигурации.
 */

#include "harmony/core/configDocument.h"
#include "harmony/core/undo/undoStack.h"
#include "harmony/transport/remoteLink.h"

#include "versionStore.h"
#include "widgets/configView.h"

#include <QCloseEvent>
#include <QMainWindow>
#include <QPointer>
#include <QProgressDialog>

#include <functional>

#include <memory>
#include <variant>

class QFileSystemWatcher;
class QLabel;
class QStackedWidget;
class QThread;
class QTimer;
class QToolButton;

namespace harmony::irdb {
class IrDatabase;
}

namespace harmony::app {

class JournalDialog;
class ZoneEditorDialog;

class AppHeader;
class IrDatabaseService;
class RemoteSession;
class ConfigView;
class WelcomePage;

/**
 * @brief Окно хода операции, которое нельзя закрыть (Esc, крестик), пока отмена не разрешена.
 *
 * Обычный QProgressDialog по Esc и по крестику шлёт canceled() даже без кнопки «Отмена»,
 * а на перезапуске пульта отменять уже нельзя.
 */
class GuardedProgressDialog final : public QProgressDialog {
public:
    using QProgressDialog::QProgressDialog;

    /// Разрешает или запрещает отмену (кнопку «Отмена» показывает сам вызывающий).
    void setCancellable(bool cancellable) { cancellable_ = cancellable; }

    /// @return Можно ли сейчас отменить.
    [[nodiscard]] bool cancellable() const { return cancellable_; }

    void reject() override {
        if (cancellable_) {
            QProgressDialog::reject();
        }
    }

protected:
    void closeEvent(QCloseEvent* event) override {
        if (cancellable_) {
            QProgressDialog::closeEvent(event);
        } else {
            event->ignore();
        }
    }

private:
    bool cancellable_ = true;
};

/**
 * @brief Главное окно приложения.
 *
 * Строки меню нет: действия с конфигурацией открываются из шапки (по её имени),
 * горячие клавиши работают через действия, добавленные прямо в окно.
 */
class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    /**
     * @brief Создаёт окно со стартовым экраном.
     * @param parent Родительский виджет (обычно nullptr).
     */
    explicit MainWindow(QWidget* parent = nullptr);

    /**
     * @brief Действие при запуске: открыть конфигурацию «при запуске» или показать окно выбора.
     *
     * Вызывать после show(), чтобы окно выбора появилось поверх главного окна.
     */
    void startup();

protected:
    /// Спрашивает про несохранённые изменения и запоминает размер окна.
    void closeEvent(QCloseEvent* event) override;

private:
    /// Создаёт действия, меню шапки и горячие клавиши.
    void createActions();

    /// Строка меню: Файл, Правка, Вид, Пульт, Настройки, Справка.
    void createMenuBar(const QList<QAction*>& openActions, QAction* quitAction, QAction* undoAction,
                       QAction* redoAction, QAction* checkAction, QAction* backupsAction, QAction* helpAction,
                       QAction* aboutAction);

    /// Какую вкладку настроек открыть.
    enum class SettingsPage { General, IrDatabase };

    /// Окно настроек; после смены языка предлагает перезапуск.
    void showSettings(SettingsPage page = SettingsPage::General);

    /// Добавляет устройство из базы ИК-кодов (номер в каталоге).
    void addFromDatabase(int entry);

    /// Показывает окно выбора конфигурации и выполняет выбор.
    void showStartDialog();

    /// Создаёт новую пустую конфигурацию (спрашивает имя и место файла EZHex).
    void createConfig();

    /// Спрашивает папку или файл и открывает конфигурацию.
    void browseConfig();

    /**
     * @brief Открывает конфигурацию из папки.
     * @param folder Папка конфигурации или файл UserConfiguration.xml.
     * @return true, если открыта.
     */
    bool openFolder(const QString& folder);
    /// После открытия хаба: KeyCode команд → ИК-данные по базе (одной отменяемой правкой).
    void resolveHubCodes();
    /// Устройства хаба без кодов (стоковый хаб): модель ищется в базе, при нескольких — выбор пользователя.
    void matchStockHubDevices(const irdb::IrDatabase& database);

    /// Открывает встроенный пример (без папки: сохранить можно только «как»).
    void openDemo();

    /// Сохраняет в текущий EZHex; для импортированной папки или нового документа выбирает файл.
    bool save();

    /// Сохраняет в выбранный файл EZHex.
    bool saveAs();

    /// Закрывает конфигурацию и возвращает на стартовый экран.
    void closeConfig();

    /**
     * @brief Спрашивает, что делать с несохранёнными изменениями.
     * @return true — можно продолжать (сохранено или изменения не нужны), false — отмена.
     */
    bool confirmDiscard();

    /// Объясняет, как подключить пульт и что уже умеет программа.
    void showConnectionHelp();

    /// Показывает окно «О программе».
    void showAbout();

    /// Открывает справку на разделе открытой страницы и вкладки (F1).
    void showHelp();

    /// Показывает документ (окно забирает владение).
    void showDocument(ConfigDocument document);

    /// Обновляет заголовок, шапку и сводку после открытия или сохранения.
    void updateTitle();

    /**
     * @brief Применяет правку документа шагом истории и обновляет экран.
     * @param text Описание шага («Переименовать «ТВ»»).
     * @param change Правка; при ошибке показывается сообщение, история не меняется.
     * @return true, если правка применена.
     */
    bool applyEdit(const QString& text,
                   const std::function<Result<void>(ConfigDocument&)>& change);

    /// Отменяет последний шаг и обновляет экран.
    void undo();

    /// Повторяет отменённый шаг и обновляет экран.
    void redo();

    /// Спрашивает новое имя активности или устройства и переименовывает.
    void renameItem(ConfigView::Item item, const QString& id);

    /// Спрашивает подтверждение и удаляет активность или устройство.
    void removeItem(ConfigView::Item item, const QString& id);

    /**
     * @brief Удаление устройства, которое нужно активностям: окно с выбором
     *        «убрать из активностей и удалить», «заменить…» или отмена.
     * @param deviceId Устройство.
     * @param name Его имя для окна и истории.
     */
    void removeUsedDevice(const std::string& deviceId, const QString& name);

    /// Переносит устройства из другой конфигурации (окно выбора источника и устройств).
    void addDevices();

    /// Создаёт устройство с нуля (окно имени и типа) и предлагает выучить команды.
    void createDevice();

    /**
     * @brief Мастер обучения команд с родного пульта (каждая выученная команда — шаг отмены).
     * @param deviceId Устройство.
     * @param commands Какие команды отметить сразу (переучить выделенные); пусто — недостающие.
     */
    void learnCommands(const QString& deviceId, const QStringList& commands = {});

    /// Окно команды: протокол, биты, сигнал, тайминги.
    void showCommandDetails(const QString& deviceId, const QString& command);

    /// Удаляет команду устройства; если она на кнопках или в запуске активностей — предлагает убрать её оттуда.
    void removeCommand(const QString& deviceId, const QString& command);

    /// Собирает новую активность (окно выбора устройств и ролей).
    void addActivity();

    /// Открывает окно кнопки активности или устройства и применяет назначение.
    void editButton(const ButtonOwner& owner, const ButtonRef& button);

    /// Снимает назначение аппаратной кнопки или удаляет экранную.
    void removeButton(const ButtonOwner& owner, const ButtonRef& button);

    /// Переносит или копирует назначение кнопки (перетаскивание на пульте).
    void moveButton(const ButtonOwner& owner, const ButtonRef& from, const ButtonRef& to, bool copy);

    /// Назначает кнопке команду устройства (команду перетащили на картинку пульта).
    void assignCommand(const ButtonOwner& owner, const ButtonRef& button, const QString& deviceId,
                       const QString& command);

    /// Меняет состав активности: устройства, роли, входы (окно активности).
    void editActivity(const QString& activityId);

    /// Переставляет активность на позицию @p index.
    void moveActivity(const QString& id, int index);

    /// Читает конфигурацию с пульта, сохраняет резервной копией и открывает.
    void readFromRemote();

    /// Выбирает целевой пульт проекта или модель для чтения без открытого проекта.
    void chooseRemote();
    /// Перечитать ресурсы, при ошибке сохранить прежний снимок.
    void reloadRemoteResources();
    /**
     * @brief Перечитать ресурсы и заново подписаться на изменения файлов.
     * @param interactive true — ошибка в окне; false (автоперечитывание) — в строке
     *        состояния и журнале, а занятый пульт откладывает попытку.
     */
    void applyRemoteResources(bool interactive);
    /// Открыть конфигурацию при запуске: «открывать при запуске», последнюю или окно выбора.
    void openStartupConfig();
    /// Открыть редактор зон пультов (zones.json, layouts.json).
    void editRemoteZones();
    /// Выгрузить ресурсы текущей модели или все в папку для правки.
    void exportRemoteResources();
    /// Открыть выбранную папку ресурсов в файловом менеджере.
    void openRemoteResourcesFolder();
    /// Сохраняет диагностический HID-дамп, не передавая его парсеру Harmony 900.
    void saveHidBackup(const QByteArray& backup);

    /// Открывает конфигурацию, прочитанную с пульта.
    void openFromRemote(const QByteArray& ezhex);

    /// Записывает открытую конфигурацию на пульт (после подтверждения).
    void writeToRemote();

    /// Чем отличаются конфигурации по составу (устройства и активности по именам).
    [[nodiscard]] static QString differenceText(const model::RemoteConfig& current, const model::RemoteConfig& remote);

    /// Копия с пульта сохранена (первый шаг записи): показать, что изменится, и записать.
    void backupSaved(const QByteArray& ezhex, const QString& backupFile);

    /// Итог записи на пульт.
    void remoteWritten(const QString& written);

    /// Окно «Команда вручную»: новая команда или правка @p command (каждая — шаг отмены).
    void protocolCommand(const QString& deviceId, const QString& command);

    /**
     * @brief Окно «Питание» или «Входы» устройства (с отменой).
     * @param deviceId Устройство.
     * @param inputs true — «Входы», false — «Питание».
     */
    void editDeviceStates(const QString& deviceId, bool inputs);

    /// Окно «Заменить устройство»: новое — на место прежнего в активностях (одним шагом отмены).
    void replaceDevice(const QString& deviceId);

    /// Окно «Сравнить устройства»; отмеченные команды копируются одним шагом отмены.
    void compareDevices(const QString& deviceId);

    /// Окно «Свойства» устройства или активности; все изменения — одним шагом отмены.
    void editProperties(ConfigView::Item item, const QString& id);

    /// «Не отправлять на пульт» у устройства или активности (шаг отмены).
    void setNotSent(ConfigView::Item item, const QString& id, bool notSent);

    /// Окно «Копировать кнопки» активности (одним шагом отмены) и итог.
    void copyButtons(const QString& activityId, bool hasButton, const ButtonRef& button);

    /// Окно «Проверка конфигурации»; «Показать» — открыть место проблемы.
    void showCheck();

    /// Открывает место проблемы: устройство или активность и кнопку.
    void showProblem(const Problem& problem);

    /// Обновляет значок проблем в строке состояния.
    void updateProblems();

    /// Несохранённые правки — версией (если с прошлого автосохранения что-то менялось).
    void autosave();

    /// После открытия: есть автосохранение новее файла — предложить восстановить.
    void offerAutosave();

    /// Окно «Версии».
    void showVersions();

    /// Окно «Сравнить с другой конфигурацией».
    void compareWithOther();

    /// Заменяет содержимое версией (с отменой); текущее состояние перед этим — версией.
    void restoreVersion(const versions::Version& version);

    /// Быстрый поиск (Ctrl+K): открыть найденное.
    void showQuickSearch();

    /// Окно «Настройки пульта»: в конфигурации (с отменой) или сразу на подключённом пульте.
    void showRemoteSettings();

    /// Системный журнал пульта (telnet, sloginfo) — в отдельном окне.
    void showRemoteLog();

    /// Ошибка или отмена операции с пультом.
    void remoteFailed(const QString& message, bool cancelled);

    /**
     * @brief Свободна ли сессия пульта для новой операции.
     *
     * Занята фоновой проверкой подключения — @p retry выполнится сразу после неё
     * (сказано в строке состояния); занята другой операцией — только сообщение.
     * @return true — можно начинать сейчас.
     */
    bool remoteReady(const std::function<void()>& retry);

    /**
     * @brief Окно прогресса операции с пультом.
     * @param title Заголовок окна прогресса.
     * @param failureTitle Заголовок окна ошибки («Write to remote failed»).
     */
    void startRemoteProgress(const QString& title, const QString& failureTitle);
    void finishRemoteProgress();
    void showRemoteProgress(transport::Stage stage, qint64 done, qint64 total);

    /// Обновляет кнопки отмены/повтора и признак изменённого документа.
    void updateUndoState();

    // --- Буфер обмена: кнопки и команды ---------------------------------------------

    /// Скопированная кнопка: что она шлёт и где была.
    struct ClipButton {
        bool soft = false;
        std::string key;      ///< Аппаратная: имя кнопки пульта
        std::string label;    ///< Экранная: подпись
        std::string deviceId; ///< Кому шлёт
        std::string command;  ///< Что шлёт
    };

    /// Скопированные команды: имена и сигналы (сигналы — для вставки в другую конфигурацию).
    struct ClipCommands {
        int documentSerial = 0; ///< Из какой открытой конфигурации (см. documentSerial_)
        std::string deviceId;
        std::vector<std::pair<std::string, ir::IrSignal>> commands;
    };

    /// @return true, если есть что копировать (вырезать) в открытой вкладке.
    [[nodiscard]] bool canCopy(bool cut) const;

    /// @return true, если буфер можно вставить в открытую вкладку.
    [[nodiscard]] bool canPaste() const;

    /// Копирует (вырезает) выбранную кнопку или команды.
    void copySelection(bool cut);

    /// Вставляет из буфера: кнопку — в кнопки, команды — в устройство.
    void pasteClipboard();

    std::unique_ptr<ConfigDocument> document_; ///< Открытая конфигурация (nullptr — нет)
    int documentSerial_ = 0; ///< Номер открытой конфигурации (растёт при каждом открытии)
    std::variant<std::monostate, ClipButton, ClipCommands> clip_; ///< Буфер обмена программы
    undo::UndoStack undoStack_;                ///< История изменений открытой конфигурации

    AppHeader* header_ = nullptr;
    QPointer<ZoneEditorDialog> zoneEditorDialog_; ///< Окно редактора зон (одно, немодальное)
    QPointer<JournalDialog> journalDialog_; ///< Окно журнала (создаётся при первом открытии)
    QStackedWidget* screens_ = nullptr; ///< 0 — стартовый экран, 1 — конфигурация
    WelcomePage* welcome_ = nullptr;
    ConfigView* configView_ = nullptr;
    QLabel* summary_ = nullptr; ///< Сводка справа в строке состояния
    QToolButton* problems_ = nullptr; ///< «2 ошибки · 1 предупреждение» в строке состояния

    QAction* saveAction_ = nullptr;
    QAction* saveAsAction_ = nullptr;
    QAction* readRemoteAction_ = nullptr;
    QAction* chooseRemoteAction_ = nullptr;
    QAction* transferAction_ = nullptr;
    QString selectedRemote_ = "900";
    QAction* writeRemoteAction_ = nullptr;
    QAction* checkConfigAction_ = nullptr;
    QAction* compareConfigsAction_ = nullptr; ///< «Сравнить с другой конфигурацией…»
    QAction* versionsAction_ = nullptr;
    QAction* quickSearchAction_ = nullptr;
    quint64 editGeneration_ = 0;      ///< Растёт при каждой правке, отмене и смене документа
    quint64 autosavedGeneration_ = 0; ///< editGeneration_ последнего автосохранения
    QString pendingBackup_; ///< Резервная копия, сохранённая перед текущей записью
    RemoteSession* remote_ = nullptr;
    IrDatabaseService* irDatabase_ = nullptr; ///< База ИК-кодов Logitech
    GuardedProgressDialog* progress_ = nullptr;
    QString remoteFailureTitle_;               ///< Заголовок окна ошибки текущей операции с пультом
    std::function<void()> pendingRemoteAction_; ///< Ждёт конца проверки подключения (remoteReady)
    QTimer* problemsTimer_ = nullptr;          ///< Откладывает updateProblems() после серии правок
    QFileSystemWatcher* resourceWatcher_ = nullptr; ///< Файлы внешней папки ресурсов пультов
    QTimer* resourceTimer_ = nullptr;          ///< Одно перечитывание на серию сохранений
    QPointer<QThread> dbImportThread_;         ///< Перенос устройства из базы ИК-кодов
    QPointer<QThread> remoteLogThread_;        ///< Чтение системного журнала пульта
    QPointer<QDialog> remoteLogDialog_;        ///< Окно системного журнала пульта
    QAction* recoverIrAction_ = nullptr;       ///< «Восстановить ИК-данные из копии…» (нужен документ)
    QAction* usbSetupAction_ = nullptr;        ///< «Настроить USB…» (только Linux)
    bool remoteConnected_ = false;
    QAction* closeAction_ = nullptr;
};

} // namespace harmony::app
