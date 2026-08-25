#include "kv/kv_engine.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <latch>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

class ManualClock final : public kv::Clock {
   public:
    explicit ManualClock(kv::Timestamp initial) noexcept
        : now_ms_(initial.milliseconds_since_epoch) {}

    [[nodiscard]] kv::Timestamp now() const noexcept override {
        // Atomic loading keeps the fake clock thread-safe
        return kv::Timestamp{now_ms_.load(std::memory_order_relaxed)};
    }

    void set(kv::Timestamp value) noexcept {
        now_ms_.store(value.milliseconds_since_epoch, std::memory_order_relaxed);
    }

   private:
    std::atomic<std::int64_t> now_ms_;
};

// Keeping key construction here so worker and verification code don't accidentally generate keys
// differently
std::string artifact_key(std::size_t character_index, std::size_t artifact_index) {
    return "artifact_" + std::to_string(character_index) + "_" + std::to_string(artifact_index);
}

}  // namespace

TEST(KvEngineTest, StoresReadsAndOverwritesValues) {
    kv::KvEngine store;

    EXPECT_EQ(store.put("favorite_inazuma_character", "Ayaka"), kv::Status::kOk);

    const kv::GetResult first_result = store.get("favorite_inazuma_character");

    ASSERT_EQ(first_result.status, kv::Status::kOk);
    EXPECT_EQ(first_result.value, "Ayaka");

    EXPECT_EQ(store.put("favorite_inazuma_character", "Yoimiya"), kv::Status::kOk);

    const kv::GetResult second_result = store.get("favorite_inazuma_character");

    ASSERT_EQ(second_result.status, kv::Status::kOk);
    EXPECT_EQ(second_result.value, "Yoimiya");
}

TEST(KvEngineTest, ReportsMissingAndErasesValues) {
    kv::KvEngine store;

    EXPECT_EQ(store.erase("weekly_boss"), kv::Status::kNotFound);
    EXPECT_EQ(store.put("weekly_boss", "Azhdaha"), kv::Status::kOk);
    EXPECT_EQ(store.erase("weekly_boss"), kv::Status::kOk);
    EXPECT_EQ(store.erase("weekly_boss"), kv::Status::kNotFound);

    const kv::GetResult result = store.get("weekly_boss");

    EXPECT_EQ(result.status, kv::Status::kNotFound);
    EXPECT_TRUE(result.value.empty());
}

TEST(KvEngineTest, SupportsEmbeddedNullBytes) {
    kv::KvEngine store;

    const std::string key("Yoimiya\0Inazuma", 15);
    const std::string value{"Pyro\0Bow", 8};

    EXPECT_EQ(store.put(key, value), kv::Status::kOk);

    const kv::GetResult result = store.get(key);

    ASSERT_EQ(result.status, kv::Status::kOk);
    EXPECT_EQ(result.value, value);
}

TEST(KvEngineTest, EnforcesKeyLimits) {
    kv::KvEngine store;

    const std::string max_key(kv::KvEngine::kMaxKeyBytes, 'k');
    const std::string oversized_key(kv::KvEngine::kMaxKeyBytes + 1U, 'k');

    EXPECT_EQ(store.put("", "Paimon"), kv::Status::kInvalidKey);
    EXPECT_EQ(store.get("").status, kv::Status::kInvalidKey);
    EXPECT_EQ(store.erase(""), kv::Status::kInvalidKey);

    EXPECT_EQ(store.put(max_key, ""), kv::Status::kOk);
    EXPECT_EQ(store.get(max_key).status, kv::Status::kOk);

    EXPECT_EQ(store.put(oversized_key, "Primogem"), kv::Status::kKeyTooLarge);
    EXPECT_EQ(store.get(oversized_key).status, kv::Status::kKeyTooLarge);
    EXPECT_EQ(store.erase(oversized_key), kv::Status::kKeyTooLarge);
}

TEST(KvEngineTest, EnforcesValueLimitsBeforeMutation) {
    kv::KvEngine store;

    const std::string max_value(kv::KvEngine::kMaxValueBytes, 'a');
    const std::string oversized_value(kv::KvEngine::kMaxValueBytes + 1U, 'a');

    EXPECT_EQ(store.put("artifact_inventory", max_value), kv::Status::kOk);

    EXPECT_EQ(store.put("artifact_inventory", oversized_value), kv::Status::kValueTooLarge);

    const kv::GetResult result = store.get("artifact_inventory");

    ASSERT_EQ(result.status, kv::Status::kOk);
    EXPECT_EQ(result.value, max_value);
}

