/**
 * @file modelFinder.cpp
 * @brief Поиск модели по нескольким кнопкам родного пульта.
 */

#include "harmony/core/irdb/modelFinder.h"

#include "harmony/core/irdb/irDatabase.h"

#include <algorithm>
#include <map>

namespace harmony::irdb {

namespace {

/// Сравнение без учёта регистра (латиница) — для порядка производителей и моделей.
bool lessNoCase(const std::string& a, const std::string& b) {
    return std::ranges::lexicographical_compare(a, b, [](char x, char y) {
        auto low = [](char c) {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        };
        return low(x) < low(y);
    });
}

} // namespace

ModelFinder::ModelFinder(const IrDatabase& database, std::shared_ptr<const SignalIndex> index)
    : database_(database), index_(std::move(index)) {}

ModelFinder::Press ModelFinder::add(const ir::IrSignal& signal) {
    Frame frame = SignalIndex::firstFrame(signal);
    if (frame.size() < SignalIndex::kMinFrame) {
        return Press::TooShort;
    }
    if (std::ranges::any_of(pressed_,
                            [&frame](const Frame& p) { return SignalIndex::similar(p, frame); })) {
        return Press::Repeated;
    }
    if (index_) {
        for (const std::uint32_t entry : index_->lookup(frame)) {
            ++score_[entry];
        }
    }
    pressed_.push_back(std::move(frame));
    return Press::Added;
}

void ModelFinder::reset() {
    pressed_.clear();
    score_.clear();
}

ModelFinder::Results ModelFinder::results(std::string_view filter, std::size_t max) const {
    Results out;
    const auto& entries = database_.entries();
    std::vector<Hit> hits;
    hits.reserve(score_.size());
    for (const auto& [entry, matched] : score_) {
        if (entry < entries.size() && (filter.empty() || database_.matches(entry, filter))) {
            hits.push_back({entry, matched});
            out.best = std::max(out.best, matched);
        }
    }
    out.total = hits.size();

    // Производители лучших: чем больше у производителя таких моделей, тем вероятнее он.
    std::map<std::string, std::size_t> makers;
    for (const Hit& hit : hits) {
        if (hit.matched == out.best) {
            ++makers[entries[hit.entry].manufacturer];
            ++out.bestCount;
        }
    }
    auto popularity = [&makers](const std::string& maker) {
        const auto it = makers.find(maker);
        return it != makers.end() ? it->second : 0;
    };
    // Каталог уже по алфавиту, поэтому среди равных порядок номеров — алфавитный.
    std::ranges::sort(hits, [&](const Hit& a, const Hit& b) {
        if (a.matched != b.matched) {
            return a.matched > b.matched;
        }
        const std::string& makerA = entries[a.entry].manufacturer;
        const std::string& makerB = entries[b.entry].manufacturer;
        const std::size_t countA = popularity(makerA);
        const std::size_t countB = popularity(makerB);
        if (countA != countB) {
            return countA > countB;
        }
        return a.entry < b.entry;
    });
    if (hits.size() > max) {
        hits.resize(max);
    }
    out.hits = std::move(hits);

    out.makers.assign(makers.begin(), makers.end());
    std::ranges::stable_sort(out.makers, [](const auto& a, const auto& b) {
        if (a.second != b.second) {
            return a.second > b.second;
        }
        return lessNoCase(a.first, b.first);
    });
    return out;
}

} // namespace harmony::irdb
