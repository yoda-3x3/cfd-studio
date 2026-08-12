#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace cfd::core {

// Content-addressed hashing for cache keys (mesh vertex/face buffers, run
// parameters). The Python app used SHA-256 for this (solver3d/cache.py),
// but the actual requirement is collision-avoidance for cache keys, not
// cryptographic strength -- xxHash (XXH3-128) is far faster on large
// vertex/face buffers. 128 bits (not 64) so a 24-hex-char truncation --
// matching cache.py's own `[:24]` truncation of its SHA-256 hex digest --
// still has real entropy behind it (a 64-bit hash would run out at 16 hex
// chars).
struct Hash128 {
    std::uint64_t hi;
    std::uint64_t lo;
};

[[nodiscard]] Hash128 hash_bytes(const void* data, std::size_t size) noexcept;

[[nodiscard]] std::string to_hex(const Hash128& hash, int hex_chars = 24);

// Convenience: hash_bytes + to_hex in one call, the common case for
// building a cache key from one buffer.
[[nodiscard]] inline std::string hash_hex(const void* data, std::size_t size, int hex_chars = 24) {
    return to_hex(hash_bytes(data, size), hex_chars);
}

} // namespace cfd::core
