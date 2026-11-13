/**
 * @file configDocumentNotSent.cpp
 * @brief Пометка «не отправлять на пульт» и конфигурация для пульта без помеченного.
 *
 * Пометка — XML-комментарий model::kNotSentMarker первым в <Device> или
 * <Activity>: файл остаётся правильным для любого разборщика, а при записи
 * на пульт помеченное убирается целиком (см. ConfigDocument::toEzHex()).
 */

#include "configDocumentImpl.h"

#include <algorithm>
#include <cstring>

namespace harmony {

namespace {

/// Ставит или снимает пометку на элементе устройства или активности.
void mark(pugi::xml_node node, bool notSent) {
    pugi::xml_node marker;
    for (pugi::xml_node child : node.children()) {
        if (child.type() == pugi::node_comment && std::strcmp(child.value(), model::kNotSentMarker) == 0) {
            marker = child;
        }
    }
    if (notSent && !marker) {
        node.prepend_child(pugi::node_comment).set_value(model::kNotSentMarker);
    } else if (!notSent && marker) {
        node.remove_child(marker);
    }
}

} // namespace

Result<void> ConfigDocument::setDeviceNotSent(const std::string& deviceId, bool notSent) {
    if (impl_->model.findDevice(deviceId) == nullptr) {
        return makeError(ErrorCode::NotFound, "no device %1", {deviceId});
    }
    return detail::mutate(*impl_, [&](pugi::xml_node root) -> Result<void> {
        mark(detail::findById(root, "Device", deviceId), notSent);
        return {};
    });
}

Result<void> ConfigDocument::setActivityNotSent(const std::string& activityId, bool notSent) {
    const model::Activity* activity = impl_->model.findActivity(activityId);
    if (activity == nullptr) {
        return makeError(ErrorCode::NotFound, "no activity %1", {activityId});
    }
    if (activity->isPowerOff()) {
        return makeError(ErrorCode::Unsupported, "the remote needs the “turn everything off” activity");
    }
    return detail::mutate(*impl_, [&](pugi::xml_node root) -> Result<void> {
        mark(detail::findById(root, "Activity", activityId), notSent);
        return {};
    });
}

std::vector<std::string> ConfigDocument::activitiesNotSent() const {
    std::vector<std::string> result;
    for (const auto& activity : impl_->model.activities) {
        if (impl_->model.isActivityNotSent(activity)) {
            result.push_back(activity.id);
        }
    }
    return result;
}

bool ConfigDocument::hasNotSent() const {
    const model::RemoteConfig& config = impl_->model;
    return std::ranges::any_of(config.devices, &model::Device::notSent) ||
           std::ranges::any_of(config.activities, &model::Activity::notSent);
}

namespace detail {

Result<std::vector<std::uint8_t>> remoteEzHex(const ConfigDocument& document) {
    // Копия того, что сохранилось бы, — и из неё убирается помеченное.
    auto saved = document.toEzHex(false);
    if (!saved) {
        return std::unexpected(std::move(saved.error()));
    }
    auto copy = ConfigDocument::fromEzHex(*saved, "remote");
    if (!copy) {
        return std::unexpected(std::move(copy.error()));
    }
    for (const auto& id : document.activitiesNotSent()) {
        if (auto removed = copy->removeActivity(id); !removed) {
            return std::unexpected(std::move(removed.error()));
        }
    }
    for (const auto& device : document.model().devices) {
        if (device.notSent) {
            if (auto removed = copy->removeDevice(device.id); !removed) {
                return std::unexpected(std::move(removed.error()));
            }
        }
    }
    return copy->toEzHex(true);
}

} // namespace detail

} // namespace harmony
