#pragma once

#include "../app/src/remoteResources.h"
#include <QString>

/**
 * @file profileNames.h
 * @brief Пути к файлам модели для тестов — из тех же констант, что и в программе.
 *
 * Имена заданы один раз (`remoteResources::kRemotesDir`, `kSpritesDir`, `kProfileFile`,
 * `kPhotoFile`, `kLegacyProfileFile`). Тесты подставляют их через эти помощники, а не
 * повторяют литерал: иначе переименование снова пришлось бы размазывать по всем тестам.
 * Сами значения имён фиксирует один тест — `RemoteResources.ProfileFilePathPrefersNewName`.
 */
namespace harmony::testpaths {
using app::remoteResources::kPhotoFile;
using app::remoteResources::kProfileFile;
using app::remoteResources::kLegacyProfileFile;
using app::remoteResources::kLegacyProfileBackup;
using app::remoteResources::kRemotesDir;
using app::remoteResources::kResourcePrefix;

/// Путь файла модели от корня папки ресурсов: `remotes/<id>/<имя>`.
inline QString modelKey(const QString& modelId, const QString& name) {
    return kRemotesDir + QLatin1Char('/') + modelId + QLatin1Char('/') + name;
}
/// Путь профиля от корня папки ресурсов: `remotes/<id>/profile.json`.
inline QString profileKey(const QString& modelId) { return modelKey(modelId, kProfileFile); }
/// Путь профиля под старым именем: `remotes/<id>/remote.json` (только для проверок совместимости).
inline QString legacyKey(const QString& modelId) { return modelKey(modelId, kLegacyProfileFile); }
/// Резервная копия старого профиля: `remotes/<id>/remote.json.bak`.
inline QString backupKey(const QString& modelId) { return modelKey(modelId, kLegacyProfileBackup); }
/// Путь фото модели от корня папки ресурсов: `remotes/<id>/photo.png`.
inline QString photoKey(const QString& modelId) { return modelKey(modelId, kPhotoFile); }
/// Встроенный профиль модели: `:/res/remotes/<id>/profile.json`.
inline QString builtinProfile(const QString& modelId) { return kResourcePrefix + profileKey(modelId); }
/// Встроенное фото модели: `:/res/remotes/<id>/photo.png`.
inline QString builtinPhoto(const QString& modelId) { return kResourcePrefix + photoKey(modelId); }
}
