/**
 * @file xmlDom.cpp
 * @brief Помощники правки XML пульта без потерь.
 */

#include "xmlDom.h"

#include <sstream>
#include <string_view>
#include <vector>

namespace harmony::detail {

namespace {

/**
 * @brief Как записан каждый элемент исходника: true — «<X/>», false — парой тегов.
 *
 * Элементы идут в порядке появления в тексте — в том же порядке их обходит
 * pugixml (обход в глубину), поэтому флаги сопоставляются по номеру.
 * Своё сканирование, а не offset_debug() из pugixml: так поведение не зависит
 * от версии и сборки библиотеки.
 */
std::vector<bool> selfClosingFlags(const std::string& xml) {
    std::vector<bool> flags;
    std::size_t at = 0;
    // Пропуск до конца конструкции; npos — текст закончился.
    auto skipPast = [&](std::string_view end) {
        const auto found = xml.find(end, at);
        at = found == std::string::npos ? std::string::npos : found + end.size();
    };
    while ((at = xml.find('<', at)) != std::string::npos) {
        const std::string_view rest(xml.data() + at, xml.size() - at);
        if (rest.starts_with("<!--")) {
            skipPast("-->");
        } else if (rest.starts_with("<![CDATA[")) {
            skipPast("]]>");
        } else if (rest.starts_with("<?")) {
            skipPast("?>");
        } else if (rest.starts_with("<!") || rest.starts_with("</")) {
            skipPast(">");
        } else {
            // Открывающий тег: ищем «>» вне кавычек значений атрибутов.
            char quote = 0;
            std::size_t end = at + 1;
            for (; end < xml.size(); ++end) {
                const char c = xml[end];
                if (quote != 0) {
                    quote = c == quote ? 0 : quote;
                } else if (c == '"' || c == '\'') {
                    quote = c;
                } else if (c == '>') {
                    break;
                }
            }
            if (end >= xml.size()) {
                break;
            }
            flags.push_back(xml[end - 1] == '/');
            at = end + 1;
        }
        if (at == std::string::npos) {
            break;
        }
    }
    return flags;
}

} // namespace

Result<void> loadDom(pugi::xml_document& document, const std::string& xml) {
    const pugi::xml_parse_result parsed = document.load_buffer(
        xml.data(), xml.size(), pugi::parse_default | pugi::parse_ws_pcdata_single | pugi::parse_comments, pugi::encoding_utf8);
    if (!parsed) {
        return makeError(ErrorCode::Format, "XML error: %1", {parsed.description()});
    }

    // Пустому элементу, записанному парой тегов, даём текст-метку (см. kEmptyText).
    struct EmptyPairs final : pugi::xml_tree_walker {
        std::vector<bool> selfClosing;
        std::size_t index = 0;
        bool for_each(pugi::xml_node& node) override {
            if (node.type() != pugi::node_element) {
                return true;
            }
            const bool pair = index < selfClosing.size() && !selfClosing[index];
            ++index;
            if (pair && !node.first_child()) {
                node.append_child(pugi::node_pcdata).set_value(std::string(kEmptyText).c_str());
            }
            return true;
        }
    } walker;
    walker.selfClosing = selfClosingFlags(xml);
    document.traverse(walker);
    return {};
}

std::string saveDom(const pugi::xml_document& document) {
    std::ostringstream out;
    out << kDeclaration;
    document.save(out, "", pugi::format_raw | pugi::format_no_declaration, pugi::encoding_utf8);
    // Метки пустого текста — прочь: остаётся пара тегов <Label></Label>.
    std::string text = out.str();
    std::string result;
    result.reserve(text.size());
    std::size_t from = 0;
    for (std::size_t at = text.find(kEmptyText); at != std::string::npos; at = text.find(kEmptyText, from)) {
        result.append(text, from, at - from);
        from = at + kEmptyText.size();
    }
    result.append(text, from, std::string::npos);
    return result;
}

std::string_view textOf(pugi::xml_node node) {
    const std::string_view text = node.text().get();
    return text == kEmptyText ? std::string_view() : text;
}

pugi::xml_node findById(pugi::xml_node root, const char* tag, const std::string& id) {
    for (pugi::xml_node node : root.children(tag)) {
        if (id == node.child("Id").text().get()) {
            return node;
        }
    }
    return {};
}

void setLabel(pugi::xml_node owner, const std::string& label) {
    pugi::xml_node presentation = owner.child("Presentation");
    if (!presentation) {
        presentation = owner.append_child("Presentation");
    }
    pugi::xml_node node = presentation.child("Label");
    if (!node) {
        node = presentation.prepend_child("Label");
    }
    node.text() = label.c_str();
}

pugi::xml_node appendText(pugi::xml_node parent, const char* name, std::string_view text) {
    pugi::xml_node node = parent.append_child(name);
    node.append_child(pugi::node_pcdata).set_value(std::string(text.empty() ? kEmptyText : text).c_str());
    return node;
}

void appendProperty(pugi::xml_node properties, const char* name, std::string_view value) {
    pugi::xml_node property = appendText(properties, "Property", value);
    property.prepend_attribute("name") = name;
}

void setProperty(pugi::xml_node properties, const char* name, std::string_view value) {
    pugi::xml_node property = properties.find_child_by_attribute("Property", "name", name);
    if (property) {
        property.text() = std::string(value).c_str();
    } else {
        appendProperty(properties, name, value);
    }
}

void appendSetValue(pugi::xml_node actions, const std::string& deviceId, std::string_view state,
                    std::string_view value) {
    pugi::xml_node action = actions.append_child("Action");
    appendText(action, "Target", "Device");
    pugi::xml_node operation = action.append_child("Operation");
    appendText(operation, "Name", "SetValue");
    appendText(operation, "Parameter", deviceId).append_attribute("name") = "DeviceId";
    appendText(operation, "Parameter", state).append_attribute("name") = "State";
    appendText(operation, "Parameter", value).append_attribute("name") = "Value";
}

} // namespace harmony::detail
