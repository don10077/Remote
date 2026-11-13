/**
 * @file configDocumentButtons.cpp
 * @brief Правка кнопок активности или устройства: аппаратные кнопки пульта и кнопки на экране.
 *
 * В XML активности (у устройства — так же):
 * @code
 * <Presentation><Label>…</Label>
 *   <ControlGroup name="Misc">          ← экранные кнопки
 *     <Button><Label>Субтитры</Label><Position>1</Position><ActionId>…</ActionId></Button>
 *   </ControlGroup>
 *   <ControlGroup name="HardButtons">   ← аппаратные кнопки
 *     <Button name="Menu"><Label></Label><ActionId>92555930_Menu_Hold</ActionId></Button>
 *   </ControlGroup>
 * </Presentation>
 * @endcode
 */

#include "configDocumentImpl.h"
#include "harmony/core/buttonIcons.h"

#include <algorithm>
#include <map>
#include <set>

namespace harmony {

namespace {

using detail::appendText;

constexpr const char* kSoftGroup = "Misc";
constexpr const char* kHardGroup = "HardButtons";

/// Элемент владельца кнопок (<Activity> или <Device>); ошибка, если его нет.
Result<pugi::xml_node> findOwner(pugi::xml_node root, const ButtonOwner& owner) {
    pugi::xml_node node = detail::findById(root, owner.isDevice() ? "Device" : "Activity", owner.id);
    if (!node) {
        return owner.isDevice() ? makeError(ErrorCode::NotFound, "no device %1", {owner.id})
                                : makeError(ErrorCode::NotFound, "no activity %1", {owner.id});
    }
    return node;
}

/// Группа кнопок; @p create — создать, если нет (экранные — перед аппаратными).
pugi::xml_node group(pugi::xml_node owner, const char* name, bool create) {
    pugi::xml_node presentation = owner.child("Presentation");
    if (!presentation) {
        if (!create) {
            return {};
        }
        // У активности Presentation идёт после Properties, у устройства — после Model.
        pugi::xml_node after = owner.child("Properties") ? owner.child("Properties") : owner.child("Model");
        presentation = after ? owner.insert_child_after("Presentation", after) : owner.append_child("Presentation");
    }
    if (pugi::xml_node found = presentation.find_child_by_attribute("ControlGroup", "name", name)) {
        return found;
    }
    if (!create) {
        return {};
    }
    pugi::xml_node hard = presentation.find_child_by_attribute("ControlGroup", "name", kHardGroup);
    pugi::xml_node created = std::string_view(name) == kSoftGroup && hard
                                 ? presentation.insert_child_before("ControlGroup", hard)
                                 : presentation.append_child("ControlGroup");
    created.append_attribute("name") = name;
    return created;
}

/// Убирает группу, если в ней не осталось кнопок.
void dropIfEmpty(pugi::xml_node controlGroup) {
    if (controlGroup && !controlGroup.child("Button")) {
        controlGroup.parent().remove_child(controlGroup);
    }
}

/// Аппаратная кнопка в группе по имени.
pugi::xml_node hardButton(pugi::xml_node controlGroup, const std::string& key) {
    return controlGroup ? controlGroup.find_child_by_attribute("Button", "name", key.c_str()) : pugi::xml_node();
}

/// Экранная кнопка по месту на экране.
pugi::xml_node softButton(pugi::xml_node controlGroup, int position) {
    for (pugi::xml_node button : controlGroup.children("Button")) {
        if (button.child("Position").text().as_int(-1) == position) {
            return button;
        }
    }
    return {};
}

/// Задаёт текст элемента, создавая его при необходимости (перед @p before).
void setText(pugi::xml_node parent, const char* name, const std::string& text, const char* before = nullptr) {
    pugi::xml_node node = parent.child(name);
    if (!node) {
        pugi::xml_node next = before != nullptr ? parent.child(before) : pugi::xml_node();
        node = next ? parent.insert_child_before(name, next) : parent.append_child(name);
    }
    node.text() = text.c_str();
}

/**
 * @brief Проверяет, что кнопка может слать команду, и возвращает имя действия.
 *
 * У активности — устройству, которое она включает; у устройства — только ему самому.
 */
Result<std::string> actionFor(const model::RemoteConfig& config, const ButtonOwner& owner,
                              const std::string& deviceId, const std::string& command) {
    const model::Device* device = config.findDevice(deviceId);
    if (owner.isDevice()) {
        if (config.findDevice(owner.id) == nullptr) {
            return makeError(ErrorCode::NotFound, "no device %1", {owner.id});
        }
        if (deviceId != owner.id) {
            return makeError(ErrorCode::Format, "device buttons send commands only to the device itself");
        }
    } else {
        const model::Activity* activity = config.findActivity(owner.id);
        if (activity == nullptr) {
            return makeError(ErrorCode::NotFound, "no activity %1", {owner.id});
        }
        if (device == nullptr) {
            return makeError(ErrorCode::NotFound, "no device %1", {deviceId});
        }
        if (std::ranges::find(activity->powerOn, deviceId) == activity->powerOn.end()) {
            return makeError(ErrorCode::Format, "“%1” is not turned on in this activity — the button will not work", {device->displayName()});
        }
    }
    if (device->findCommand(command) == nullptr) {
        return makeError(ErrorCode::NotFound, "“%1” has no command “%2”", {device->displayName(), command});
    }
    return deviceId + "_" + command + "_Hold";
}

} // namespace

namespace detail {

void appendHardButton(pugi::xml_node owner, const std::string& key, const std::string& deviceId,
                      const std::string& command) {
    pugi::xml_node button = group(owner, kHardGroup, true).append_child("Button");
    button.append_attribute("name") = key.c_str();
    appendText(button, "Label", "");
    appendText(button, "ActionId", deviceId + "_" + command + "_Hold");
}

void appendSoftButton(pugi::xml_node owner, int position, const std::string& label, const std::string& deviceId,
                      const std::string& command) {
    pugi::xml_node button = group(owner, kSoftGroup, true).append_child("Button");
    appendText(button, "Label", label);
    appendText(button, "Position", std::to_string(position));
    appendText(button, "ActionId", deviceId + "_" + command + "_Hold");
}

} // namespace detail

Result<void> ConfigDocument::setHardButton(const ButtonOwner& owner, const std::string& key,
                                           const std::string& deviceId, const std::string& command) {
    if (key.empty()) {
        return makeError(ErrorCode::Format, "no remote button specified");
    }
    std::string action;
    if (!command.empty()) {
        auto checked = actionFor(impl_->model, owner, deviceId, command);
        if (!checked) {
            return std::unexpected(std::move(checked.error()));
        }
        action = std::move(*checked);
    }
    return detail::mutate(*impl_, [&](pugi::xml_node root) -> Result<void> {
        auto node = findOwner(root, owner);
        if (!node) {
            return std::unexpected(std::move(node.error()));
        }
        if (action.empty()) {
            pugi::xml_node hard = group(*node, kHardGroup, false);
            if (pugi::xml_node button = hardButton(hard, key)) {
                hard.remove_child(button);
                dropIfEmpty(hard);
            }
            return {};
        }
        pugi::xml_node hard = group(*node, kHardGroup, true);
        pugi::xml_node button = hardButton(hard, key);
        if (!button) {
            button = hard.append_child("Button");
            button.append_attribute("name") = key.c_str();
            appendText(button, "Label", "");
        }
        setText(button, "ActionId", action);
        return {};
    });
}

Result<void> ConfigDocument::moveHardButton(const ButtonOwner& owner, const std::string& fromKey,
                                            const std::string& toKey, bool copy) {
    if (fromKey == toKey) {
        return {};
    }
    return detail::mutate(*impl_, [&](pugi::xml_node root) -> Result<void> {
        auto node = findOwner(root, owner);
        if (!node) {
            return std::unexpected(std::move(node.error()));
        }
        pugi::xml_node hard = group(*node, kHardGroup, false);
        pugi::xml_node from = hardButton(hard, fromKey);
        if (!from) {
            return makeError(ErrorCode::NotFound, "button %1 has no assignment", {fromKey});
        }
        const std::string action = from.child("ActionId").text().get();
        pugi::xml_node to = hardButton(hard, toKey);
        if (!to) {
            to = hard.append_child("Button");
            to.append_attribute("name") = toKey.c_str();
            appendText(to, "Label", "");
        } else if (!copy) {
            // Перенос на занятую кнопку — обмен, как у экранных: прежнее назначение не теряется.
            const std::string previous = to.child("ActionId").text().get();
            setText(to, "ActionId", action);
            setText(from, "ActionId", previous);
            return {};
        }
        setText(to, "ActionId", action);
        if (!copy) {
            hard.remove_child(from);
        }
        return {};
    });
}

Result<void> ConfigDocument::setSoftButton(const ButtonOwner& owner, int position,
                                           const std::string& label, const std::string& deviceId,
                                           const std::string& command) {
    if (position < 0) {
        return makeError(ErrorCode::Format, "invalid screen position");
    }
    auto action = actionFor(impl_->model, owner, deviceId, command);
    if (!action) {
        return std::unexpected(std::move(action.error()));
    }
    return detail::mutate(*impl_, [&](pugi::xml_node root) -> Result<void> {
        auto node = findOwner(root, owner);
        if (!node) {
            return std::unexpected(std::move(node.error()));
        }
        pugi::xml_node soft = group(*node, kSoftGroup, true);
        pugi::xml_node button = softButton(soft, position);
        if (!button) {
            button = soft.append_child("Button");
            appendText(button, "Label", "");
            appendText(button, "Position", std::to_string(position));
        }
        // Подпись вместо значка; без подписи — значок, а если его нет — имя команды.
        if (!label.empty()) {
            button.remove_child("Icon");
            button.child("Label").text() = label.c_str();
        } else if (!button.child("Icon") && detail::textOf(button.child("Label")).empty()) {
            button.child("Label").text() = command.c_str();
        }
        setText(button, "ActionId", *action);
        return {};
    });
}

Result<void> ConfigDocument::setSoftButtonIcon(const ButtonOwner& owner, int position, const std::string& icon) {
    return detail::mutate(*impl_, [&](pugi::xml_node root) -> Result<void> {
        auto node = findOwner(root, owner);
        if (!node) {
            return std::unexpected(std::move(node.error()));
        }
        pugi::xml_node soft = group(*node, kSoftGroup, false);
        pugi::xml_node button = soft ? softButton(soft, position) : pugi::xml_node();
        if (!button) {
            return makeError(ErrorCode::NotFound, "there is no button at this screen position");
        }
        // Незнакомый значок, который уже стоит в файле, не трогаем; новый — только из прошивки.
        if (!icon.empty() && !isKnownButtonIcon(icon) && detail::textOf(button.child("Icon")) != icon) {
            return makeError(ErrorCode::Format, "the remote has no icon “%1”: the button would be blank", {icon});
        }
        pugi::xml_node label = button.child("Label");
        if (!label) {
            label = button.prepend_child("Label");
        }
        if (icon.empty()) {
            button.remove_child("Icon");
            if (detail::textOf(label).empty()) {
                // Без значка и подписи кнопка пустая: подписываем командой.
                const std::string action(detail::textOf(button.child("ActionId")));
                const auto first = action.find('_');
                const auto last = action.rfind('_');
                const std::string text = first != std::string::npos && last > first
                                             ? action.substr(first + 1, last - first - 1)
                                             : action;
                label.text() = text.c_str();
            }
            return {};
        }
        // Как в файлах пульта: <Label></Label><Icon>…</Icon><Position>…
        label.text() = std::string(detail::kEmptyText).c_str();
        pugi::xml_node iconNode = button.child("Icon");
        if (!iconNode) {
            iconNode = button.insert_child_after("Icon", label);
        }
        iconNode.text() = icon.c_str();
        return {};
    });
}

Result<void> ConfigDocument::removeSoftButton(const ButtonOwner& owner, int position) {
    return detail::mutate(*impl_, [&](pugi::xml_node root) -> Result<void> {
        auto node = findOwner(root, owner);
        if (!node) {
            return std::unexpected(std::move(node.error()));
        }
        pugi::xml_node soft = group(*node, kSoftGroup, false);
        pugi::xml_node button = soft ? softButton(soft, position) : pugi::xml_node();
        if (!button) {
            return makeError(ErrorCode::NotFound, "there is no button at this screen position");
        }
        soft.remove_child(button);
        dropIfEmpty(soft);
        return {};
    });
}

Result<void> ConfigDocument::moveSoftButton(const ButtonOwner& owner, int from, int to) {
    if (to < 0) {
        return makeError(ErrorCode::Format, "invalid screen position");
    }
    if (from == to) {
        return {};
    }
    return detail::mutate(*impl_, [&](pugi::xml_node root) -> Result<void> {
        auto node = findOwner(root, owner);
        if (!node) {
            return std::unexpected(std::move(node.error()));
        }
        pugi::xml_node soft = group(*node, kSoftGroup, false);
        pugi::xml_node moving = soft ? softButton(soft, from) : pugi::xml_node();
        if (!moving) {
            return makeError(ErrorCode::NotFound, "there is no button at this screen position");
        }
        if (pugi::xml_node other = softButton(soft, to)) {
            other.child("Position").text() = from; // место занято — меняемся
        }
        moving.child("Position").text() = to;
        return {};
    });
}

Result<ButtonCopyReport> ConfigDocument::copyButtons(const ButtonCopy& copy) {
    const model::RemoteConfig& config = impl_->model;
    const model::Activity* source = config.findActivity(copy.fromActivity);
    if (source == nullptr) {
        return makeError(ErrorCode::NotFound, "no activity %1", {copy.fromActivity});
    }
    // Куда — только обычные активности (кроме самой исходной); у каждой — что она включает.
    std::map<std::string, std::vector<std::string>> poweredOn;
    for (const auto& id : copy.toActivities) {
        const model::Activity* target = config.findActivity(id);
        if (target == nullptr) {
            return makeError(ErrorCode::NotFound, "no activity %1", {id});
        }
        if (target->isPowerOff()) {
            return makeError(ErrorCode::Unsupported, "the “turn everything off” activity has no buttons");
        }
        if (id != copy.fromActivity) {
            poweredOn[id] = target->powerOn;
        }
    }
    auto wanted = [](const auto& list, const auto& value) {
        return list.empty() || std::ranges::find(list, value) != list.end();
    };
    // Устройство, которому шлёт кнопка: «<устройство>_<команда>_<модификатор>».
    auto deviceOf = [](pugi::xml_node button) {
        const std::string action(detail::textOf(button.child("ActionId")));
        return action.substr(0, action.find('_'));
    };

    ButtonCopyReport report;
    auto changed = detail::mutate(*impl_, [&](pugi::xml_node root) -> Result<void> {
        const pugi::xml_node from = detail::findById(root, "Activity", copy.fromActivity);
        const pugi::xml_node fromHard = group(from, kHardGroup, false);
        const pugi::xml_node fromSoft = group(from, kSoftGroup, false);
        for (const auto& [id, devices] : poweredOn) {
            pugi::xml_node to = detail::findById(root, "Activity", id);
            auto works = [&](pugi::xml_node button) {
                return std::ranges::find(devices, deviceOf(button)) != devices.end();
            };
            bool touched = false;
            if (copy.hard && fromHard) {
                for (const pugi::xml_node button : fromHard.children("Button")) {
                    const std::string key = button.attribute("name").value();
                    if (!wanted(copy.hardKeys, key)) {
                        continue;
                    }
                    if (!works(button)) {
                        ++report.skipped;
                        continue;
                    }
                    pugi::xml_node hard = group(to, kHardGroup, true);
                    pugi::xml_node existing = hardButton(hard, key);
                    if (existing && !copy.replace) {
                        ++report.kept;
                        continue;
                    }
                    if (existing) {
                        hard.insert_copy_before(button, existing);
                        hard.remove_child(existing);
                    } else {
                        hard.append_copy(button);
                    }
                    ++report.hard;
                    touched = true;
                }
            }
            if (copy.soft && fromSoft) {
                for (const pugi::xml_node button : fromSoft.children("Button")) {
                    const int position = button.child("Position").text().as_int(-1);
                    if (!wanted(copy.softPositions, position)) {
                        continue;
                    }
                    if (!works(button)) {
                        ++report.skipped;
                        continue;
                    }
                    pugi::xml_node soft = group(to, kSoftGroup, true);
                    pugi::xml_node existing = softButton(soft, position);
                    int place = position;
                    if (existing && !copy.replace) {
                        // Место занято — первое свободное.
                        place = 0;
                        while (softButton(soft, place)) {
                            ++place;
                        }
                        existing = {};
                    }
                    pugi::xml_node placed =
                        existing ? soft.insert_copy_before(button, existing) : soft.append_copy(button);
                    if (existing) {
                        soft.remove_child(existing);
                    }
                    placed.child("Position").text() = place;
                    ++report.soft;
                    touched = true;
                }
            }
            if (touched) {
                ++report.activities;
            }
        }
        return {};
    });
    if (!changed) {
        return std::unexpected(std::move(changed.error()));
    }
    return report;
}

int ConfigDocument::freeSoftPosition(const ButtonOwner& owner) const {
    const std::vector<model::ControlGroup>* groups = nullptr;
    if (owner.isDevice()) {
        if (const model::Device* device = impl_->model.findDevice(owner.id)) {
            groups = &device->controlGroups;
        }
    } else if (const model::Activity* activity = impl_->model.findActivity(owner.id)) {
        groups = &activity->controlGroups;
    }
    if (groups == nullptr) {
        return -1;
    }
    std::set<int> taken;
    for (const auto& controlGroup : *groups) {
        if (controlGroup.name == kSoftGroup) {
            for (const auto& button : controlGroup.buttons) {
                taken.insert(button.position);
            }
        }
    }
    int position = 0;
    while (taken.contains(position)) {
        ++position;
    }
    return position;
}

} // namespace harmony
