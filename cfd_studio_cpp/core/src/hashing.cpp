#include "core/hashing.hpp"

#define XXH_INLINE_ALL
#include "xxhash.h"

#include <array>
#include <cstdio>

namespace cfd::core {

Hash128 hash_bytes(const void* data, std::size_t size) noexcept {
    XXH128_hash_t h = XXH3_128bits(data, size);
    return Hash128{h.high64, h.low64};
}

std::string to_hex(const Hash128& hash, int hex_chars) {
    std::array<char, 33> buf{}; // 16 bytes -> 32 hex chars + null terminator
    std::snprintf(buf.data(), buf.size(), "%016llx%016llx",
                  static_cast<unsigned long long>(hash.hi),
                  static_cast<unsigned long long>(hash.lo));
    std::string full(buf.data());
    if (hex_chars < 0 || hex_chars > static_cast<int>(full.size())) {
        hex_chars = static_cast<int>(full.size());
    }
    return full.substr(0, static_cast<std::size_t>(hex_chars));
}

} // namespace cfd::core
