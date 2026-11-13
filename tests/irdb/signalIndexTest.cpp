/**
 * @file signalIndexTest.cpp
 * @brief Тесты поиска модели по кнопкам родного пульта: кадр сигнала, сравнение с допуском,
 * индекс в базе и ModelFinder.
 */

#include "harmony/core/irdb/signalIndex.h"

#include "harmony/core/irdb/irDatabase.h"
#include "harmony/core/irdb/modelFinder.h"

#include <filesystem>
#include <gtest/gtest.h>

using namespace harmony;
using irdb::Frame;
using irdb::SignalIndex;

namespace {

const std::filesystem::path kData = std::filesystem::path(HARMONY_TEST_DATA_DIR) / "irdb";

/// Сигнал с «дрожанием», как у живого пульта: каждая длительность сдвинута на ±@p jitter мкс.
ir::IrSignal jittered(ir::IrSignal signal, int jitter) {
    int sign = 1;
    for (int& duration : signal.durations) {
        duration += duration > 0 ? sign * jitter : -sign * jitter;
        sign = -sign;
    }
    return signal;
}

/// База из фикстуры (три устройства) во временной папке.
class ModelFinderTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = std::filesystem::temp_directory_path() /
               ("harmony-signals-" +
                std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" +
                ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::create_directories(dir_);
        auto stats =
            irdb::buildDatabase(kData / "device_features_raw.jsonl.gz",
                                kData / "AllProtocols.json.gz", dir_ / "test.irdb", "test");
        ASSERT_TRUE(stats) << stats.error().message;
        EXPECT_GT(stats->signalFrames, 100U);
        auto database = irdb::IrDatabase::open(dir_ / "test.irdb");
        ASSERT_TRUE(database) << database.error().message;
        database_ = std::move(*database);
    }
    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(dir_, ignored);
    }

    /// Сигнал команды @p index устройства, найденного по @p query.
    ir::IrSignal command(std::string_view query, std::size_t index) {
        const auto entry = database_.search(query, 1);
        EXPECT_EQ(entry.size(), 1U);
        auto device = database_.device(entry.front());
        EXPECT_TRUE(device);
        auto signal = database_.protocols().render(device->commands.at(index));
        EXPECT_TRUE(signal) << signal.error().message;
        return *signal;
    }

    std::filesystem::path dir_;
    irdb::IrDatabase database_;
};

} // namespace

TEST(SignalIndex, FirstFrameEndsAtLongGap) {
    const ir::IrSignal signal{
        38000, {-5000, 9000, -4500, 560, -560, 560, -1690, 560, -40000, 9000, -2250, 560}};
    EXPECT_EQ(SignalIndex::firstFrame(signal), (Frame{9000, 4500, 560, 560, 560, 1690, 560}));
}

TEST(SignalIndex, FirstFrameJoinsRepeatedSigns) {
    // Два импульса подряд — один; две паузы, вместе длиннее предела, — конец кадра.
    const ir::IrSignal signal{38000, {300, 200, -500, 400, -6000, -6000, 700}};
    EXPECT_EQ(SignalIndex::firstFrame(signal), (Frame{500, 500, 400}));
}

TEST(SignalIndex, SimilarAllowsQuarterOrTwoHundredMicroseconds) {
    const Frame a{9000, 4500, 560, 560, 560, 1690, 560, 560};
    EXPECT_TRUE(SignalIndex::similar(a, Frame{8800, 4400, 700, 420, 600, 1500, 560, 9999}));
    EXPECT_FALSE(SignalIndex::similar(a, Frame{9000, 4500, 560, 560, 560, 560, 560, 560}));
    EXPECT_FALSE(SignalIndex::similar(a, Frame{9000, 4500, 560}));
    EXPECT_FALSE(SignalIndex::similar(Frame{}, Frame{}));
}

