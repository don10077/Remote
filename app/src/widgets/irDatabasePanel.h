#pragma once

/**
 * @file irDatabasePanel.h
 * @brief Вкладка настроек «База ИК-кодов»: откуда скачивать два файла архива (полные адреса),
 * куда их класть, где база; «Скачать» и «Проиндексировать» — отдельно.
 */

#include <QWidget>

class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;

namespace harmony::app {

class IrDatabaseService;

/// Настройки и состояние базы ИК-кодов.
class IrDatabasePanel final : public QWidget {
    Q_OBJECT

public:
    IrDatabasePanel(IrDatabaseService* service, QWidget* parent = nullptr);

    /// Записывает поля в настройки.
    void store();

    /// Ставит пути и адрес по умолчанию (сохраняются по store()).
    void loadDefaults();

private:
    /// Состояние базы и папки архива; кнопки — по тому, идёт ли работа.
    void updateStatus();

    IrDatabaseService* service_;
    QLabel* status_ = nullptr;
    QLineEdit* devicesUrl_ = nullptr;   ///< Адрес device_features_raw.jsonl.gz
    QLineEdit* protocolsUrl_ = nullptr; ///< Адрес AllProtocols.json.gz
    QLabel* files_ = nullptr;           ///< Есть ли файлы уже в папке
    QLineEdit* folder_ = nullptr;
    QLineEdit* file_ = nullptr;
    QPushButton* build_ = nullptr;
    QPushButton* download_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* message_ = nullptr;
};

} // namespace harmony::app
