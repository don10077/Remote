#pragma once

#include <QHash>
#include <QIcon>
#include <QPixmap>
#include <QString>

namespace harmony::app {

/**
 * Имя файла значка активности (res/activity_icons) по типу активности.
 * Значки взяты из прошивки пульта: там они тоже выбираются по типу активности.
 * @param type тип из конфига, например "VirtualDvd"
 */
inline QString activityIconName(const QString& type) {
    static const QHash<QString, QString> names = {
        {"VirtualCdJukebox", "listen_to_jukebox"},
        {"VirtualCdMulti", "listen_to_cd"},
        {"VirtualDvd", "watch_dvd"},
        {"VirtualGameConsole", "play_a_game"},
        {"VirtualGeneric", "generic"},
        {"VirtualLaserdisc", "watch_laserdisc"},
        {"VirtualMediaCenterMusic", "listen_to_music"},
        {"VirtualMediaCenterPictures", "show_my_media_center"},
        {"VirtualMediaCenterTv", "watch_tv_mediacenter"},
        {"VirtualMediaCenterVideos", "watch_videos"},
        {"VirtualMinidisc", "listen_to_minidisc"},
        {"VirtualMusicServer", "listen_to_mp3"},
        {"VirtualPresentation", "show_presentation"},
        {"VirtualPvr", "watch_dvr_pvr"},
        {"VirtualRadioSimple", "listen_to_radio"},
        {"VirtualReplayTV", "watch_replay_tv"},
        {"VirtualSatelliteMusic", "listen_to_satellite"},
        {"VirtualTape", "listen_to_tape"},
        {"VirtualTeletext", "use_teletext"},
        {"VirtualTelevisionN", "watch_tv"},
        {"VirtualTiVo", "watch_tivo"},
        {"VirtualVcr", "watch_vcr"},
        {"VirtualVcrRecorder", "record_to_vcr"},
    };
    // неизвестные типы рисуются общим значком
    return names.value(type, QStringLiteral("generic"));
}

/** значок активности (картинка 80x68 из ресурсов); грузится один раз и кэшируется по типу */
inline const QPixmap& activityPixmap(const QString& type) {
    static QHash<QString, QPixmap> cache;
    auto it = cache.find(type);
    if (it == cache.end()) {
        it = cache.insert(type, QPixmap(QStringLiteral(":/res/activity_icons/") +
                                        activityIconName(type) + QStringLiteral(".png")));
    }
    return it.value();
}

/** то же как QIcon, для таблиц и боковой панели */
inline QIcon activityIcon(const QString& type) {
    return QIcon(activityPixmap(type));
}

} // namespace harmony::app
