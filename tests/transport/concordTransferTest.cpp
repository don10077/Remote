#include "harmony/transport/concordTransfer.h"
#include "harmony/core/remoteCatalog.h"
#include "harmony/core/formats/zipArchive.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <set>

using namespace harmony;
using namespace harmony::transport;
namespace {
/// Самостоятельный контейнер MH без настоящих пользовательских данных.
formats::Bytes mhFile(std::string_view target = "/cfg/usercfg", int skin = 78, bool badChecksum = false) {
    formats::Bytes payload{1, 2, 3, 4, 0, 0, 'P', 'T', 'Y', 'Y'};
    const auto checksum = 0x4321 ^ 0x0201 ^ 0x0403;
    std::string xml = "<DATA><INTENDED><SKIN>" + std::to_string(skin) + "</SKIN></INTENDED><FILES>"
        "<FILE NAME=\"Result.EzHex\" PATH=\"" + std::string(target) + "\" SIZE=\"10\" OPERATIONTYPE=\"userconfiguration\">"
        "<CHECKSUM TYPE=\"XOR\" OFFSET=\"0\" LENGTH=\"4\" SEED=\"0x4321\" EXPECTEDVALUE=\"" +
        std::to_string(badChecksum ? checksum + 1 : checksum) + "\"/></FILE></FILES></DATA>";
    formats::ZipArchive archive;
    const formats::Bytes descriptor(xml.begin(), xml.end());
    archive.entries.push_back(formats::makeEntry("Description.xml", 0100644, descriptor, 0));
    archive.entries.push_back(formats::makeEntry("Result.EzHex", 0100644, payload, 0));
    return formats::writeZip(archive);
}

/// Минимальный GSPM с известными указателем, LWJL и XOR, не аппаратный образец.
TransferFile oneFile() {
    TransferFile file;
    file.protocol = 12; file.skin = 54;
    file.payload.resize(128, 0);
    auto& b = file.payload;
    std::copy_n("GSPM", 4, b.begin());
    std::copy_n("LWJL", 4, b.begin() + 0x63);
    const auto end = 0x040000u + static_cast<unsigned>(b.size()) - 4;
    b[4] = static_cast<std::uint8_t>(end); b[5] = static_cast<std::uint8_t>(end >> 8); b[6] = static_cast<std::uint8_t>(end >> 16);
    std::uint16_t sum = 0x4321;
    for (std::size_t i = 0; i < b.size() - 6; i += 2) sum ^= static_cast<std::uint16_t>(b[i] | (b[i + 1] << 8));
    b[b.size() - 6] = static_cast<std::uint8_t>(sum);
    b[b.size() - 5] = static_cast<std::uint8_t>(sum >> 8);
    std::copy_n("PTYY", 4, b.end() - 4);
    return file;
}
}

TEST(RemoteCatalog, ModelsWithoutArtworkStillHaveTransferCapabilities) {
    for (const auto id : {"745", "659", "880", "harman-kardon-tc-30", "monster-avl-300", "800", "200", "350"}) {
        const auto* model = catalogModel(id);
        ASSERT_NE(model, nullptr) << id;
        EXPECT_TRUE(model->writable) << id;
        EXPECT_TRUE(model->visualProfile.empty()) << id;
        EXPECT_FALSE(model->writeTested) << id;
        EXPECT_FALSE(model->skins.empty()) << id;
    }
    std::set<std::string_view> ids;
    std::set<int> skins;
    for (const auto& model : remoteCatalog()) {
        EXPECT_TRUE(ids.insert(model.id).second) << model.id;
        for (const int skin : model.skins) EXPECT_TRUE(skins.insert(skin).second) << skin;
    }
    EXPECT_FALSE(catalogModel("1000")->readable);
    EXPECT_TRUE(catalogModel("1000")->writable);
    EXPECT_FALSE(catalogModel("1100i")->writable);
}

TEST(ConcordTransfer, ReadsMhContainerWithoutRequiringEditorSupport) {
    const auto file = inspectTransferFile(mhFile());
    ASSERT_TRUE(file) << file.error().message;
    EXPECT_TRUE(file->mh);
    EXPECT_EQ(file->skin, 78);
    EXPECT_EQ(file->payload.size(), 10u);
    TransferInfo info;
    info.mh = true; info.writable = true; info.remote.identity.skin = 78;
    EXPECT_TRUE(validateTransfer(*file, info));
    info.remote.identity.skin = 80;
    EXPECT_FALSE(validateTransfer(*file, info));
}

TEST(ConcordTransfer, RejectsMhFirmwarePathsAndBadChecksums) {
    EXPECT_FALSE(inspectTransferFile(mhFile("/sys/firmware")));
    EXPECT_FALSE(inspectTransferFile(mhFile("/cfg/usercfg", 78, true)));
    auto broken = mhFile(); broken.resize(broken.size() / 2);
    EXPECT_FALSE(inspectTransferFile(broken));
}

TEST(ConcordTransfer, DoesNotDefaultMissingIdentityTo900) {
    formats::EzHex container;
    container.payload = {1, 2, 3};
    container.header = "<?xml version=\"1.0\"?><INFORMATION><BINARYDATASIZE>3</BINARYDATASIZE><CHECKSUM>105</CHECKSUM></INFORMATION>\r\n";
    auto bytes = formats::writeEzHex(container);
    ASSERT_TRUE(bytes);
    EXPECT_FALSE(inspectTransferFile(*bytes));
    container.header.clear();
    bytes = formats::writeEzHex(container);
    ASSERT_TRUE(bytes);
    EXPECT_TRUE(inspectTransferFile(*bytes));
}

TEST(ConcordTransfer, OneRequiresItsOwnAddressFooterAndInternalChecksum) {
    const auto file = oneFile();
    TransferInfo info;
    info.writable = true; info.protocol = 12; info.capacity = 0x390000;
    info.remote.identity.architecture = 12; info.remote.identity.skin = 54;
    ASSERT_TRUE(validateTransfer(file, info));
    for (const std::size_t offset : {std::size_t{6}, std::size_t{20}, std::size_t{127}}) {
        auto broken = file; broken.payload[offset] ^= 1;
        EXPECT_FALSE(validateTransfer(broken, info)) << offset;
    }
    auto oversized = file; oversized.payload.resize(0x390000);
    EXPECT_FALSE(validateTransfer(oversized, info));
    info.protocol = 14;
    EXPECT_FALSE(validateTransfer(file, info));
}

TEST(ConcordTransfer, UnknownModelsAndConcurrentSessionsNeverOpenHardware) {
    ConcordTransfer first;
    EXPECT_FALSE(first.connect("unknown-model"));
    ConcordTransfer second;
    EXPECT_FALSE(second.connect("900"));
    EXPECT_FALSE(first.read());
    EXPECT_FALSE(first.write({}, {}));
}
