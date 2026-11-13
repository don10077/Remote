/**
 * @file configDocumentProperties.cpp
 * @brief Свойства <Property> устройств и активностей: чтение и правка (см. propertyCatalog.h).
 *
 * Меняется только значение нужного свойства: остальные, в том числе неизвестные
 * программе, остаются в файле как были.
 */

#include "configDocumentImpl.h"
#include "harmony/core/propertyCatalog.h"

#include <charconv>

namespace harmony {

namespace {

/// Узел <Properties> свойства: у устройства, его команд (паузы) или активности.
pugi::xml_node propertiesNode(pugi::xml_node root, PropertyScope scope, const std::string& id, bool create) {
    pugi::xml_node owner = detail::findById(root, scope == PropertyScope::Activity ? "Activity" : "Device", id);
    if (!owner) {
        return {};
    }
    if (scope == PropertyScope::DeviceTiming) {
        pugi::xml_node commands = owner.child("Commands");
        if (!commands) {
            if (!create) return {};
            commands = owner.append_child("Commands");
        }
        pugi::xml_node properties = commands.child("Properties");
        if (!properties && create) {
            properties = commands.prepend_child("Properties");
        }
        return properties;
    }
    pugi::xml_node properties = owner.child("Properties");
    if (!properties && create) {
        // Как в файлах пульта: после <Model> у устройства, после <Type> у активности.
        pugi::xml_node after = owner.child(scope == PropertyScope::Activity ? "Type" : "Model");
        properties = after ? owner.insert_child_after("Properties", after) : owner.append_child("Properties");
    }
    return properties;
}

/// Значение подходит описанию свойства.
Result<void> checkValue(const PropertyInfo& info, std::string_view value) {
    if (info.readOnly) {
        return makeError(ErrorCode::Unsupported, "the property %1 is set by the remote itself",
                         {std::string(info.name)});
    }
    if (info.type == PropertyInfo::Type::Int) {
        int number = 0;
        const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), number);
        if (ec != std::errc{} || end != value.data() + value.size() || number < info.min || number > info.max) {
            return makeError(ErrorCode::Format, "the property %1 must be a number from %2 to %3",
                             {std::string(info.name), std::to_string(info.min), std::to_string(info.max)});
        }
    }
    return {};
}

} // namespace

std::vector<std::pair<std::string, std::string>> ConfigDocument::properties(PropertyScope scope,
                                                                             const std::string& id) const {
    std::vector<std::pair<std::string, std::string>> out;
    pugi::xml_document dom;
    if (!detail::loadDom(dom, impl_->xml)) {
        return out;
    }
    const pugi::xml_node node = propertiesNode(dom.child("Root"), scope, id, false);
    for (const pugi::xml_node property : node.children("Property")) {
        out.emplace_back(property.attribute("name").value(), std::string(detail::textOf(property)));
    }
    return out;
}

Result<void> ConfigDocument::setProperty(PropertyScope scope, const std::string& id, const std::string& name,
                                         const std::string& value) {
    if (name.empty()) {
        return makeError(ErrorCode::Format, "the property must have a name");
    }
    const PropertyInfo* info = findProperty(scope, name);
    if (info != nullptr && !value.empty()) {
        if (auto checked = checkValue(*info, value); !checked) {
            return checked;
        }
    }
    const std::string stored = formatPropertyValue(scope, name, value);
    return detail::mutate(*impl_, [&](pugi::xml_node root) -> Result<void> {
        if (!detail::findById(root, scope == PropertyScope::Activity ? "Activity" : "Device", id)) {
            return makeError(ErrorCode::NotFound, "no device or activity %1", {id});
        }
        pugi::xml_node list = propertiesNode(root, scope, id, !value.empty());
        pugi::xml_node property = list.find_child_by_attribute("Property", "name", name.c_str());
        if (value.empty()) {
            if (property) {
                list.remove_child(property);
            }
            return {};
        }
        if (property && detail::textOf(property) == stored) {
            return {}; // не меняется — файл байт в байт прежний
        }
        detail::setProperty(list, name.c_str(), stored);
        return {};
    });
}

} // namespace harmony
