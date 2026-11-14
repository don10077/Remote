/**
 * @file harmony900Backend.cpp
 * @brief Вид ConfigBackend на документ формата Harmony 900.
 */

#include "configDocumentImpl.h"

#include "harmony/core/configBackend.h"

namespace harmony {

namespace {

/// Вид на содержимое ConfigDocument::Impl; живёт не дольше документа.
class Harmony900Backend final : public ConfigBackend {
public:
    explicit Harmony900Backend(const ConfigDocument::Impl& impl) : impl_(impl) {}

    [[nodiscard]] ConfigFormat format() const override { return impl_.format; }
    [[nodiscard]] const model::RemoteConfig& model() const override { return impl_.model; }
    [[nodiscard]] const std::string& name() const override { return impl_.name; }
    [[nodiscard]] const std::filesystem::path& location() const override {
        return impl_.ezhexFile.empty() ? impl_.folder : impl_.ezhexFile;
    }
    [[nodiscard]] formats::RemoteIdentity identity() const override {
        return impl_.package ? impl_.package->identity : formats::RemoteIdentity{};
    }
    [[nodiscard]] bool canWriteToRemote() const override { return impl_.format == ConfigFormat::Harmony900; }

private:
    const ConfigDocument::Impl& impl_;
};

} // namespace

std::unique_ptr<const ConfigBackend> ConfigDocument::backend() const {
    return std::make_unique<Harmony900Backend>(*impl_);
}

} // namespace harmony
