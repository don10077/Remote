/**
 * @file remoteCommands.cpp
 * @brief Команды, работающие с подключённым пультом.
 */

#include "remoteCommands.h"
#include "cliCommon.h"
#include "cliFiles.h"

#include "harmony/core/configDiff.h"
#include "harmony/core/configDocument.h"
#include "harmony/core/ir/irSignal.h"
#include "harmony/core/remotePreferences.h"
#include "harmony/transport/harmony900Link.h"
#include "harmony/transport/remoteServices.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <optional>
#include <vector>

#ifdef _WIN32
// GCC 16 для mingw-w64 определяет NOMINMAX в os_defines.h, поэтому проверяем
// перед определением: иначе повторное #define ловится как -Werror.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// shellapi.h требует windows.h, поэтому подключается после него.
#include <shellapi.h>
#endif

namespace harmony::cli {
namespace {

/// Нажат Ctrl-C: чтение или запись отменяются через printProgress, а не убийством процесса.
std::atomic<bool> cancelRequested{false};
static_assert(std::atomic<bool>::is_always_lock_free, "the flag is set from a signal handler");

#ifdef _WIN32
BOOL WINAPI onConsoleControl(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT) {
        cancelRequested = true;
        return TRUE;
    }
    return FALSE;
}
#else
void onInterrupt(int /*signal*/) {
    cancelRequested = true;
}
#endif

/// Перехватывает Ctrl-C на время обмена с пультом.
void catchInterrupt() {
#ifdef _WIN32
    SetConsoleCtrlHandler(onConsoleControl, TRUE);
#else
    std::signal(SIGINT, onInterrupt);
#endif
}

/// Печатает прогресс одной строкой: «Запись: 45%».
bool printProgress(harmony::transport::Stage stage, std::size_t done, std::size_t total) {
    using harmony::transport::Stage;
    const char* name = stage == Stage::Read      ? "Reading"
                       : stage == Stage::Write   ? "Writing"
                       : stage == Stage::Verify  ? "Verifying"
                       : stage == Stage::Restart ? "Restarting the remote"
                       : stage == Stage::SetTime ? "Clock"
                                                 : "Connecting";
    std::cout << "\r" << name << ": " << (total > 0 ? done * 100 / total : 0) << "%    " << std::flush;
    if (!cancelRequested) {
        return true;
    }
    if (stage == Stage::Read || stage == Stage::Write) {
        return false;
    }
    // Проверку, перезапуск и часы транспорт не прерывает: пульт уже применяет конфигурацию.
    static bool told = false;
    if (!told) {
        told = true;
        std::cout << "\nCannot cancel now: the remote is applying the configuration.\n";
    }
    return true;
}

/**
 * @brief Сохранить резервную копию, не затирая существующий файл.
 *
 * Если файл с таким именем уже есть, к имени добавляется номер (freePath); запись — через
 * временный файл (writeFileAtomic), поэтому при сбое на диске не остаётся недописанной копии.
 * @param wanted Желаемое имя.
 * @param bytes Содержимое.
 * @return Путь, куда записано; пусто — не удалось (сообщение уже напечатано).
 */
std::filesystem::path saveCopy(const std::filesystem::path& wanted, const std::vector<std::uint8_t>& bytes) {
    const std::filesystem::path path = harmony::cli::freePath(wanted);
    if (path.empty()) {
        std::cerr << "Error: no free file name next to " << wanted.string() << "\n";
        return {};
    }
    if (!harmony::cli::writeFileAtomic(path, bytes)) {
        std::cerr << "Error: could not write the file " << path.string() << "\n";
        return {};
    }
    if (path != wanted) {
        std::cout << wanted.string() << " already exists, saved under another name.\n";
    }
    return path;
}

/// Подпись значения настройки: «On», «24-hour» или само число.
std::string preferenceText(const harmony::RemotePreference& preference, const std::string& value) {
    for (const auto& item : preference.choices) {
        if (item.value == value) {
            return std::string(item.label) + " (" + value + ")";
        }
    }
    return value;
}

/// Допустимые значения настройки для подсказки.
std::string preferenceRange(const harmony::RemotePreference& preference) {
    if (preference.choices.empty()) {
        return std::to_string(preference.min) + "…" + std::to_string(preference.max);
    }
    std::string out;
    for (const auto& item : preference.choices) {
        out += (out.empty() ? "" : ", ") + std::string(item.value) + " = " + std::string(item.label);
    }
    return out;
}

} // namespace

