/**
 * @file ezhex.cpp
 * @brief Разбор и сборка контейнера .EZHex.
 */

#include "harmony/core/formats/ezhex.h"

#include <charconv>
#include <optional>
#include <string_view>

namespace harmony::formats {

namespace {

constexpr std::string_view kEndTag = "</INFORMATION>";
constexpr std::uint8_t kChecksumSeed = 0x69;

/// Текст элемента <name> в заголовке (первое вхождение после @p from); nullopt — нет.
std::optional<std::string> field(std::string_view header, std::string_view name, std::size_t from = 0) {
    const std::string open = "<" + std::string(name) + ">";
    const std::string close = "</" + std::string(name) + ">";
    const auto start = header.find(open, from);
    if (start == std::string_view::npos) {
        return std::nullopt;
    }
    const auto end = header.find(close, start);
    if (end == std::string_view::npos) {
        return std::nullopt;
    }
    return std::string(header.substr(start + open.size(), end - start - open.size()));
}

std::optional<long long> number(const std::optional<std::string>& text) {
    if (!text) {
        return std::nullopt;
    }
    long long value = 0;
    const auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), value);
    return error == std::errc{} && end == text->data() + text->size() ? std::optional(value) : std::nullopt;
}

/// Заменяет текст первого элемента <name> в заголовке.
/// @return false — элемента или его закрывающего тега нет (заголовок испорчен).
bool replaceField(std::string& header, std::string_view name, const std::string& value) {
    const std::string open = "<" + std::string(name) + ">";
    const std::string close = "</" + std::string(name) + ">";
    const auto start = header.find(open);
    if (start == std::string::npos) {
        return false;
    }
    const auto end = header.find(close, start + open.size());
    if (end == std::string::npos) {
        return false;
    }
    header.replace(start + open.size(), end - start - open.size(), value);
    return true;
}

} // namespace

std::uint8_t ezHexChecksum(std::span<const std::uint8_t> payload) {
    std::uint8_t sum = kChecksumSeed;
    for (const std::uint8_t byte : payload) {
        sum ^= byte;
    }
    return sum;
}

Result<EzHex> parseEzHex(std::span<const std::uint8_t> file) {
    const std::string_view text(reinterpret_cast<const char*>(file.data()), file.size());
    const auto tag = text.find(kEndTag);
    if (!text.starts_with("<?xml") || tag == std::string_view::npos) {
        return makeError(ErrorCode::Format, "this is not a remote configuration file (.EZHex): no header");
    }
    // Заголовок — по конец строки после закрывающего тега (CRLF; LF тоже принимаем).
    std::size_t headerEnd = tag + kEndTag.size();
    if (text.substr(headerEnd, 2) == "\r\n") {
        headerEnd += 2;
    } else if (text.substr(headerEnd, 1) == "\n") {
        headerEnd += 1;
    }

    EzHex ezhex;
    ezhex.header = std::string(text.substr(0, headerEnd));
    ezhex.payload.assign(file.begin() + static_cast<std::ptrdiff_t>(headerEnd), file.end());

    const auto size = number(field(ezhex.header, "BINARYDATASIZE"));
    const auto checksum = number(field(ezhex.header, "CHECKSUM"));
    if (!size || static_cast<std::size_t>(*size) != ezhex.payload.size()) {
        return makeError(ErrorCode::Format, "the .EZHex file is truncated or corrupt: data size does not match the "
                                            "header");
    }
    if (!checksum || *checksum != ezHexChecksum(ezhex.payload)) {
        return makeError(ErrorCode::Format, "the .EZHex file is corrupt: checksum mismatch");
    }

    // Чей пульт — из <INTENDEDVERSION>.
    const auto intended = ezhex.header.find("<INTENDEDVERSION>");
    if (intended != std::string::npos) {
        RemoteIdentity& id = ezhex.identity;
        id.architecture = static_cast<int>(number(field(ezhex.header, "PROTOCOL", intended)).value_or(id.architecture));
        id.skin = static_cast<int>(number(field(ezhex.header, "SKIN", intended)).value_or(id.skin));
        id.flash = field(ezhex.header, "FLASH", intended).value_or(id.flash);
        id.board = field(ezhex.header, "BOARD", intended).value_or(id.board);
        id.softwareType = static_cast<int>(number(field(ezhex.header, "SOFTWARETYPE", intended)).value_or(id.softwareType));
    }
    return ezhex;
}

