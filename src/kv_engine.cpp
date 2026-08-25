#include "kv/kv_engine.hpp"

#include <functional>
#include <mutex>
#include <shared_mutex>
#include <utility>

namespace kv {

Status KvEngine::put(std::string key, std::string value) {
    const Status key_status = validate_key(key);

    if (key_status != Status::kOk) {
        return key_status;
    }

    if (value.size() > kMaxValueBytes) {
        return Status::kValueTooLarge;
    }

    Shard& shard = shards_[shard_index(key)];

    // Unique lock gives this writer exclusive access
    std::unique_lock lock{shard.mutex};

    shard.entries.insert_or_assign(std::move(key), std::move(value));

    // The lock is automatically released when the function ends
    return Status::kOk;
}

GetResult KvEngine::get(std::string_view key) const {
    const Status key_status = validate_key(key);

    if (key_status != Status::kOk) {
        return GetResult{key_status, {}};
    }

    const std::string owned_key{key};

    const Shard& shard = shards_[shard_index(key)];

    std::shared_lock lock{shard.mutex};

    const auto entry = shard.entries.find(owned_key);

    if (entry == shard.entries.end()) {
        return GetResult{Status::kNotFound, {}};
    }

    return GetResult{Status::kOk, entry->second};
}

Status KvEngine::erase(std::string_view key) {
    const Status key_status = validate_key(key);

    if (key_status != Status::kOk) {
        return key_status;
    }

    const std::string owned_key{key};
    Shard& shard = shards_[shard_index(key)];

    // Erasing changes the map so it requires exclusive access
    std::unique_lock lock{shard.mutex};

    if (shard.entries.erase(owned_key) == 0U) {
        return Status::kNotFound;
    }

    return Status::kOk;
}

Status KvEngine::validate_key(std::string_view key) noexcept {
    if (key.empty()) {
        return Status::kInvalidKey;
    }

    if (key.size() > kMaxKeyBytes) {
        return Status::kKeyTooLarge;
    }

    return Status::kOk;
}

std::size_t KvEngine::shard_index(std::string_view key) noexcept {
    // Modulo hashing maps the number into the range 0 to 63
    return std::hash<std::string_view>{}(key) % kShardCount;
}

}  // namespace kv