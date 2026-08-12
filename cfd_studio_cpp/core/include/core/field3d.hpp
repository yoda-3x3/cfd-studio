#pragma once

#include <cstdint>
#include <vector>

#include "core/grid_index.hpp"

namespace cfd::core {

// Ghost-cell-padded (nx+2, ny+2, nz+2) scalar field, matching the layout
// solver3d/navier_stokes3d.py and kernels.cpp already use. Real cells are
// 1..nx / 1..ny / 1..nz.
class Field3D {
public:
    Field3D(int nx, int ny, int nz, double fill = 0.0)
        : nx_(nx), ny_(ny), nz_(nz), data_(padded_total3(nx, ny, nz), fill) {}

    [[nodiscard]] double& operator()(int i, int j, int k) noexcept { return data_[idx3(i, j, k, ny_, nz_)]; }
    [[nodiscard]] double operator()(int i, int j, int k) const noexcept { return data_[idx3(i, j, k, ny_, nz_)]; }

    [[nodiscard]] double* data() noexcept { return data_.data(); }
    [[nodiscard]] const double* data() const noexcept { return data_.data(); }
    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }

    [[nodiscard]] int nx() const noexcept { return nx_; }
    [[nodiscard]] int ny() const noexcept { return ny_; }
    [[nodiscard]] int nz() const noexcept { return nz_; }

private:
    int nx_, ny_, nz_;
    std::vector<double> data_;
};

// Same padded layout, boolean mask (solid cell = true), stored as int8_t to
// match the int8 mask kernels.cpp/kernel_backend.hpp already expect at the
// ctypes/C-ABI boundary (numpy bool_ is byte-compatible with int8).
class SolidMask3D {
public:
    SolidMask3D(int nx, int ny, int nz, bool fill = false)
        : nx_(nx), ny_(ny), nz_(nz), data_(padded_total3(nx, ny, nz), fill ? 1 : 0) {}

    [[nodiscard]] std::int8_t& operator()(int i, int j, int k) noexcept { return data_[idx3(i, j, k, ny_, nz_)]; }
    [[nodiscard]] std::int8_t operator()(int i, int j, int k) const noexcept { return data_[idx3(i, j, k, ny_, nz_)]; }

    [[nodiscard]] const std::int8_t* data() const noexcept { return data_.data(); }
    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }

    [[nodiscard]] int nx() const noexcept { return nx_; }
    [[nodiscard]] int ny() const noexcept { return ny_; }
    [[nodiscard]] int nz() const noexcept { return nz_; }

private:
    int nx_, ny_, nz_;
    std::vector<std::int8_t> data_;
};

} // namespace cfd::core
