#include "algo/Mod.h"
#include "Analyzer.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <tuple>
#include <type_traits>
#include <variant>

std::pair<PlacedJobRegistry, std::size_t>
idealloc(JobSet original_input,
         double worst_case_frag,
         std::size_t start_address,
         std::uint32_t max_lives)
{
    // Rust: rayon::ThreadPoolBuilder::new().stack_size(1 GiB).build_global().unwrap()
    //   There is no portable C++ equivalent of rayon's global thread pool.
    //   If you use std::execution::par, stack size is controlled by the
    //   runtime (pthread/tbb). If you need a deeper stack specifically, set
    //   the thread stack via a custom std::thread with pthread_attr_setstacksize,
    //   or link against TBB and configure it. In practice, unless you see
    //   stack overflow in the recursive core, you can skip this.

    const auto total_start = std::chrono::steady_clock::now();

    // Rust: match prelude_analysis(original_input, start_address) { ... }
    //
    // std::visit lets us match on the variant returned by prelude_analysis.
    // The lambda returns a triple (target_load, best_opt, placement), matching
    // the Rust `let (target_load, best_opt, placement) = match ...`.
    auto [target_load, best_opt, placement] = std::visit(
        [&](auto&& alt) -> std::tuple<std::size_t, std::size_t, PlacedJobRegistry> {
            using T = std::decay_t<decltype(alt)>;

            // -------- NoOverlap --------
            if constexpr (std::is_same_v<T, analysis_result::NoOverlap>) {
                JobSet jobs = std::move(alt.jobs);

                PlacedJobRegistry reg;
                for (auto& j : jobs) {
                    auto placed = std::make_shared<PlacedJob>(j);
                    placed->offset = placed->get_corrected_offset(start_address, 0);
                    reg.emplace(placed->descr->id, std::move(placed));
                }

                return { jobset::get_load(jobs),
                         jobset::get_max_size(jobs),
                         std::move(reg) };
            }
            // -------- SameSizes --------
            else if constexpr (std::is_same_v<T, analysis_result::SameSizes>) {
                JobSet jobs = std::move(alt.jobs);
                InterferenceGraph ig = std::move(alt.ig);
                PlacedJobRegistry reg = std::move(alt.reg);

                const std::size_t l = jobset::get_load(jobs);
                const std::size_t row_size = jobs[0]->size;

                LoosePlacement loose;

                std::size_t row_idx = 0;
                for (auto& igc_row : jobset::interval_graph_coloring(std::move(jobs))) {
                    for (auto& j : igc_row) {
                        // Rust: reg.get(&j.id).unwrap()
                        PlacedJobPtr semi_placed = reg.at(j->id);
                        semi_placed->offset = row_idx * row_size;
                        loose.push(semi_placed);
                    }
                    ++row_idx;
                }

                const std::size_t makespan = do_best_fit(
                    std::move(loose), ig,
                    /*iters_done=*/0,
                    std::numeric_limits<std::size_t>::max(),
                    /*first_fit=*/false,
                    start_address);

                return { l, makespan, std::move(reg) };
            }
            // -------- NeedsBA --------
            else {
                // alt is analysis_result::NeedsBA
                BACtrl& ctrl = alt.ctrl;

                // Move out the pieces we need to mutate locally.
                std::shared_ptr<Instance> input     = std::move(ctrl.input);
                std::shared_ptr<Instance> pre_boxed = std::move(ctrl.pre_boxed);
                const std::size_t         to_box    = ctrl.to_box;
                const double              epsilon   = ctrl.epsilon;
                const std::size_t         real_load = ctrl.real_load;
                const std::shared_ptr<Job> dummy    = std::move(ctrl.dummy);
                InterferenceGraph         ig        = std::move(ctrl.ig);
                PlacedJobRegistry         reg       = std::move(ctrl.reg);
                const double              mu_lim    = ctrl.mu_lim;
                std::size_t               best_opt_local = ctrl.best_opt;
                const auto                hardness  = ctrl.hardness;   // tuple<double,double,double>

                const std::size_t heuristic_opt = best_opt_local;

                // Initializations
                std::uint32_t lives_left = max_lives;
                std::uint32_t total_iters = 1;
                const std::size_t target_opt =
                    static_cast<std::size_t>(std::floor(
                        static_cast<double>(real_load) * worst_case_frag));

                const std::uint32_t dumb_id =
                    dummy ? dummy->id : (std::numeric_limits<std::uint32_t>::max() / 2 + 1);

                // Build final_placement from reg
                auto snapshot_registry = [](const PlacedJobRegistry& src) {
                    PlacedJobRegistry snap;
                    for (const auto& [id, pj] : src) {
                        auto baby = std::make_shared<PlacedJob>(pj->descr);
                        baby->offset = pj->offset;
                        snap.emplace(baby->descr->id, std::move(baby));
                    }
                    return snap;
                };
                PlacedJobRegistry final_placement = snapshot_registry(reg);

                // Rust: let ig_reg = (ig, reg);
                //   Since `place` takes `&(InterferenceGraph, PlacedJobRegistry)`,
                //   we build a pair to pass by reference. It doesn't own anything
                //   new -- the underlying PlacedJobs are shared_ptr.
                std::pair<InterferenceGraph, PlacedJobRegistry> ig_reg{
                    std::move(ig), std::move(reg) };

                // Rust: debug_assert!(placement_is_valid(&ig_reg));
                //   placement_is_valid should take a reference to the same pair type.
                assert(placement_is_valid(ig_reg));

                // Rust: let (_, mut mu, _, _) = pre_boxed.get_safety_info(epsilon);
                auto [safety_r, mu, safety_h, safety_ok] =
                    pre_boxed->get_safety_info(epsilon);
                (void)safety_r; (void)safety_h; (void)safety_ok;
                if (mu > mu_lim) mu = 0.99 * mu_lim;

                auto [h_min, h_max] = input->min_max_height();
                (void)h_min;
                const double final_h =
                    static_cast<double>(h_max) / mu;

                while (lives_left > 0 && best_opt_local > target_opt) {
                    // std::cerr << lives_left << "\n";
                    Instance boxed = c_15(pre_boxed, final_h, mu);

                    assert(boxed.check_boxed_originals(
                        static_cast<std::uint32_t>(to_box)));

                    const std::size_t current_opt = boxed.place(
                        ig_reg, total_iters, best_opt_local,
                        dumb_id, start_address);

                    assert(current_opt == std::numeric_limits<std::size_t>::max()
                           || current_opt >= real_load);

                    if (current_opt < best_opt_local) {
                        assert(placement_is_valid(ig_reg));
                        best_opt_local = current_opt;
                        std::cout << "Beating heuristic by "
                                  << (heuristic_opt - best_opt_local)
                                  << " bytes! (" << total_iters
                                  << " iterations)\n";
                        final_placement = snapshot_registry(ig_reg.second);
                    }
                    ++total_iters;
                    --lives_left;
                    if (lives_left > 0 && best_opt_local > target_opt) {
                        pre_boxed = rogue(input, epsilon);
                    } else {
                        break;
                    }
                }

                const std::size_t num_buffers = ig_reg.second.size();
                std::cout
                    << "\nBuffers treated:\t" << num_buffers
                    << "\nHeights hardness:\t"
                    << (std::get<0>(hardness) * 100.0) << "%"
                    << "\nConflicts hardness:\t"
                    << (std::get<1>(hardness) * 100.0) << "%"
                    << "\nDeaths hardness:\t"
                    << (std::get<2>(hardness) * 100.0) << "%"
                    << "\nCompound hardness:\t"
                    << ((1.0 + std::get<0>(hardness))
                        * (1.0 + std::get<1>(hardness))
                        * (1.0 + std::get<2>(hardness)))
                    << "\n" << (heuristic_opt - best_opt_local)
                    << " fewer bytes than heuristic.\n\n";

                return { real_load, best_opt_local, std::move(final_placement) };
            }
        },
        prelude_analysis(std::move(original_input), start_address));

    // Final prints
    const auto total_end = std::chrono::steady_clock::now();
    const auto total_us =
        std::chrono::duration_cast<std::chrono::microseconds>(
            total_end - total_start).count();
    std::cout << "Total allocation time: " << total_us << " μs\n";

    std::cout
        << "Makespan:\t" << best_opt << " bytes\n"
        << "LOAD:\t\t" << target_load << " bytes\n"
        << "Fragmentation:\t "
        << ((static_cast<double>(best_opt - target_load) /
             static_cast<double>(target_load)) * 100.0)
        << "%\n";

    return { std::move(placement), best_opt };
}