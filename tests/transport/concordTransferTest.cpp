#include "harmony/transport/concordTransfer.h"
#include "harmony/core/remoteCatalog.h"
#include "harmony/core/remoteProfile.h"
#include "harmony/core/formats/zipArchive.h"
#include "../dumpFiles.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <set>
#include "../../third_party/libconcord/read_flash_packet.h"
#include <remote.h>
#include <protocol_z.h>
#include <z_hid_packet.h>

using namespace harmony;
using namespace harmony::transport;

TEST(TransferTargets, ExtenderCanBeRestoredWithoutBecomingARemoteModel) {
    const auto target = transferTarget("c-re4a");
    ASSERT_TRUE(target);
    EXPECT_TRUE(target->readable);
    EXPECT_TRUE(target->writable);
    EXPECT_EQ(target->link, LinkKind::UsbHid);
    EXPECT_EQ(target->architecture, 10);
    EXPECT_EQ(target->containerSlots, 11);
    ASSERT_TRUE(transferTargetBySkin(24));
    EXPECT_EQ(transferTargetBySkin(24)->id, target->id);
    EXPECT_EQ(catalogModelBySkin(24), nullptr);
    EXPECT_EQ(catalogModel("c-re4a"), nullptr);
    EXPECT_FALSE(transferTargetBySkin(-1));
    EXPECT_FALSE(transferTargetBySkin(20)); // Другой экстендер пока не подтверждён данными.
    EXPECT_FALSE(transferTarget("896"));
    for (const auto& model : remoteCatalog()) {
        const auto remote = transferTarget(model.id);
        ASSERT_TRUE(remote) << model.id;
        EXPECT_EQ(remote->readable, model.readable);
        EXPECT_EQ(remote->writable, model.rawRestore);
        for (const int skin : model.skins) {
            ASSERT_TRUE(transferTargetBySkin(skin));
            EXPECT_EQ(transferTargetBySkin(skin)->id, model.id);
        }
    }
}

TEST(TransferTargets, ExtenderRejectsDamagedImagesAndWrongIdentityBeforeWriting) {
    const auto path = harmony::test::dumpFile("Json/C-RE4A", "c-re4a-20261124.EZHex");
    if (path.empty()) GTEST_SKIP() << "No local C-RE4A dump";
    const auto file = inspectTransferFile(harmony::test::readFile(path));
    ASSERT_TRUE(file) << file.error().message;
    const auto target = transferTarget("c-re4a");
    ASSERT_TRUE(target);
    TransferInfo info;
    info.writable = target->writable;
    info.protocol = file->protocol;
    info.remote.identity.architecture = target->architecture;
    info.remote.identity.skin = target->skins.front();
    info.containerSlots = target->containerSlots;
    ASSERT_TRUE(validateTransfer(*file, info));
    auto damaged = *file;
    damaged.payload[60] ^= 1;
    EXPECT_FALSE(validateTransfer(damaged, info));
    info.containerSlots = 23;
    EXPECT_FALSE(validateTransfer(*file, info));
    info.containerSlots = target->containerSlots;
    info.remote.identity.architecture = 14;
    EXPECT_FALSE(validateTransfer(*file, info));
    info.remote.identity.architecture = target->architecture;
    info.remote.identity.skin = 19;
    EXPECT_FALSE(validateTransfer(*file, info));
    info.remote.identity.skin = target->skins.front();
    ++info.protocol;
    EXPECT_FALSE(validateTransfer(*file, info));
}

namespace {
/// Exposes packet parsing without opening a USB device.
class ZHidParser final : public CRemoteZ_HID {
public:
    using CRemoteZ_HID::ParseParams;
    using CRemoteZ_Base::TParamList;
};
}

TEST(ZHidPackets, RejectsRegionDataInPlaceOfIdentity) {
    ZHidParser parser;
    std::uint8_t packet[60]{0x40, 0xf8, 0x2f, 0x01, 0x48};
    ZHidParser::TParamList params{};
    EXPECT_NE(parser.ParseParams(55, packet, params), 0);
    EXPECT_EQ(params.count, 0u);
    EXPECT_EQ(params.p[2], nullptr);
}

