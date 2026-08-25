#include "kv/kv_engine.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <utility>

namespace kv {

namespace {
class SystemClock final : public Clock {
   public:
    [[nodiscard]] Timestamp now() const noexcept override {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch());

        return Timestamp{static_cast<std::int64_t>(elapsed.count())};
    }
};

}  // namespace

KvEngine::KvEngine() : KvEngine(std::make_shared<SystemClock>()) {}

KvEngine::KvEngine(std::shared_ptr<const Clock> clock) : clock_(std::move(clock)) {
    // null clock makes every TTL operation unsafe
    if (!clock_) {
        throw std::invalid_argument{"clock must not be null"};
    }
}

Status KvEngine::put(std::string key, std::string value,
                     std::optional<std::chrono::milliseconds> ttl) {
    const Status key_status = validate_key(key);

    if (key_status != Status::kOk) {
        return key_status;
    }

    if (value.size() > kMaxValueBytes) {
        return Status::kValueTooLarge;
    }

    // TTL must be greater than 0
    if (ttl.has_value() && ttl->count() <= 0) {
        return Status::kInvalidTtl;
    }

    if (ttl.has_value() && !std::in_range<std::int64_t>(ttl->count())) {
        return Status::kInvalidTtl;
    }

    Shard& shard = shards_[shard_index(key)];

    // Unique lock gives this writer exclusive access
    std::unique_lock lock{shard.mutex};

    std::optional<Timestamp> expires_at;

    if (ttl.has_value()) {
        const std::int64_t delta = static_cast<std::int64_t>(ttl->count());

        // Time is sampled after acquiring the shard lock
        const std::int64_t now = clock_->now().milliseconds_since_epoch;

        if (now > std::numeric_limits<std::int64_t>::max() - delta) {
            return Status::kInvalidTtl;
        }

        expires_at = Timestamp{now + delta};
    }

    shard.entries.insert_or_assign(std::move(key), Entry{
                                                       std::move(value),
                                                       expires_at,
                                                   });

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

    if (entry->second.expires_at.has_value() && is_expired(entry->second, clock_->now())) {
        return GetResult{Status::kNotFound, {}};
    }

    return GetResult{
        Status::kOk,
        entry->second.value,
    };
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

    const auto entry = shard.entries.find(owned_key);

    if (entry == shard.entries.end()) {
        return Status::kNotFound;
    }

    // An expired entry is logically missing
    const bool expired =
        entry->second.expires_at.has_value() && is_expired(entry->second, clock_->now());

    shard.entries.erase(entry);

    return expired ? Status::kNotFound : Status::kOk;
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

bool KvEngine::is_expired(const Entry& entry, Timestamp now) noexcept {
    // A key is missing at or after the deadline
    return (entry.expires_at.has_value() &&
            now.milliseconds_since_epoch >= entry.expires_at->milliseconds_since_epoch);
}

}  // namespace kv