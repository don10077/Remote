/**
 * @file harmony900Backend.cpp
 * @brief Вид ConfigBackend на документ, хранящий UserConfiguration.xml (Harmony 900 и хаб).
 */

#include "configDocumentImpl.h"

#include "harmony/core/configBackend.h"

namespace harmony {

namespace detail {

ConfigFormat sourceFormat(const model::RemoteConfig& model) {
    if (model.property(kSourceFormatProperty) == kBinaryHidSourceFormat) return ConfigFormat::BinaryHid;
    return model.property(kSourceFormatProperty) == kHubSourceFormat ? ConfigFormat::Hub : ConfigFormat::Harmony900;
}

} // namespace detail

namespace {

/**
 * @brief Вид на содержимое ConfigDocument::Impl.
 *
 * Принадлежит самому Impl и живёт ровно столько же. Формат не хранится отдельно,
 * а читается из XML (свойство SourceFormat), поэтому всегда совпадает с содержимым.
 */
class Harmony900Backend final : public ConfigBackend {
public:
    explicit Harmony900Backend(const ConfigDocument::Impl& impl) : impl_(impl) {}

    [[nodiscard]] ConfigFormat format() const override { return detail::sourceFormat(impl_.model); }
    [[nodiscard]] const model::RemoteConfig& model() const override { return impl_.model; }
    [[nodiscard]] const std::string& name() const override { return impl_.name; }
    [[nodiscard]] const std::filesystem::path& location() const override {
        return impl_.ezhexFile.empty() ? impl_.folder : impl_.ezhexFile;
    }
    [[nodiscard]] formats::RemoteIdentity identity() const override {
        return impl_.package ? impl_.package->identity : formats::RemoteIdentity{};
    }
    [[nodiscard]] BackendCapabilities capabilities() const override {
        BackendCapabilities caps;
        switch (format()) {
        case ConfigFormat::Harmony900:
            // XML правится целиком и сохраняется в исходный .EZHex/ZIP.
            caps.writeToRemote = true;
            caps.saveToSource = true;
            break;
        case ConfigFormat::Hub:
            // Хаб правится и сохраняется в папку или .EZHex, но на пульт не пишется.
            caps.saveToSource = true;
            break;
        case ConfigFormat::BinaryHid:
            // Исходный образ сохраняется рядом с проектом; правки в него не собираются.
            caps.importedApproximation = true;
            caps.powerStateKnown = false;
            break;
        }
        return caps;
    }

private:
    const ConfigDocument::Impl& impl_;
};

} // namespace

std::unique_ptr<const ConfigBackend> detail::makeBackend(const ConfigDocument::Impl& impl) {
    return std::make_unique<Harmony900Backend>(impl);
}

const ConfigBackend* ConfigDocument::backend() const {
    return impl_->backend.get();
}

} // namespace harmony