TEST(ZHidPackets, SkipsStandaloneAckAndPreviousCommandReply) {
    const std::uint8_t ack[]{3, TYPE_TCP_ACK, 0xf8, 0x2f};
    EXPECT_EQ(harmony_z_tcp_packet(ack, sizeof(ack), COMMAND_READ_REGION_DATA), 0);
    EXPECT_EQ(harmony_z_tcp_packet(ack, sizeof(ack), COMMAND_WRITE_UPDATE_DATA, true), 1);
    std::uint8_t reply[]{5, TYPE_TCP_ACK, 0xf8, 0x2f, TYPE_RESPONSE, COMMAND_READ_REGION};
    EXPECT_EQ(harmony_z_tcp_packet(reply, sizeof(reply), COMMAND_READ_REGION_DATA), 0);
    reply[5] = COMMAND_READ_REGION_DATA;
    EXPECT_EQ(harmony_z_tcp_packet(reply, sizeof(reply), COMMAND_READ_REGION_DATA), 1);
    EXPECT_EQ(harmony_z_tcp_packet(reply, 5, COMMAND_READ_REGION_DATA), -1);
    reply[5] = COMMAND_READ_REGION_DONE;
    EXPECT_EQ(harmony_z_tcp_packet(reply, sizeof(reply), COMMAND_READ_REGION_DATA), 1);
    reply[0] = 61;
    EXPECT_EQ(harmony_z_tcp_packet(reply, sizeof(reply), COMMAND_READ_REGION_DATA), -1);
}

TEST(ZHidPackets, ValidatesIdentityAndGuidLengths) {
    ZHidParser parser;
    std::uint8_t packet[60]{1, TYPE_RESPONSE, COMMAND_GET_SYSTEM_INFO, STATUS_OK};
    ZHidParser::TParamList params{};
    EXPECT_NE(parser.ParseParams(14, packet, params), 0);
    EXPECT_EQ(parser.ParseParams(15, packet, params), 0);
    EXPECT_EQ(params.count, 8u);
    EXPECT_EQ(params.p[7], packet + 17);
    packet[2] = COMMAND_GET_GUID;
    EXPECT_NE(parser.ParseParams(47, packet, params), 0);
    EXPECT_EQ(params.p[7], nullptr);
    EXPECT_EQ(parser.ParseParams(48, packet, params), 0);
    packet[3] = STATUS_BUSY;
    EXPECT_NE(parser.ParseParams(48, packet, params), 0);
}

TEST(ClassicHidPackets, ValidatesActualReportLengthAndChunkBoundary) {
    const std::uint8_t packet[]{0x63, 0x12, 0xab, 0xcd};
    EXPECT_EQ(harmony_flash_packet(packet, 4, 14, 0x12, 2), 2);
    EXPECT_EQ(harmony_flash_packet(packet, 3, 14, 0x12, 2), -1);
    EXPECT_EQ(harmony_flash_packet(packet, 4, 14, 0x12, 1), -1);
    EXPECT_EQ(harmony_flash_packet(packet, 4, 14, 0x01, 2), -1);
    EXPECT_EQ(harmony_flash_packet(packet, 1, 14, 0x12, 2), -1);
}

TEST(ClassicHidPackets, RejectsPrematureDoneAndUnknownLengthCodes) {
    const std::uint8_t done[]{0xf1, 0x50};
    EXPECT_EQ(harmony_flash_packet(done, 2, 14, 1, 0), 0);
    EXPECT_EQ(harmony_flash_packet(done, 2, 14, 1, 1), -1);
    std::uint8_t invalid[]{0x6b, 1, 0, 0};
    EXPECT_EQ(harmony_flash_packet(invalid, 4, 14, 1, 2), -1);
    invalid[0] = 0x20;
    EXPECT_EQ(harmony_flash_packet(invalid, 4, 14, 1, 2), -1);
}