TEST(KvEngineTest, ReturnsValueCopies) {
    kv::KvEngine store;

    EXPECT_EQ(store.put("active_party_member", "Yoimiya"), kv::Status::kOk);

    const kv::GetResult original_result = store.get("active_party_member");

    ASSERT_EQ(original_result.status, kv::Status::kOk);

    EXPECT_EQ(store.put("active_party_member", "Furina"), kv::Status::kOk);

    EXPECT_EQ(original_result.value, "Yoimiya");
}

TEST(KvEngineTest, HandlesConcurrentReadsAndWrites) {
    kv::KvEngine store;

    const std::array<std::string, 8> characters{
        "Yoimiya", "Furina", "Nahida", "Zhongli", "Venti", "Raiden Shogun", "Navia", "Neuvillette",
    };

    constexpr std::size_t kArtifactsPerCharacter = 200;

    std::latch start_gate{static_cast<std::ptrdiff_t>(characters.size())};

    std::atomic<bool> operations_valid{true};

    std::vector<std::thread> workers;
    workers.reserve(characters.size());

    for (std::size_t character_index = 0; character_index < characters.size(); ++character_index) {
        workers.emplace_back([&, character_index] {
            start_gate.count_down();
            start_gate.wait();

            try {
                const std::string& character = characters[character_index];

                for (std::size_t artifact_index = 0; artifact_index < kArtifactsPerCharacter;
                     ++artifact_index) {
                    const std::string key = artifact_key(character_index, artifact_index);
                    const kv::Status artifact_status = store.put(key, character);

                    const kv::Status party_status = store.put("active_party_member", character);

                    if (artifact_status != kv::Status::kOk || party_status != kv::Status::kOk) {
                        operations_valid.store(false, std::memory_order_relaxed);
                    }
                    const kv::GetResult result = store.get("active_party_member");

                    const bool allowed_value = std::find(characters.begin(), characters.end(),
                                                         result.value) != characters.end();

                    if (result.status != kv::Status::kOk || !allowed_value) {
                        operations_valid.store(false, std::memory_order_relaxed);
                    }
                }
            } catch (...) {
                operations_valid.store(false, std::memory_order_relaxed);
            }
        });
    }

    // join waits for each worker to finish
    for (std::thread& worker : workers) {
        worker.join();
    }

    ASSERT_TRUE(operations_valid.load(std::memory_order_relaxed));

    for (std::size_t character_index = 0; character_index < characters.size(); ++character_index) {
        for (std::size_t artifact_index = 0; artifact_index < kArtifactsPerCharacter;
             ++artifact_index) {
            const std::string key = artifact_key(character_index, artifact_index);

            const kv::GetResult result = store.get(key);

            ASSERT_EQ(result.status, kv::Status::kOk) << key;
            EXPECT_EQ(result.value, characters[character_index]) << key;
        }
    }
}

TEST(KvEngineTest, AllowsOnlyOneConcurrentErase) {
    kv::KvEngine store;

    ASSERT_EQ(store.put("limited_banner", "Yoimiya"), kv::Status::kOk);

    constexpr std::size_t kEraserCount = 8;

    std::latch start_gate{static_cast<std::ptrdiff_t>(kEraserCount)};

    std::array<kv::Status, kEraserCount> results{};

    std::atomic<bool> worker_failed{false};
    std::vector<std::thread> workers;
    workers.reserve(kEraserCount);

    for (std::size_t worker_index = 0; worker_index < kEraserCount; ++worker_index) {
        workers.emplace_back([&, worker_index] {
            start_gate.count_down();
            start_gate.wait();

            try {
                results[worker_index] = store.erase("limited_banner");
            } catch (...) {
                worker_failed.store(true, std::memory_order_relaxed);
            }
        });
    }

    for (std::thread& worker : workers) {
        worker.join();
    }

    ASSERT_FALSE(worker_failed.load(std::memory_order_relaxed));

    std::size_t erased_count = 0;

    for (const kv::Status status : results) {
        if (status == kv::Status::kOk) {
            ++erased_count;
        } else {
            // Every thread that lost the race must report
            // that the key was already missing
            EXPECT_EQ(status, kv::Status::kNotFound);
        }
    }

    EXPECT_EQ(erased_count, 1U);

    const kv::GetResult result = store.get("limited_banner");

    EXPECT_EQ(result.status, kv::Status::kNotFound);
    EXPECT_TRUE(result.value.empty());
}

