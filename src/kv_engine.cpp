#include "kv/kv_engine.hpp"

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

    entries_.insert_or_assign(std::move(key), std::move(value));
    return Status::kOk;
}

GetResult KvEngine::get(std::string_view key) const {
    const Status key_status = validate_key(key);

    if (key_status != Status::kOk) {
        return GetResult{key_status, {}};
    }

    const auto entry = entries_.find(std::string{key});

    if (entry == entries_.end()) {
        return GetResult{Status::kNotFound, {}};
    }

    return GetResult{Status::kOk, entry->second};
}

Status KvEngine::erase(std::string_view key) {
    const Status key_status = validate_key(key);

    if (key_status != Status::kOk) {
        return key_status;
    }

    if (entries_.erase(std::string{key}) == 0U) {
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

}  // namespace kv