TEST(ClassicHidPackets, UsesLegacyLengthMapOnlyForProtocolZero) {
    const std::uint8_t packet[]{0x68, 1, 1, 2, 3, 4, 5, 6, 7};
    EXPECT_EQ(harmony_flash_packet(packet, 9, 0, 1, 7), 7);
    EXPECT_EQ(harmony_flash_packet(packet, 9, 14, 1, 7), -1);
}

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

/// Минимальная рамка GSPM: One (arch 12, 22 слота, база 0x040000) или 600–700
/// (arch 14, 20 слотов, база 0x030000). Указатели секций нулевые: проверяется рамка.
formats::Bytes gspmImage(int architecture = 12) {
    const std::uint32_t base = architecture == 12 ? 0x040000u : 0x030000u;
    const std::size_t slots = architecture == 12 ? 22 : 20;
    formats::Bytes b(128, 0);
    std::copy_n("GSPM", 4, b.begin());
    b[9] = static_cast<std::uint8_t>(slots);
    std::copy_n("LWJL", 4, b.begin() + 0x0B + 4 * slots);
    const auto end = base + static_cast<unsigned>(b.size()) - 4;
    b[4] = static_cast<std::uint8_t>(end); b[5] = static_cast<std::uint8_t>(end >> 8); b[6] = static_cast<std::uint8_t>(end >> 16);
    std::uint16_t sum = 0x4321;
    for (std::size_t i = 0; i < b.size() - 6; i += 2) sum ^= static_cast<std::uint16_t>(b[i] | (b[i + 1] << 8));
    b[b.size() - 6] = static_cast<std::uint8_t>(sum);
    b[b.size() - 5] = static_cast<std::uint8_t>(sum >> 8);
    std::copy_n("PTYY", 4, b.end() - 4);
    return b;
}

/// Минимальный GSPM One, не аппаратный образец.
TransferFile oneFile() {
    TransferFile file;
    file.protocol = 12; file.skin = 54;
    file.payload = gspmImage();
    return file;
}

/// Пульт 600–700, как его видит connect(): arch 14, протокол 14.
TransferInfo info665() {
    TransferInfo info;
    info.modelId = "665";
    info.remote.model = "Harmony 665";
    info.remote.firmwareVersion = "2.2";
    info.remote.identity = {.architecture = 14, .skin = 99, .flash = "0x01:0x49", .board = "1.2.3", .softwareType = 0};
    info.protocol = 14;
    info.readable = true;
    return info;
}

/// EZHex, каким его возвращает ConcordTransfer::read() для 600–700.
formats::Bytes ezhex665(formats::Bytes image) {
    auto identity = info665().remote.identity;
    auto bytes = formats::writeEzHex({.header = {}, .payload = std::move(image), .identity = identity});
    EXPECT_TRUE(bytes);
    return bytes.value_or(formats::Bytes{});
}

formats::Bytes asBytes(const std::string& text) { return {text.begin(), text.end()}; }
}