/// Команда backup: конфигурация с пульта → файл .EZHex.
int runBackup(const std::filesystem::path& file, const std::string& host) {
    harmony::transport::Harmony900Link link;
    if (!connectRemote(link, host)) {
        return 2;
    }
    catchInterrupt();
    auto ezhex = link.readConfig(printProgress);
    std::cout << "\n";
    if (!ezhex) {
        std::cerr << "Error: " << ezhex.error().message << "\n";
        return 2;
    }
    const std::filesystem::path saved = saveCopy(file, *ezhex);
    if (saved.empty()) {
        return 2;
    }
    std::cout << "Saved: " << saved.string() << " (" << ezhex->size() << " bytes)\n";
    return 0;
}

/**
 * @brief Команда write: проверка, резервная копия с пульта, отличия, затем запись.
 * @param path Что записать.
 * @param host Адрес пульта.
 * @param force Писать и с ошибками проверки, и если копию с пульта не удалось разобрать.
 * @return 0 — записано, 2 — ошибка, 3 — в конфигурации ошибки (без --force).
 */
int runWrite(const std::filesystem::path& path, const std::string& host, bool force) {
    const auto document = harmony::ConfigDocument::open(path);
    if (!document) {
        std::cerr << "Error: " << document.error().message << "\n";
        return 2;
    }
    // Проверяем, что конфигурация собирается, ещё до связи с пультом.
    if (auto check = document->toEzHex(true); !check) {
        std::cerr << "Error: " << check.error().message << "\n";
        return 2;
    }
    // Как в приложении: с ошибками проверки часть кнопок на пульте молча не сработает.
    if (const auto problems = document->check();
        harmony::countProblems(problems, harmony::Problem::Severity::Error) > 0) {
        printProblems(problems);
        if (!force) {
            std::cerr << "Error: the configuration has errors; some buttons or steps will not work on the remote.\n"
                         "Fix them (harmony-cli check <path> --fix) or add --force to write anyway.\n";
            return 3;
        }
    }
    harmony::transport::Harmony900Link link;
    if (!connectRemote(link, host)) {
        return 2;
    }
    catchInterrupt();

    // Резервная копия обязательна: другого способа вернуть прежнюю конфигурацию нет.
    char stamp[32] = {};
    const std::time_t now = std::time(nullptr);
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", std::localtime(&now));
    const std::filesystem::path wantedBackup = std::string("harmony-backup-") + stamp + ".EZHex";
    auto current = link.readConfig(printProgress);
    std::cout << "\n";
    if (!current) {
        std::cerr << "Error: " << current.error().message << " — writing cancelled\n";
        return 2;
    }
    const std::filesystem::path backup = saveCopy(wantedBackup, *current);
    if (backup.empty()) {
        std::cerr << "Error: could not save the backup — writing cancelled\n";
        return 2;
    }
    std::cout << "Backup: " << backup.string() << "\n";

    // Настройки RF-бластеров — с пульта, как они сейчас (их меняют на самом пульте).
    const auto onRemote = harmony::ConfigDocument::fromEzHex(*current, "remote");
    if (onRemote) {
        std::cout << "Changes on the remote:\n";
        printChanges(harmony::compareConfigurations(*onRemote, *document));
    } else {
        std::cerr << "Warning: the remote's configuration could not be read (" << onRemote.error().message
                  << "): its RF blaster settings will be replaced by those of the file being written.\n";
        if (!force) {
            std::cerr << "Writing cancelled; add --force to write anyway.\n";
            return 2;
        }
    }
    const auto ezhex = onRemote ? document->toEzHexKeepingRemoteRf(*onRemote) : document->toEzHex(true);
    if (!ezhex) {
        std::cerr << "Error: " << ezhex.error().message << "\n";
        return 2;
    }
    if (cancelRequested) {
        std::cerr << "Writing cancelled. The backup is kept.\n";
        return 2;
    }
    auto written = link.writeConfig(*ezhex, printProgress);
    std::cout << "\n";
    if (!written) {
        std::cerr << "Error: " << written.error().message << "\n";
        return 2;
    }
    std::cout << "Done: configuration written, remote restarted.\n";
    return 0;
}

