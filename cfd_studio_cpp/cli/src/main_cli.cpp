// cfd_headless: the numerics-only CLI for CFD Studio's C++ rewrite --
// mesh load -> orient -> voxelize -> solve -> write OpenFOAM/VTK, exercised
// end to end before any GUI work starts (Phase 5). Also hosts `gen-mesh`, a
// small reusable synthetic-mesh generator (box/tube primitives) for cases
// where no real test mesh is on hand -- e.g. an internal-flow duct.
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "mesh/mesh.hpp"
#include "mesh/primitives.hpp"
#include "mesh/stl_writer.hpp"
#include "pipeline/run_2d.hpp"
#include "pipeline/run_3d.hpp"
#include "solvers/orientation.hpp"
#include "solvers/performance_presets_3d.hpp"

namespace {

class Args {
public:
    Args(int argc, char** argv) {
        for (int i = 0; i < argc; ++i) tokens_.emplace_back(argv[i]);
    }

    [[nodiscard]] std::optional<std::string> get(const std::string& flag) const {
        for (std::size_t i = 0; i < tokens_.size(); ++i) {
            if (tokens_[i] == flag) {
                if (i + 1 >= tokens_.size()) throw std::invalid_argument("missing value for " + flag);
                return tokens_[i + 1];
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] bool has_flag(const std::string& flag) const {
        for (const auto& t : tokens_) {
            if (t == flag) return true;
        }
        return false;
    }

private:
    std::vector<std::string> tokens_;
};

std::string require(const Args& args, const std::string& flag) {
    if (auto v = args.get(flag)) return *v;
    throw std::invalid_argument("missing required argument " + flag);
}

int get_int(const Args& args, const std::string& flag, int def) {
    if (auto v = args.get(flag)) return std::stoi(*v);
    return def;
}

double get_double(const Args& args, const std::string& flag, double def) {
    if (auto v = args.get(flag)) return std::stod(*v);
    return def;
}

std::optional<int> get_opt_int(const Args& args, const std::string& flag) {
    if (auto v = args.get(flag)) return std::stoi(*v);
    return std::nullopt;
}

std::optional<double> get_opt_double(const Args& args, const std::string& flag) {
    if (auto v = args.get(flag)) return std::stod(*v);
    return std::nullopt;
}

// Prints one line per progress callback: step, residual, wall-clock elapsed.
// The CLI is synchronous (no threading/signals needed, unlike the Qt
// workers this pipeline was ported from), so this can just print directly.
auto make_progress_printer(std::chrono::steady_clock::time_point start) {
    return [start](int step, double residual) {
        double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::printf("  step %6d  residual %.6e  elapsed %.1fs\n", step, residual, elapsed);
    };
}

int cmd_2d(const Args& args) {
    cfd::pipeline::Run2DOptions opts;
    opts.scenario = require(args, "--scenario");
    opts.output_dir = require(args, "--output-dir");
    opts.nx = get_opt_int(args, "--nx");
    opts.ny = get_opt_int(args, "--ny");
    opts.Re = get_opt_double(args, "--re");
    opts.U = get_opt_double(args, "--u");
    opts.dt = get_opt_double(args, "--dt");
    opts.n_steps = get_int(args, "--steps", opts.n_steps);
    opts.output_every = get_int(args, "--output-every", opts.output_every);
    if (auto v = args.get("--case-name")) opts.case_name = *v;

    std::printf("cfd_headless 2d: scenario=%s output_dir=%s\n", opts.scenario.c_str(), opts.output_dir.c_str());
    auto start = std::chrono::steady_clock::now();
    auto result = cfd::pipeline::run_2d(opts, make_progress_printer(start));
    std::printf("done: %d steps, final residual %.6e\n  %s\n", result.steps_run, result.final_residual,
                result.pvd_path.c_str());
    return 0;
}

int cmd_3d(const Args& args) {
    std::string mesh_path = require(args, "--mesh");

    cfd::mesh::Mesh mesh = cfd::mesh::load_mesh(mesh_path);
    auto candidates = cfd::solvers::analyze_orientation(mesh);

    int rank = get_int(args, "--orientation-rank", 0);
    if (rank < 0 || rank > 2) throw std::invalid_argument("--orientation-rank must be 0, 1, or 2");
    cfd::solvers::OrientationCandidate candidate = candidates[static_cast<std::size_t>(rank)];

    if (args.has_flag("--reverse-flow")) {
        auto reference_axes = cfd::solvers::principal_axes(mesh);
        cfd::mesh::Vec3 reversed_axis = candidate.flow_axis * -1.0;
        candidate = cfd::solvers::candidate_from_flow_axis(mesh, reversed_axis, candidate.label + "-reversed",
                                                             candidate.rank, &reference_axes);
    }
    mesh = cfd::solvers::apply_orientation(mesh, candidate);
    std::printf("cfd_headless 3d: mesh=%s orientation=\"%s\"\n", mesh_path.c_str(), candidate.label.c_str());

    cfd::pipeline::Run3DOptions opts;
    opts.domain_mode = args.get("--domain-mode").value_or("external");
    opts.mesh_name = std::filesystem::path(mesh_path).filename().string();

    if (auto preset_key = args.get("--preset")) {
        const auto& preset = cfd::solvers::performance_preset_3d(*preset_key);
        opts.nx = preset.nx;
        opts.ny = preset.ny;
        opts.nz = preset.nz;
        opts.n_steps = preset.steps;
        opts.output_every = preset.output_every;
    }
    auto nx_ov = get_opt_int(args, "--nx");
    auto ny_ov = get_opt_int(args, "--ny");
    auto nz_ov = get_opt_int(args, "--nz");
    if (nx_ov || ny_ov || nz_ov) {
        if (!(nx_ov && ny_ov && nz_ov)) throw std::invalid_argument("--nx/--ny/--nz must all be given together");
        opts.nx = *nx_ov;
        opts.ny = *ny_ov;
        opts.nz = *nz_ov;
    }
    opts.n_steps = get_int(args, "--steps", opts.n_steps);
    opts.output_every = get_int(args, "--output-every", opts.output_every);

    opts.Re = get_double(args, "--re", opts.Re);
    opts.U_in = get_double(args, "--u-in", opts.U_in);
    opts.output_dir = require(args, "--output-dir");
    opts.num_threads = get_opt_int(args, "--threads");
    opts.inflow_gap = get_double(args, "--inflow-gap", opts.inflow_gap);
    opts.wake_gap = get_double(args, "--wake-gap", opts.wake_gap);
    opts.lateral_gap = get_double(args, "--lateral-gap", opts.lateral_gap);
    opts.force_rerun = args.has_flag("--force-rerun");
    if (auto v = args.get("--cache-dir")) opts.cache_root = *v;

    std::printf("  domain_mode=%s grid=%dx%dx%d steps=%d\n", opts.domain_mode.c_str(), opts.nx, opts.ny, opts.nz,
                opts.n_steps);
    auto start = std::chrono::steady_clock::now();
    auto result = cfd::pipeline::run_3d(mesh, opts, make_progress_printer(start));
    if (result.was_cached) {
        std::printf("done: reused cached run\n  %s\n", result.foam_path.c_str());
    } else {
        std::printf("done: %d steps, final residual %.6e\n  %s\n", result.steps_run, result.final_residual,
                    result.foam_path.c_str());
    }
    return 0;
}

int cmd_gen_mesh(const Args& args) {
    std::string shape = require(args, "--shape");
    std::string out = require(args, "--out");

    cfd::mesh::Mesh mesh;
    if (shape == "box") {
        double length = get_double(args, "--length", 1.0);
        double width = get_double(args, "--width", 1.0);
        double height = get_double(args, "--height", 1.0);
        mesh = cfd::mesh::make_box({length, width, height});
    } else if (shape == "tube") {
        double length = get_double(args, "--length", 4.0);
        double radius = get_double(args, "--radius", 0.5);
        double wall_thickness = get_double(args, "--wall-thickness", 0.1);
        int segments = get_int(args, "--segments", 24);
        bool capped = args.has_flag("--capped");
        mesh = cfd::mesh::make_tube(length, radius, wall_thickness, segments, capped);
    } else {
        throw std::invalid_argument("gen-mesh: --shape must be \"box\" or \"tube\"");
    }

    cfd::mesh::write_stl(mesh, out);
    std::printf("gen-mesh: wrote %zu vertices, %zu triangles to %s\n", mesh.vertices.size(), mesh.triangles.size(),
                out.c_str());
    return 0;
}

void print_usage() {
    std::puts(
        "usage:\n"
        "  cfd_headless 2d --scenario cavity|channel|obstacle --output-dir DIR\n"
        "               [--nx N] [--ny N] [--re R] [--u U] [--dt DT]\n"
        "               [--steps N] [--output-every N] [--case-name NAME]\n"
        "\n"
        "  cfd_headless 3d --mesh PATH --output-dir DIR\n"
        "               [--domain-mode external|internal] [--preset fast_preview|balanced|high_quality]\n"
        "               [--nx N --ny N --nz N] [--re R] [--u-in U] [--steps N] [--output-every N]\n"
        "               [--threads N] [--inflow-gap F] [--wake-gap F] [--lateral-gap F]\n"
        "               [--orientation-rank 0|1|2] [--reverse-flow] [--force-rerun] [--cache-dir DIR]\n"
        "\n"
        "  cfd_headless gen-mesh --shape box|tube --out PATH.stl\n"
        "               [--length L] [--width W] [--height H]                 (box)\n"
        "               [--length L] [--radius R] [--wall-thickness T] [--segments N] [--capped]  (tube)\n");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }
    std::string command = argv[1];
    Args args(argc - 2, argv + 2);

    try {
        if (command == "2d") return cmd_2d(args);
        if (command == "3d") return cmd_3d(args);
        if (command == "gen-mesh") return cmd_gen_mesh(args);
        std::fprintf(stderr, "error: unknown subcommand \"%s\"\n\n", command.c_str());
        print_usage();
        return 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
