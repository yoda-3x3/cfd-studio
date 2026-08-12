#include "core/binary_morphology.hpp"

namespace cfd::core {

namespace {
inline std::size_t idx(int i, int j, int k, int ny, int nz) {
    return static_cast<std::size_t>(i) * static_cast<std::size_t>(ny) * static_cast<std::size_t>(nz)
         + static_cast<std::size_t>(j) * static_cast<std::size_t>(nz)
         + static_cast<std::size_t>(k);
}
} // namespace

std::vector<std::uint8_t> dilate_6connectivity(
    const std::vector<std::uint8_t>& mask, int nx, int ny, int nz, int iterations) {
    std::vector<std::uint8_t> current = mask;
    for (int iter = 0; iter < iterations; ++iter) {
        std::vector<std::uint8_t> next = current;
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                for (int k = 0; k < nz; ++k) {
                    if (current[idx(i, j, k, ny, nz)]) continue;
                    bool grow =
                        (i > 0 && current[idx(i - 1, j, k, ny, nz)]) ||
                        (i + 1 < nx && current[idx(i + 1, j, k, ny, nz)]) ||
                        (j > 0 && current[idx(i, j - 1, k, ny, nz)]) ||
                        (j + 1 < ny && current[idx(i, j + 1, k, ny, nz)]) ||
                        (k > 0 && current[idx(i, j, k - 1, ny, nz)]) ||
                        (k + 1 < nz && current[idx(i, j, k + 1, ny, nz)]);
                    if (grow) next[idx(i, j, k, ny, nz)] = 1;
                }
            }
        }
        current = std::move(next);
    }
    return current;
}

} // namespace cfd::core
