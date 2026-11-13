#pragma once

/**
 * @file addDeviceDialog.h
 * @brief Окно «Добавить устройство»: из базы ИК-кодов Logitech или из другой конфигурации.
 *
 * Устройство пульта — это не только имя: в нём ИК-коды, состояния питания
 * и входов. Готовое описание берётся из базы Logitech (поиск по
 * производителю и модели) или из другой конфигурации — резервной копии с
 * пульта или встроенного примера — вместе со всеми командами.
 */

#include "harmony/core/configDocument.h"

#include <QDialog>

#include <memory>
#include <string>
#include <vector>

class QComboBox;
class QLineEdit;
class QTabWidget;
class QTimer;
class QLabel;
class QListWidget;
class QPushButton;

namespace harmony::app {

class IrDatabaseService;
class RemoteSession;

/// Выбор конфигурации-источника и устройств из неё.
class AddDeviceDialog final : public QDialog {
    Q_OBJECT

public:
    /// Код завершения exec(): вместо переноса выбрано «Новое устройство…».
    static constexpr int kNewDevice = 2;
    /// Код завершения exec(): база не собрана — открыть настройки базы.
    static constexpr int kOpenSettings = 3;

    /**
     * @brief Создаёт окно и сразу загружает первый источник.
     * @param target Конфигурация, куда добавляются устройства (чтобы пометить уже имеющиеся).
     * @param currentFolder Папка текущей конфигурации — её нет среди источников.
     * @param irDatabase База ИК-кодов.
     * @param session Связь с пультом — для поиска модели по родному пульту.
     * @param parent Родительское окно.
     */
    AddDeviceDialog(const model::RemoteConfig& target, const QString& currentFolder,
                    IrDatabaseService* irDatabase, RemoteSession* session, QWidget* parent = nullptr);
    ~AddDeviceDialog() override;

    /// @return Загруженная конфигурация-источник (nullptr — не загрузилась).
    [[nodiscard]] const ConfigDocument* source() const { return source_.get(); }

    /// @return Отмеченные устройства источника (id) в порядке списка.
    [[nodiscard]] std::vector<std::string> selectedDevices() const;

    /// @return Выбрано устройство из базы (а не из другой конфигурации).
    [[nodiscard]] bool fromDatabase() const;

    /// @return Номер выбранного устройства в каталоге базы; -1 — не выбрано.
    [[nodiscard]] int databaseEntry() const;

private:
    /// Загружает источник и заполняет список устройств.
    void loadSource(const QString& source);

    /// Добавляет папку в список источников и выбирает её.
    void addSourceFolder(const QString& folder);

    /// Обновляет кнопку «Добавить» по числу отмеченных устройств.
    void updateButton();

    /// Страница «База Logitech».
    QWidget* makeDatabasePage();

    /// Ищет в базе по тексту поля поиска.
    void searchDatabase();

    /// Показывает подробности выбранного устройства базы.
    void showDatabaseDevice();

    /// Окно «Найти по пульту»; выбранная модель сразу добавляется.
    void findByRemote();

    const model::RemoteConfig& target_;
    std::unique_ptr<ConfigDocument> source_;

    IrDatabaseService* irDatabase_;
    RemoteSession* session_;
    int pickedEntry_ = -1; ///< Модель, найденная по пульту
    QTabWidget* tabs_ = nullptr;
    QLineEdit* search_ = nullptr;
    QListWidget* results_ = nullptr;
    QLabel* found_ = nullptr;
    QLabel* details_ = nullptr;
    QTimer* searchTimer_ = nullptr;

    QComboBox* sources_ = nullptr;
    QListWidget* list_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* add_ = nullptr;
};

} // namespace harmony::app
