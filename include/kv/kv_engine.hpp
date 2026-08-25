#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace kv {

struct Timestamp {
    std::int64_t milliseconds_since_epoch;
};

class Clock {
   public:
    virtual ~Clock() = default;

    // now() must be thread-safe because different shards
    // may call it concurrently
    [[nodiscard]] virtual Timestamp now() const noexcept = 0;
};

enum class Status {
    kOk,
    kNotFound,
    kInvalidKey,
    kKeyTooLarge,
    kValueTooLarge,
    kInvalidTtl,
};

struct GetResult {
    Status status;
    std::string value;
};

class KvEngine final {
   public:
    static constexpr std::size_t kMaxKeyBytes = 1024;
    static constexpr std::size_t kMaxValueBytes = std::size_t{1024} * std::size_t{1024};

    KvEngine();
    explicit KvEngine(std::shared_ptr<const Clock> clock);

    [[nodiscard]] Status put(std::string key, std::string value,
                             std::optional<std::chrono::milliseconds> ttl = std::nullopt);

    [[nodiscard]] GetResult get(std::string_view key) const;
    [[nodiscard]] Status erase(std::string_view key);

   private:
    static constexpr std::size_t kShardCount = 64;

    struct Entry {
        std::string value;
        std::optional<Timestamp> expires_at;
    };

    struct Shard {
        mutable std::shared_mutex mutex;

        std::unordered_map<std::string, Entry> entries;
    };

    [[nodiscard]] static Status validate_key(std::string_view key) noexcept;

    [[nodiscard]] static std::size_t shard_index(std::string_view key) noexcept;

    [[nodiscard]] static bool is_expired(const Entry& entry, Timestamp now) noexcept;

    std::shared_ptr<const Clock> clock_;

    std::array<Shard, kShardCount> shards_{};
};

}  // namespace kv