TEST(SignalIndex, WrittenIndexFindsDevicesBySimilarFrame) {
    const ir::IrSignal nec{38000, {9000, -4500, 560, -560, 560, -1690, 560, -560, 560, -40000}};
    const ir::IrSignal other{38000, {2400, -600, 1200, -600, 600, -600, 1200, -600, 600, -25000}};
    irdb::SignalIndexBuilder builder;
    builder.add(0, nec);
    builder.add(0, nec); // та же команда устройства — один раз
    builder.add(1, other);
    builder.add(2, nec);
    builder.add(3, ir::IrSignal{38000, {500, -500, 500}}); // слишком короткий
    EXPECT_EQ(builder.frameCount(), 2U);

    // Номера при сборке → номера каталога: 0 → 7, 1 → 5, 2 → 6.
    auto index = SignalIndex::parse(builder.write({7, 5, 6, 4}));
    ASSERT_TRUE(index) << index.error().message;
    EXPECT_EQ(index->frameCount(), 2U);
    EXPECT_EQ(index->lookup(SignalIndex::firstFrame(jittered(nec, 80))),
              (std::vector<std::uint32_t>{6, 7}));
    EXPECT_EQ(index->lookup(SignalIndex::firstFrame(other)), (std::vector<std::uint32_t>{5}));
    EXPECT_TRUE(index->lookup(Frame{1, 2, 3}).empty());
}

TEST(SignalIndex, RejectsDamagedData) {
    irdb::SignalIndexBuilder builder;
    builder.add(0,
                ir::IrSignal{38000, {9000, -4500, 560, -560, 560, -1690, 560, -560, 560, -40000}});
    std::string data = builder.write();
    data.resize(data.size() - 2);
    EXPECT_FALSE(SignalIndex::parse(data));
}

TEST_F(ModelFinderTest, FinderRanksTheModelWhoseButtonsWerePressed) {
    auto index = database_.signalIndex();
    ASSERT_TRUE(index) << index.error().message;
    irdb::ModelFinder finder(database_, *index);

    // Три разные кнопки Samsung с «дрожанием» живого пульта.
    using Press = irdb::ModelFinder::Press;
    EXPECT_EQ(finder.add(jittered(command("samsung", 0), 60)), Press::Added);
    EXPECT_EQ(finder.add(jittered(command("samsung", 5), -50)), Press::Added);
    EXPECT_EQ(finder.add(jittered(command("samsung", 20), 40)), Press::Added);
    EXPECT_EQ(finder.add(command("samsung", 5)), Press::Repeated);
    EXPECT_EQ(finder.add(ir::IrSignal{38000, {500, -500, 500}}), Press::TooShort);
    EXPECT_EQ(finder.presses(), 3U);

    const auto results = finder.results({}, 10);
    ASSERT_FALSE(results.hits.empty());
    EXPECT_EQ(results.best, 3);
    EXPECT_EQ(results.bestCount, 1U);
    EXPECT_EQ(database_.entries()[results.hits.front().entry].model, "HLN501W");
    ASSERT_FALSE(results.makers.empty());
    EXPECT_EQ(results.makers.front().first, "Samsung");

    // Фильтр по производителю убирает Samsung из итогов.
    EXPECT_TRUE(finder.results("sony", 10).hits.empty());

    finder.reset();
    EXPECT_EQ(finder.presses(), 0U);
    EXPECT_TRUE(finder.results({}, 10).hits.empty());
}

TEST_F(ModelFinderTest, UnknownSignalFindsNothing) {
    auto index = database_.signalIndex();
    ASSERT_TRUE(index);
    irdb::ModelFinder finder(database_, *index);
    EXPECT_EQ(finder.add(ir::IrSignal{
                  38000, {1234, -987, 1234, -987, 333, -333, 333, -333, 4444, -50000}}),
              irdb::ModelFinder::Press::Added);
    const auto results = finder.results({}, 10);
    EXPECT_TRUE(results.hits.empty());
    EXPECT_EQ(results.best, 0);
}
