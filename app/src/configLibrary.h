#pragma once

/**
 * @file configLibrary.h
 * @brief Список конфигураций пользователя: недавние, папка по умолчанию, открытие при запуске.
 *
 * Хранится в QSettings. Конфигурация — папка с UserConfiguration.xml.
 */

#include <QString>
#include <QStringList>

namespace harmony::app::library {

/// Сколько недавних конфигураций помнить.
inline constexpr int kMaxRecent = 12;

/// @return Папки недавних конфигураций, последняя открытая — первой.
QStringList recent();

/**
 * @brief Поднимает папку в начало списка недавних (или добавляет).
 * @param folder Папка конфигурации.
 */
void touch(const QString& folder);

/// Убирает папку из списка недавних (и из открытия при запуске, если это она).
void forget(const QString& folder);

/// @return Папка, которую открывать при запуске; пусто — показывать окно выбора.
QString startupFolder();

/**
 * @brief Задаёт папку для открытия при запуске.
 * @param folder Папка; пустая строка — при запуске показывать окно выбора.
 */
void setStartupFolder(const QString& folder);

/// @return Папка, где создаются новые конфигурации: «Документы/Harmony».
QString defaultLocation();

/// @return Последняя папка, из которой открывали файл (для диалогов).
QString lastDialogDir();

/**
 * @brief Папка резервных копий с пульта (создаётся при первом обращении).
 * @return «Документы/Harmony/BackUp».
 */
QString backupFolder();

/**
 * @brief Новый файл резервной копии: «harmony900-20261026-2015.EZHex» в папке копий.
 * @param prefix Начало имени (модель пульта).
 */
QString newBackupFile(const QString& prefix = QStringLiteral("harmony900"));

/// Запоминает папку последнего диалога.
void setLastDialogDir(const QString& dir);

/// @return Одинаковое написание пути для сравнения (абсолютный, без лишних «/»).
QString normalized(const QString& folder);

} // namespace harmony::app::library
