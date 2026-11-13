#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace harmony::app::remoteResources {
/// Рабочая папка resources рядом с исполняемым файлом, независимо от настроек и cwd.
QString folder();
/**
 * @brief Создать папку и восстановить отсутствующие штатные ресурсы.
 * @param root Рабочая папка; явный путь используется также для изолированных тестов.
 * Существующие файлы, включая изменённые и некорректные, не перезаписываются.
 * @return Пустая строка — успех, иначе диагностика с путём.
 */
QString prepareFolder(const QString& root = folder());
/// Подготовить папку и атомарно прочитать её. Ошибка сохраняет прежний снимок.
QString reload(const QString& root = folder());

/**
 * @brief Модели для редактора зон: проверенный каталог и папки внешних ресурсов.
 *
 * В отличие от harmony::remoteProfiles включает новые папки `remotes/<id>/`
 * даже тогда, когда reload() отклонил их profile.json: зоны такой модели
 * всё равно нужно уметь открыть и исправить. Папка без profile.json не
 * считается моделью.
 * @param root Корень папки ресурсов; явный путь используется в тестах.
 */
QStringList modelIds(const QString& root = folder());

/// Прочитать ресурс по штатному пути :/res/... из последнего корректного снимка.
QByteArray data(const QString& path);
/// Версия снимка для сброса кэшей изображений и геометрии.
quint64 revision();

/**
 * @brief Файлы текущего снимка для выгрузки, пути относительно корня папки ресурсов.
 * @param modelId Модель (`remotes/<id>/…`); пустая строка — все ресурсы,
 *                включая общие `layouts.json` и `sprites/`.
 * @return Отсортированный список вида `remotes/900/zones.json`; пустой — модели нет в снимке.
 */
QStringList exportFiles(const QString& modelId);

/**
 * @brief Записать файлы снимка в папку в той же структуре, что читает reload().
 *
 * Существующие файлы заменяются (через QSaveFile), недостающие папки создаются.
 * @param target Корень папки ресурсов.
 * @param files Пути из exportFiles().
 * @return Пустая строка — успех, иначе диагностика с путём файла.
 */
QString exportTo(const QString& target, const QStringList& files);

/**
 * @brief Что наблюдать во внешней папке для автоперечитывания.
 *
 * Сама папка, `remotes/`, папки моделей, `sprites/` с подпапками и файлы
 * .json/.png/.svg в них. Папки нужны, чтобы заметить новые файлы и сохранение
 * через переименование, файлы — правку на месте.
 * @return Пустой список, если папка ресурсов не существует.
 */
QStringList watchPaths(const QString& root = folder());
}