TEST(RemoteCatalog, ModelsWithoutArtworkStillHaveTransferCapabilities) {
    for (const auto id : {"745", "659", "880", "harman-kardon-tc-30", "monster-avl-300", "800", "200", "350"}) {
        const auto* model = catalogModel(id);
        ASSERT_NE(model, nullptr) << id;
        EXPECT_TRUE(model->rawRestore) << id;
        EXPECT_EQ(remoteProfile(id), nullptr) << id;
        EXPECT_FALSE(model->configWriteTested) << id;
        EXPECT_FALSE(model->skins.empty()) << id;
    }
    std::set<std::string_view> ids;
    std::set<int> skins;
    for (const auto& model : remoteCatalog()) {
        EXPECT_TRUE(ids.insert(model.id).second) << model.id;
        for (const int skin : model.skins) EXPECT_TRUE(skins.insert(skin).second) << skin;
    }
    EXPECT_FALSE(catalogModel("1000")->readable);
    EXPECT_TRUE(catalogModel("1000")->rawRestore);
    EXPECT_FALSE(catalogModel("1100i")->rawRestore);
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

TEST(RemoteCaptureTransfer, CaptureOf665PassesContainerAndFrameChecks) {
    const auto capture = makeCapture(info665(), ezhex665(gspmImage(14)));
    EXPECT_EQ(capture.modelId, "665");
    EXPECT_EQ(capture.model, "Harmony 665");
    EXPECT_EQ(capture.identity.skin, 99);
    EXPECT_EQ(capture.protocol, 14);
    EXPECT_EQ(capture.firmware, "2.2");
    EXPECT_EQ(capture.container, formats::CaptureContainer::EzHex);
    EXPECT_EQ(capture.createdUtc.size(), 20u);
    const auto check = inspectCapture(capture);
    ASSERT_TRUE(check) << check.error().message;
    EXPECT_TRUE(check->checksum);
    EXPECT_TRUE(check->frame);
    EXPECT_EQ(check->file.payload, gspmImage(14));
}

TEST(RemoteCaptureTransfer, BrokenFrameOrForeignIdentityIsReported) {
    auto image = gspmImage(14);
    image[20] ^= 1; // XOR16 образа больше не сходится, а сумма EZHex пересчитана
    EXPECT_EQ(inspectCapture(makeCapture(info665(), ezhex665(image))).error().code, ErrorCode::Format);
    // Образ One (22 слота, база 0x040000) внутри снятия 665: семейство и база не те.
    EXPECT_FALSE(inspectCapture(makeCapture(info665(), ezhex665(gspmImage()))));
    auto other = info665();
    other.remote.identity.skin = 100; // контейнер для skin 99
    EXPECT_EQ(inspectCapture(makeCapture(other, ezhex665(gspmImage(14)))).error().code, ErrorCode::Format);
    other = info665();
    other.mh = true; // сказано MH, а внутри EZHex
    EXPECT_EQ(inspectCapture(makeCapture(other, ezhex665(gspmImage(14)))).error().code, ErrorCode::Format);
}

TEST(RemoteCaptureTransfer, MhCaptureHasNoFrameCheckYet) {
    TransferInfo info;
    info.modelId = "200"; info.mh = true; info.remote.identity.skin = 78;
    const auto check = inspectCapture(makeCapture(info, mhFile()));
    ASSERT_TRUE(check) << check.error().message;
    EXPECT_TRUE(check->file.mh);
    EXPECT_TRUE(check->checksum);
    EXPECT_FALSE(check->frame);
}

TEST(RemoteCaptureTransfer, WritingAcceptsCaptureJsonAndUnwrapsContainer) {
    const auto container = ezhex665(gspmImage(14));
    const auto json = formats::writeRemoteCapture(makeCapture(info665(), container));
    ASSERT_TRUE(json);
    const auto bytes = asBytes(*json);
    const auto file = inspectTransferFile(bytes);
    ASSERT_TRUE(file) << file.error().message;
    EXPECT_EQ(file->skin, 99);
    EXPECT_EQ(file->protocol, 14);
    const auto unwrapped = unwrapCapture(bytes);
    ASSERT_TRUE(unwrapped);
    EXPECT_EQ(*unwrapped, container);
    // Не JSON — как есть.
    EXPECT_EQ(unwrapCapture(container).value_or(formats::Bytes{}), container);
    // Испорченный JSON на пульт не уходит.
    auto damaged = bytes;
    damaged[damaged.size() / 2] ^= 1;
    EXPECT_FALSE(inspectTransferFile(damaged));
}

TEST(RemoteCaptureTransfer, OldHidDumpIsReadOnly) {
    formats::RemoteCapture legacy = makeCapture(info665(), gspmImage(14));
    legacy.container = formats::CaptureContainer::Raw;
    EXPECT_EQ(inspectCapture(legacy).error().code, ErrorCode::Unsupported);
    const std::string json = R"({"format":"harmony-hid-backup","version":1,"skin":99,"configSize":3,)"
        R"("configSha256":"039058c6f2c0cb492c533b0a4d14ef77cc0f78abccced5287d84a1a2011cfb81","configBase64":"AQID"})";
    EXPECT_EQ(unwrapCapture(asBytes(json)).error().code, ErrorCode::Unsupported);
    EXPECT_FALSE(inspectTransferFile(asBytes(json)));
}