/// Команда learn: выучить сигнал и напечатать протокол и длительности.
int runLearn(const std::string& host) {
    harmony::transport::Harmony900Link link;
    if (!connectRemote(link, host)) {
        return 2;
    }
    std::cout << "Point the original remote at the Harmony and press a button (waiting 30 seconds)...\n";
    auto signal = link.learnIr({}, std::chrono::seconds(30));
    if (!signal) {
        std::cerr << "Error: " << signal.error().message << "\n";
        return 2;
    }
    std::cout << harmony::ir::describe(*signal) << "\nCarrier, Hz: " << signal->frequency
              << "\nDurations, µs (+pulse/−gap):";
    for (const int duration : signal->durations) {
        std::cout << " " << duration;
    }
    std::cout << "\n";
    return 0;
}

/**
 * @brief Команда settings: настройки пульта — подключённого или в файле конфигурации.
 * @param path Пусто — подключённый пульт.
 * @param host Адрес пульта.
 */
int runSettings(const std::filesystem::path& path, const std::string& host) {
    std::optional<harmony::ConfigDocument> document;
    if (!path.empty()) {
        auto opened = harmony::ConfigDocument::open(path);
        if (!opened) {
            std::cerr << "Error: " << opened.error().message << "\n";
            return 2;
        }
        document = std::move(*opened);
    }
    for (const auto& preference : harmony::remotePreferences()) {
        std::string value;
        if (document) {
            value = document->remotePreference(preference.key).value_or("— (the remote keeps its own)");
        } else {
            auto read = harmony::transport::readRemoteSetting(host, preference.endpoint());
            if (!read) {
                std::cerr << "Error: " << read.error().message << "\n";
                return 2;
            }
            value = *read;
        }
        std::cout << "  " << preference.key << "\t" << preference.label << ": "
                  << preferenceText(preference, value) << "\n";
    }
    return 0;
}

/**
 * @brief Команда set: меняет настройку на пульте или в файле конфигурации.
 * @param path Пусто — подключённый пульт.
 * @param host Адрес пульта.
 */
int runSet(std::string_view key, const std::string& value, const std::filesystem::path& path,
           const std::string& host) {
    const auto* preference = harmony::findRemotePreference(key);
    if (preference == nullptr) {
        std::cerr << "Unknown setting: " << key << ". Settings:";
        for (const auto& item : harmony::remotePreferences()) {
            std::cerr << " " << item.key;
        }
        std::cerr << "\n";
        return 1;
    }
    if (auto checked = harmony::checkPreferenceValue(*preference, value); !checked) {
        std::cerr << "Error: " << checked.error().message << " (" << preferenceRange(*preference) << ")\n";
        return 1;
    }
    if (path.empty()) {
        auto written = harmony::transport::writeRemoteSetting(host, preference->endpoint(), value);
        if (!written) {
            std::cerr << "Error: " << written.error().message << "\n";
            return 2;
        }
        std::cout << preference->label << ": " << preferenceText(*preference, value) << "\n";
        return 0;
    }
    auto document = harmony::ConfigDocument::open(path);
    if (!document) {
        std::cerr << "Error: " << document.error().message << "\n";
        return 2;
    }
    if (document->ezhexFile().empty()) {
        std::cerr << "Error: remote settings are stored only in an .EZHex file\n";
        return 2;
    }
    if (auto changed = document->setRemotePreference(key, value); !changed) {
        std::cerr << "Error: " << changed.error().message << "\n";
        return 2;
    }
    if (auto saved = saveArchive(*document); !saved) {
        std::cerr << "Error: " << saved.error().message << "\n";
        return 2;
    }
    std::cout << preference->label << ": " << preferenceText(*preference, value)
              << " — saved; it takes effect when the configuration is written\n";
    return 0;
}

/// Команда log или shell: вывод команды в консоли пульта.
int runShell(const std::string& command, const std::string& host) {
    auto output = command.empty() ? harmony::transport::readRemoteLog(host)
                                  : harmony::transport::runRemoteShell(host, command);
    if (!output) {
        std::cerr << "Error: " << output.error().message << "\n";
        return 2;
    }
    std::cout << *output << "\n";
    return 0;
}

} // namespace harmony::cli
