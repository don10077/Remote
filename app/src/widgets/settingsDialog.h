#pragma once

/**
 * @file settingsDialog.h
 * @brief Окно «Настройки»: язык, запуск, связь с пультом, резервные копии.
 */

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QTabWidget;

namespace harmony::app {

class IrDatabasePanel;
class IrDatabaseService;

/// Настройки программы; «Сохранить» записывает их в QSettings.
class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    /**
     * @param irDatabase База ИК-кодов (для вкладки «База ИК-кодов»).
     * @param parent Родительское окно.
     */
    explicit SettingsDialog(IrDatabaseService* irDatabase, QWidget* parent = nullptr);

    /// Открывает вкладку «База ИК-кодов».
    void showIrDatabase();

    /// @return Язык изменён (нужен перезапуск).
    [[nodiscard]] bool languageChanged() const { return languageChanged_; }

private:
    /// Заполняет поля текущими настройками.
    void load();

    /// Ставит значения по умолчанию в поля открытой вкладки (сохраняются по «Сохранить»).
    void loadDefaults();

    /// Записывает поля в настройки.
    void store();

    QComboBox* language_ = nullptr;
    QComboBox* startup_ = nullptr;
    QComboBox* journalLevel_ = nullptr; ///< Что записывать в журнал
    QComboBox* theme_ = nullptr;        ///< Оформление: как в системе / светлое / тёмное
    QLineEdit* backupFolder_ = nullptr;
    QLineEdit* host_ = nullptr;
    QSpinBox* port_ = nullptr;
    QCheckBox* autoProbe_ = nullptr;
    QSpinBox* learnTimeout_ = nullptr;
    IrDatabasePanel* irDatabase_ = nullptr;
    QWidget* remotePage_ = nullptr; ///< Вкладка «Пульт» (для «По умолчанию» на ней)
    QTabWidget* tabs_ = nullptr;
    bool languageChanged_ = false;
};

} // namespace harmony::app
