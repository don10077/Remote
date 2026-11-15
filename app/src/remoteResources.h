#pragma once

#include <QByteArray>
#include <QJsonObject>
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
 * даже тогда, когда reload() отклонил их remote.json: зоны такой модели
 * всё равно нужно уметь открыть и исправить. Папка без remote.json не
 * считается моделью.
 * @param root Корень папки ресурсов; явный путь используется в тестах.
 */
QStringList modelIds(const QString& root = folder());

/// Прочитать ресурс по штатному пути :/res/... из последнего корректного снимка.
QByteArray data(const QString& path);
/**
 * @brief Описание пульта из последнего корректного снимка.
 *
 * Это remote.json пульта после перевода старой схемы (migrateRemote) и normalizeRemote():
 * у всех ячеек экранов есть `rect`. Других файлов описания пульта нет.
 * @param modelId id пульта (папка `remotes/<id>/`).
 * @return Пустой объект — пульта нет в снимке или он пропущен из-за ошибок.
 */
QJsonObject remote(const QString& modelId);

/// remote.json пульта как файл: текст на диске и описание, разобранное из этого же текста.
struct RemoteFile {
    QString path;        ///< путь от корня папки ресурсов: `remotes/<id>/remote.json`
    QByteArray text;     ///< текст файла; пусто — файла нет ни в папке, ни во встроенных ресурсах
    QByteArray current;  ///< text в текущей схеме: сам text либо переведённый migrateRemote() файл
    QJsonObject remote;  ///< описание, как его видит reload(): migrateRemote() и normalizeRemote()
    QString error;       ///< ошибка чтения или разбора JSON; пусто — описание разобрано
};
/**
 * @brief Прочитать remote.json пульта для правки.
 *
 * В отличие от remote() читает файл с диска, а не снимок: правка должна опираться на точный
 * текст файла (точечная замена фрагментов, проверка внешних изменений). Открывает и пульты,
 * которые reload() отклонил. Если файла в папке нет — встроенное описание.
 * @param modelId id пульта (папка `remotes/<id>/`).
 * @param root Корень папки ресурсов; явный путь используется в тестах.
 */
RemoteFile readRemote(const QString& modelId, const QString& root = folder());
/**
 * @brief Текст remote.json в текущей схеме.
 * @return Сам `text`, если перевод не нужен или текст не разобран; иначе описание после
 *         migrateRemote() с отступами (так редактор сразу записывает файл в новой схеме).
 */
QByteArray currentSchema(const QByteArray& text, const QString& modelId);
/**
 * @brief Поставить ячейки экрана по сетке.
 *
 * Ячейкам без `rect`, но с `col`/`row`, вычисляется `rect` в единицах `ref` по `grid`
 * (`cols`, `rows`, `gap`, по умолчанию 4; `colSpan`/`rowSpan` — по умолчанию 1) внутри
 * зоны плиток `grid.area` `[x, y, w, h]`, без неё — внутри `defaultArea`, а без обеих — по всему экрану;
 * без `kind` они становятся `ScreenSlot`. Остальное не меняется.
 * @param screen Экран из `screens` remote.json.
 * @return Экран с достроенными `rect`; без сетки — как есть.
 */
QJsonObject normalizeScreen(QJsonObject screen, const QJsonValue& defaultArea = {});
/// normalizeScreen() для всех экранов пульта; общая зона плиток — `tiles.area`.
QJsonObject normalizeRemote(QJsonObject remote);
/// Версия снимка для сброса кэшей изображений и геометрии.
quint64 revision();
/// Пользовательские модели, пропущенные последним reload() из-за ошибок: сами ошибки не мешают остальным пультам.
QStringList skipped();
/// Файлы remote.json в старой схеме, которые последний reload() перевёл на новую в памяти.
QStringList migratedByReload();
/**
 * Переводит описание пульта прошлой версии программы на текущую схему: connection
 * (driver/configFormat/transport → driver/format), поля style и compact экранов (→ картинка фона с шапкой
 * и panel.text)
 * и поле touch экранов (→ capabilities.touch пульта).
 * Недостающие значения берутся у встроенного описания того же пульта. Остальное не меняется.
 * @return true, если что-то изменилось.
 */
bool migrateRemote(QJsonObject& remote, const QString& modelId);
/// Ошибка конфигурации пульта, пропущенного последним reload(); пустая строка — ошибок нет.
QString modelError(const QString& id);
/// Идентификаторы пультов, пропущенных последним reload() из-за ошибок конфигурации.
QStringList skippedIds();

/**
 * @brief Файлы текущего снимка для выгрузки, пути относительно корня папки ресурсов.
 * @param modelId Модель (`remotes/<id>/…`); пустая строка — все ресурсы, включая общие `sprites/`.
 * @return Отсортированный список вида `remotes/900/remote.json`; пустой — модели нет в снимке.
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
