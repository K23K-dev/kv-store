#pragma once

#include <array>
#include <cstddef>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace kv {

enum class Status {
    kOk,
    kNotFound,
    kInvalidKey,
    kKeyTooLarge,
    kValueTooLarge,
};

struct GetResult {
    Status status;
    std::string value;
};

class KvEngine final {
   public:
    static constexpr std::size_t kMaxKeyBytes = 1024;
    static constexpr std::size_t kMaxValueBytes = std::size_t{1024} * std::size_t{1024};

    [[nodiscard]] Status put(std::string key, std::string value);
    [[nodiscard]] GetResult get(std::string_view key) const;
    [[nodiscard]] Status erase(std::string_view key);

   private:
    static constexpr std::size_t kShardCount = 64;

    struct Shard {
        mutable std::shared_mutex mutex;

        std::unordered_map<std::string, std::string> entries;
    };

    [[nodiscard]] static Status validate_key(std::string_view key) noexcept;

    [[nodiscard]] static std::size_t shard_index(std::string_view key) noexcept;

    std::array<Shard, kShardCount> shards_{};
};

}  // namespace kv