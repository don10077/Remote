#pragma once

/**
 * @file fileProtocolReader.h
 * @brief Читатель файлового протокола: только те файлы, которые семейство подтверждает.
 *
 * Эти пульты знают своё хранилище по имени файла, а не по адресу памяти: host открывает путь,
 * получает ручку, читает через неё и закрывает. Отправляются четыре команды — ping без
 * параметров, open для чтения, read и close. Ни записи, ни коммита, ни управления устройством
 * здесь нет.
 *
 * Список файлов — данные семейства (`RemoteFamily::answerableFiles`), потому что путь на этом
 * протоколе может быть действием: `/sys/factoryreset` и `/sys/reboot` открываются для чтения,
 * а имена группы `/tde/haptic` действуют на открывании. Поэтому вне списка читатель ничего не
 * отправляет.
 */

#include "harmony/core/error.h"
#include "harmony/core/remoteFamily.h"
#include "harmony/transport/hidChannel.h"
#include "harmony/transport/remoteLink.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace harmony::transport {

/// Что читатель принёс: файл, его форму из данных семейства и ровно столько байтов, сколько
/// заявил open.
struct FileContent {
    std::string modelId;                          ///< модель, для которой решаем (строка каталога)
    std::string path;                             ///< прочитанный путь
    harmony::FileShape shape = harmony::FileShape::Unknown;  ///< форма ответа — из данных семейства
    std::size_t statedSize = 0;                   ///< размер из ответа open: единственный конец файла
    std::vector<std::uint8_t> bytes = {};          ///< данные; короткий ответ остаётся коротким
    bool complete = false;                        ///< bytes.size() == statedSize
};

/// Читает один файл семейного списка. Путь вне списка или неподтверждённый для модели — отказ
/// до отправки чего-либо. Файл закрывается после каждого чтения.
class FileProtocolReader {
public:
    FileProtocolReader(HidChannel& channel, std::string_view modelId)
        : channel_(channel), modelId_(modelId) {}

    [[nodiscard]] Result<FileContent> read(std::string_view path, const ProgressHandler& progress = {});

private:
    HidChannel& channel_;
    std::string modelId_;
};

} // namespace harmony::transport
