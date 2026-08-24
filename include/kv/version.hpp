#pragma once

#include <string_view>

namespace kv {

[[nodiscard]] std::string_view version() noexcept;

}  // namespace kv