#pragma once

#include <cstddef>
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
    [[nodiscard]] static Status validate_key(std::string_view key) noexcept;

    std::unordered_map<std::string, std::string> entries_;
};

}  // namespace kv