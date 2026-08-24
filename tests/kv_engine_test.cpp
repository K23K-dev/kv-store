#include "kv/kv_engine.hpp"

#include <gtest/gtest.h>

#include <string>

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