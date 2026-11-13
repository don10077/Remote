#pragma once

/**
 * @file writePreviewDialog.h
 * @brief Окно перед записью на пульт: что на нём изменится.
 *
 * Конфигурацию с пульта уже прочитали (и сохранили резервной копией);
 * окно показывает отличия записываемой от неё (compareConfigurations()):
 * новые, удаляемые и изменённые устройства и активности с подробностями.
 */

#include "harmony/core/configDiff.h"

#include <QDialog>

class QTreeWidget;

namespace harmony::app {

/**
 * @brief Заполняет дерево отличиями: разделы «Устройства» и «Активности», в них — что изменится.
 * @param tree Дерево (очищается).
 * @param changes Отличия.
 * @param removedPattern Подпись удаляемого с %1 — именем («− %1 (removed from the remote)»).
 * @param addedPattern Подпись нового; пусто — «+ %1 (new)».
 */
void fillChangeTree(QTreeWidget* tree, const std::vector<ConfigChange>& changes,
                    const QString& removedPattern, const QString& addedPattern = {});

/// Что изменится на пульте.
class WritePreviewDialog final : public QDialog {
    Q_OBJECT

public:
    /**
     * @brief Создаёт окно.
     * @param changes Отличия (пусто — на пульте то же самое).
     * @param backupFile Куда сохранена текущая конфигурация пульта.
     * @param parent Родительское окно.
     */
    WritePreviewDialog(const std::vector<ConfigChange>& changes, const QString& backupFile,
                       QWidget* parent = nullptr);
};

} // namespace harmony::app
