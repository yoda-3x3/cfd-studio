#include "core/connected_components.hpp"

#include <array>
#include <deque>

namespace cfd::core {

namespace {
inline std::size_t idx(int i, int j, int k, int ny, int nz) {
    return static_cast<std::size_t>(i) * static_cast<std::size_t>(ny) * static_cast<std::size_t>(nz)
         + static_cast<std::size_t>(j) * static_cast<std::size_t>(nz)
         + static_cast<std::size_t>(k);
}
} // namespace

ComponentLabels label_components_6connectivity(
    const std::vector<std::uint8_t>& mask, int nx, int ny, int nz) {
    ComponentLabels result;
    result.labels.assign(mask.size(), 0);

    std::deque<std::array<int, 3>> queue;
    int next_label = 0;

    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            for (int k = 0; k < nz; ++k) {
                std::size_t start = idx(i, j, k, ny, nz);
                if (mask[start] == 0 || result.labels[start] != 0) continue;

                ++next_label;
                result.labels[start] = next_label;
                queue.push_back({i, j, k});

                while (!queue.empty()) {
                    auto [ci, cj, ck] = queue.front();
                    queue.pop_front();

                    static constexpr int offsets[6][3] = {
                        {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1},
                    };
                    for (const auto& off : offsets) {
                        int ni = ci + off[0], nj = cj + off[1], nk = ck + off[2];
                        if (ni < 0 || ni >= nx || nj < 0 || nj >= ny || nk < 0 || nk >= nz) continue;
                        std::size_t nidx = idx(ni, nj, nk, ny, nz);
                        if (mask[nidx] == 0 || result.labels[nidx] != 0) continue;
                        result.labels[nidx] = next_label;
                        queue.push_back({ni, nj, nk});
                    }
                }
            }
        }
    }

    result.num_components = next_label;
    return result;
}

} // namespace cfd::core