std::string makeEzHexHeader(const RemoteIdentity& identity, std::span<const std::uint8_t> payload) {
    // Тот же текст, что пишут concordance и программы Logitech; строки — CRLF.
    const std::string version = "                    <PROTOCOL>" + std::to_string(identity.architecture) + "</PROTOCOL>\r\n"
                                "                    <SKIN>" + std::to_string(identity.skin) + "</SKIN>\r\n"
                                "                    <FLASH>" + identity.flash + "</FLASH>\r\n"
                                "                    <BOARD>" + identity.board + "</BOARD>\r\n"
                                "                    <SOFTWARETYPE>" + std::to_string(identity.softwareType) + "</SOFTWARETYPE>\r\n";
    const std::string intended = "        <PROTOCOL>" + std::to_string(identity.architecture) + "</PROTOCOL>\r\n"
                                 "        <SKIN>" + std::to_string(identity.skin) + "</SKIN>\r\n"
                                 "        <FLASH>" + identity.flash + "</FLASH>\r\n"
                                 "        <BOARD>" + identity.board + "</BOARD>\r\n"
                                 "        <SOFTWARETYPE>" + std::to_string(identity.softwareType) + "</SOFTWARETYPE>\r\n";
    return "<?xml version=\"1.0\"?>\r\n"
           "<INFORMATION>\r\n"
           "    <USERMESSAGES>\r\n"
           "        <USERMESSAGE>\r\n"
           "            <VERSIONS>\r\n"
           "                <VERSION>\r\n" + version +
           "                </VERSION>\r\n"
           "            </VERSIONS>\r\n"
           "            <TYPE>DoNothing</TYPE>\r\n"
           "            <ABORTPROCESSING>True</ABORTPROCESSING>\r\n"
           "        </USERMESSAGE>\r\n"
           "        <USERMESSAGE>\r\n"
           "            <VERSIONS>\r\n"
           "                <VERSION></VERSION>\r\n"
           "            </VERSIONS>\r\n"
           "            <TYPE>Warning</TYPE>\r\n"
           "            <TEXT>This configuration file is not compatible with your Harmony Remote.</TEXT>\r\n"
           "        </USERMESSAGE>\r\n"
           "        <USERMESSAGE>\r\n"
           "            <VERSIONS>\r\n"
           "                <VERSION></VERSION>\r\n"
           "            </VERSIONS>\r\n"
           "            <TYPE>Abort</TYPE>\r\n"
           "        </USERMESSAGE>\r\n"
           "    </USERMESSAGES>\r\n"
           "    <INTENDEDVERSION>\r\n" + intended +
           "    </INTENDEDVERSION>\r\n"
           "    <TRANSFERTYPE>Winsock</TRANSFERTYPE>\r\n"
           "    <CONFIGURATION>\r\n"
           "        <CONFIGOPTION>\r\n"
           "            <KEY>InstructionNoHarmony</KEY>\r\n"
           "            <TITLE>No Harmony Detected</TITLE>\r\n"
           "            <IMAGE>0</IMAGE>\r\n"
           "            <VALUE>Plug the USB cable into your Harmony, or press any button on your Harmony to wake it up.</VALUE>\r\n"
           "        </CONFIGOPTION>\r\n"
           "    </CONFIGURATION>\r\n"
           "    <BINARYDATASIZE>" + std::to_string(payload.size()) + "</BINARYDATASIZE>\r\n"
           "    <CHECKSUM>" + std::to_string(ezHexChecksum(payload)) + "</CHECKSUM>\r\n"
           "</INFORMATION>\r\n";
}

Result<Bytes> writeEzHex(const EzHex& ezhex) {
    std::string header = ezhex.header;
    if (header.empty()) {
        header = makeEzHexHeader(ezhex.identity, ezhex.payload);
    } else {
        // Без обоих полей файл получился бы с устаревшими размером и контрольной суммой.
        const bool sizeUpdated =
            replaceField(header, "BINARYDATASIZE", std::to_string(ezhex.payload.size()));
        const bool checksumUpdated =
            replaceField(header, "CHECKSUM", std::to_string(ezHexChecksum(ezhex.payload)));
        if (!sizeUpdated || !checksumUpdated) {
            return makeError(ErrorCode::Format,
                             "the .EZHex header is corrupt: no <BINARYDATASIZE> or <CHECKSUM>");
        }
    }
    Bytes file(header.begin(), header.end());
    file.insert(file.end(), ezhex.payload.begin(), ezhex.payload.end());
    return file;
}

} // namespace harmony::formats