TEST(KvEngineTest, ExpiresValueAtDeadline) {
    const auto clock = std::make_shared<ManualClock>(kv::Timestamp{100});

    kv::KvEngine store{clock};

    EXPECT_EQ(store.put("story_quest", "Dreamlike Timelessness", 10ms), kv::Status::kOk);

    clock->set(kv::Timestamp{109});

    const kv::GetResult before_deadline = store.get("story_quest");

    ASSERT_EQ(before_deadline.status, kv::Status::kOk);

    EXPECT_EQ(before_deadline.value, "Dreamlike Timelessness");

    clock->set(kv::Timestamp{110});

    const kv::GetResult at_deadline = store.get("story_quest");

    EXPECT_EQ(at_deadline.status, kv::Status::kNotFound);

    EXPECT_TRUE(at_deadline.value.empty());
}

TEST(KvEngineTest, ClearsExpirationWhenOverwrittenWithoutTtl) {
    const auto clock = std::make_shared<ManualClock>(kv::Timestamp{100});

    kv::KvEngine store{clock};

    ASSERT_EQ(store.put("featured_character", "Yoimiya", 10ms), kv::Status::kOk);

    clock->set(kv::Timestamp{109});

    ASSERT_EQ(store.put("featured_character", "Furina"), kv::Status::kOk);

    clock->set(kv::Timestamp{1000});

    const kv::GetResult result = store.get("featured_character");

    ASSERT_EQ(result.status, kv::Status::kOk);
    EXPECT_EQ(result.value, "Furina");
}

TEST(KvEngineTest, ResetsExpirationWhenOverwritten) {
    const auto clock = std::make_shared<ManualClock>(kv::Timestamp{100});

    kv::KvEngine store{clock};

    ASSERT_EQ(store.put("featured_character", "Yoimiya", 10ms), kv::Status::kOk);

    clock->set(kv::Timestamp{100});

    ASSERT_EQ(store.put("featured_character", "Navia", 20ms), kv::Status::kOk);

    clock->set(kv::Timestamp{110});

    const kv::GetResult after_old_deadline = store.get("featured_character");

    ASSERT_EQ(after_old_deadline.status, kv::Status::kOk);

    EXPECT_EQ(after_old_deadline.value, "Navia");

    clock->set(kv::Timestamp{129});

    EXPECT_EQ(store.get("featured_character").status, kv::Status::kNotFound);
}

TEST(KvEngineTest, RejectsInvalidTtlWithoutMutation) {
    const auto clock = std::make_shared<ManualClock>(kv::Timestamp{100});

    kv::KvEngine store{clock};

    ASSERT_EQ(store.put("favorite_character", "Yoimiya"), kv::Status::kOk);

    EXPECT_EQ(store.put("favorite_character", "Furina", 0ms), kv::Status::kInvalidTtl);

    EXPECT_EQ(store.put("favorite_character", "Nahida", -1ms), kv::Status::kInvalidTtl);

    clock->set(kv::Timestamp(std::numeric_limits<std::int64_t>::max() - 1));

    EXPECT_EQ(store.put("favorite_character", "Zhongli", 2ms), kv::Status::kInvalidTtl);

    const kv::GetResult result = store.get("favorite_character");

    ASSERT_EQ(result.status, kv::Status::kOk);
    EXPECT_EQ(result.value, "Yoimiya");
}

TEST(KvEngineTest, TreatsExpiredEraseAsMissing) {
    const auto clock = std::make_shared<ManualClock>(kv::Timestamp{100});

    kv::KvEngine store{clock};

    ASSERT_EQ(store.put("limited_banner", "Yoimiya", 10ms), kv::Status::kOk);

    clock->set(kv::Timestamp{110});

    // Expired and missing have identical observable behavior
    EXPECT_EQ(store.erase("limited_banner"), kv::Status::kNotFound);

    EXPECT_EQ(store.erase("limited_banner"), kv::Status::kNotFound);

    EXPECT_EQ(store.get("limited_banner").status, kv::Status::kNotFound);
}