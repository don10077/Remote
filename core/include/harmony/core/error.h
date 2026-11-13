#pragma once

/**
 * @file error.h
 * @brief Единый тип ошибки и результат операций ядра.
 *
 * Ядро не бросает исключения при ожидаемых ошибках (нет файла, битый XML,
 * пульт не отвечает): такие функции возвращают Result<T>.
 * Исключения остаются только для нарушений контракта (ошибок программиста).
 */

#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace harmony {

/**
 * @brief Категория ошибки — по ней вызывающий код решает, как реагировать.
 */
enum class ErrorCode {
    Io,           ///< Ошибка ввода-вывода: файл, сокет
    Format,       ///< Данные не соответствуют ожидаемому формату
    NotFound,     ///< Запрошенный объект не найден
    Unsupported,  ///< Операция или версия формата не поддерживается
    Remote,       ///< Пульт вернул ошибку или не ответил
    Cancelled,    ///< Операция отменена пользователем
};

/**
 * @brief Описание ошибки: категория и понятный человеку текст.
 *
 * Текст хранится и готовым (message — по-английски, для лога и консоли), и
 * шаблоном с аргументами (format, args), чтобы интерфейс мог показать его
 * на своём языке: шаблон — ключ перевода, аргументы (пути, имена) не
 * переводятся. Ошибка может уточнять другую (cause): «файл X: <причина>».
 */
struct Error {
    ErrorCode code;                     ///< Категория ошибки
    std::string message;                ///< Готовый текст: шаблон с аргументами и причина
    std::string format;                 ///< Шаблон с %1…%9 (по-английски, ключ перевода)
    std::vector<std::string> args;      ///< Аргументы шаблона
    std::shared_ptr<const Error> cause; ///< Причина (если есть): message = текст + «: » + причина

    /**
     * @brief Создаёт ошибку с готовым текстом (он же шаблон без аргументов).
     * @param errorCode Категория ошибки.
     * @param text Описание ошибки.
     */
    Error(ErrorCode errorCode, std::string text)
        : code(errorCode), message(text), format(std::move(text)) {}

    /**
     * @brief Создаёт ошибку по шаблону.
     * @param errorCode Категория ошибки.
     * @param pattern Шаблон с %1…%9.
     * @param arguments Аргументы.
     * @param reason Причина; nullptr — нет.
     */
    Error(ErrorCode errorCode, std::string pattern, std::vector<std::string> arguments,
          std::shared_ptr<const Error> reason = nullptr)
        : code(errorCode), message(fill(pattern, arguments)), format(std::move(pattern)),
          args(std::move(arguments)), cause(std::move(reason)) {
        if (cause) {
            message += ": " + cause->message;
        }
    }

    /**
     * @brief Подставляет аргументы в шаблон: %1 → args[0] и т. д. (как QString::arg).
     * @param pattern Шаблон.
     * @param arguments Аргументы.
     */
    [[nodiscard]] static std::string fill(std::string_view pattern, const std::vector<std::string>& arguments) {
        std::string out;
        out.reserve(pattern.size() + 32);
        for (std::size_t i = 0; i < pattern.size(); ++i) {
            const char c = pattern[i];
            if (c == '%' && i + 1 < pattern.size() && pattern[i + 1] >= '1' && pattern[i + 1] <= '9') {
                const auto index = static_cast<std::size_t>(pattern[i + 1] - '1');
                if (index < arguments.size()) {
                    out += arguments[index];
                    ++i;
                    continue;
                }
            }
            out += c;
        }
        return out;
    }
};

/**
 * @brief Результат операции: значение типа T или Error.
 *
 * Пример:
 * @code
 * Result<RemoteConfig> cfg = readConfig(path);
 * if (!cfg) { log(cfg.error().message); return; }
 * @endcode
 */
template <typename T>
using Result = std::expected<T, Error>;

/**
 * @brief Удобный конструктор неуспешного результата.
 * @param code Категория ошибки.
 * @param message Описание ошибки.
 * @return Объект std::unexpected, неявно приводимый к любому Result<T>.
 */
inline std::unexpected<Error> makeError(ErrorCode code, std::string message) {
    return std::unexpected<Error>(Error(code, std::move(message)));
}

/**
 * @brief Неуспешный результат по шаблону: makeError(ErrorCode::Io, "файл не найден: %1", {path}).
 * @param code Категория ошибки.
 * @param pattern Шаблон с %1…%9 (по-русски; он же ключ перевода в интерфейсе).
 * @param args Аргументы — не переводятся (пути, имена, числа).
 */
inline std::unexpected<Error> makeError(ErrorCode code, std::string pattern, std::vector<std::string> args) {
    return std::unexpected<Error>(Error(code, std::move(pattern), std::move(args)));
}

/**
 * @brief Ошибка, уточняющая другую: «<шаблон>: <причина>».
 *
 * Пример: wrapError(cause, "%1", {path}) → «D:/a.xml: ошибка XML в строке 3».
 * @param cause Причина (её категория сохраняется).
 * @param pattern Шаблон уточнения.
 * @param args Аргументы шаблона.
 */
inline std::unexpected<Error> wrapError(const Error& cause, std::string pattern, std::vector<std::string> args = {}) {
    return std::unexpected<Error>(
        Error(cause.code, std::move(pattern), std::move(args), std::make_shared<const Error>(cause)));
}

/// То же с явной категорией ошибки.
inline std::unexpected<Error> wrapError(ErrorCode code, const Error& cause, std::string pattern,
                                        std::vector<std::string> args = {}) {
    return std::unexpected<Error>(
        Error(code, std::move(pattern), std::move(args), std::make_shared<const Error>(cause)));
}

} // namespace harmony
