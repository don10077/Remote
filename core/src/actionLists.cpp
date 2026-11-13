/**
 * @file actionLists.cpp
 * @brief Синхронизация ActionLists.xml с кнопками конфигурации.
 */

#include "actionLists.h"

#include "harmony/core/macro.h"
#include "xmlDom.h"

#include <optional>
#include <set>
#include <vector>

namespace harmony::detail {

namespace {

/// Действия всех кнопок в порядке появления в файле: сначала устройства, затем активности.
std::vector<std::string> usedActions(const model::RemoteConfig& config) {
    std::vector<std::string> ordered;
    std::set<std::string> seen;
    auto collect = [&](const std::vector<model::ControlGroup>& groups) {
        for (const auto& group : groups) {
            for (const auto& button : group.buttons) {
                if (!button.actionId.empty() && seen.insert(button.actionId).second) {
                    ordered.push_back(button.actionId);
                }
            }
        }
    };
    for (const auto& device : config.devices) {
        collect(device.controlGroups);
    }
    for (const auto& activity : config.activities) {
        collect(activity.controlGroups);
    }
    return ordered;
}

/// Части имени действия «<устройство>_<команда>_<модификатор>»; в команде бывают «_».
struct ActionName {
    std::string device;
    std::string command;
    std::string modifier;
};

std::optional<ActionName> splitAction(const std::string& name) {
    const auto first = name.find('_');
    const auto last = name.rfind('_');
    if (first == std::string::npos || last <= first + 1 || last + 1 >= name.size()) {
        return std::nullopt;
    }
    ActionName parts{name.substr(0, first), name.substr(first + 1, last - first - 1),
                     name.substr(last + 1)};
    if (parts.modifier != "Hold" && parts.modifier != "Press") {
        return std::nullopt;
    }
    return parts;
}

/// Добавляет <ActionList> перед узлом @p before (или в конец корня, если его нет).
void insertActionList(pugi::xml_node root, pugi::xml_node before, const std::string& name,
                      const ActionName& parts) {
    pugi::xml_node list = before ? root.insert_child_before("ActionList", before)
                                 : root.append_child("ActionList");
    list.append_attribute("name") = name.c_str();
    pugi::xml_node action = list.append_child("Action");
    appendText(action, "Target", "Device");
    pugi::xml_node operation = action.append_child("Operation");
    appendText(operation, "Name", "SendCommand");
    appendText(operation, "Parameter", parts.device).append_attribute("name") = "DeviceId";
    appendText(operation, "Parameter", parts.command).append_attribute("name") = "Command";
    appendText(operation, "Parameter", parts.modifier).append_attribute("name") = "Modifier";
}

/// Записывает хеш протоколов в свойство и в <Protocols><Hash>, если он отличается.
void setHash(pugi::xml_node root, const std::string& hash) {
    pugi::xml_node properties = root.child("Properties");
    if (!properties) {
        properties = root.append_child("Properties");
    }
    pugi::xml_node property = properties.find_child_by_attribute("Property", "name", "ProtocolCacheHash");
    if (!property) {
        property = properties.prepend_child("Property");
        property.append_attribute("name") = "ProtocolCacheHash";
    }
    if (hash != property.text().get()) {
        property.text() = hash.c_str();
    }
    pugi::xml_node protocols = root.child("Protocols");
    if (!protocols) {
        protocols = root.append_child("Protocols");
    }
    pugi::xml_node node = protocols.child("Hash");
    if (!node) {
        node = protocols.append_child("Hash");
    }
    if (hash != node.text().get()) {
        node.text() = hash.c_str();
    }
}

} // namespace

Result<std::string> syncActionLists(const std::string& existing, const model::RemoteConfig& config,
                                    const std::string& protocolHash, const std::string& lastUpdated) {
    pugi::xml_document document;
    pugi::xml_node root;
    if (existing.empty()) {
        root = document.append_child("Root");
        pugi::xml_node properties = root.append_child("Properties");
        appendProperty(properties, "ProtocolCacheHash", protocolHash);
        appendProperty(properties, "LastUpdated", lastUpdated);
        appendText(root.append_child("Protocols"), "Hash", protocolHash);
    } else {
        if (auto loaded = loadDom(document, existing); !loaded) {
            return wrapError(ErrorCode::Format, loaded.error(), "%1", {std::string(kActionListsFileName)});
        }
        root = document.child("Root");
        if (!root) {
            return makeError(ErrorCode::Format, "%1: no root element <Root>", {std::string(kActionListsFileName)});
        }
    }

    const std::vector<std::string> used = usedActions(config);
    const std::set<std::string> usedSet(used.begin(), used.end());

    // Убираем действия, на которые больше не ссылается ни одна кнопка.
    // Записи с именами другого вида не трогаем: их назначение нам неизвестно.
    std::set<std::string> present;
    for (pugi::xml_node list = root.child("ActionList"); list;) {
        pugi::xml_node next = list.next_sibling("ActionList");
        const std::string name = list.attribute("name").value();
        // Свои макросы («<id>_macro_<N>») без кнопок тоже убираются.
        if (!usedSet.contains(name) && (splitAction(name) || isMacroAction(name))) {
            root.remove_child(list);
        } else {
            present.insert(name);
        }
        list = next;
    }

    // Недостающие — после последнего действия (перед свойствами файла).
    pugi::xml_node before = root.child("Properties");
    for (const std::string& name : used) {
        if (present.contains(name)) {
            continue;
        }
        if (const auto parts = splitAction(name)) {
            insertActionList(root, before, name, *parts);
        }
    }

    if (!protocolHash.empty()) {
        setHash(root, protocolHash);
    }
    return saveDom(document);
}

} // namespace harmony::detail
