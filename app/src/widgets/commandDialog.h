#pragma once

/**
 * @file commandDialog.h
 * @brief Компактное окно ИК-команды: протокол, адрес, код и биты.
 */

#include "harmony/core/configDocument.h"

#include <QDialog>

namespace harmony::app {

/// Подробности одной ИК-команды (только просмотр и копирование).
class CommandDialog final : public QDialog {
    Q_OBJECT

public:
    /**
     * @param device Имя устройства (для заголовка).
     * @param command Имя команды.
     * @param details Сигнал и что о нём известно.
     * @param parent Родительское окно.
     */
    CommandDialog(const QString& device, const QString& command, const ConfigDocument::CommandDetails& details,
                  QWidget* parent = nullptr);
};

} // namespace harmony::app
