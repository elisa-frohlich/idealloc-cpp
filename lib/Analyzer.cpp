#include "Analyzer.h"
#include "Helper.h"
#include "algo/Boxing.h"
#include "algo/Placement.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

AnalysisResult prelude_analysis(JobSet jobs, std::size_t start_addr) {
    const auto prelude_cost = std::chrono::steady_clock::now();

    //--- Overlap / same-size detection ---
    bool last_evt_was_birth = false;
    bool overlap_exists = false;
    bool same_sizes = false;

    //--- Max-load calculation ---
    std::size_t running_load = 0;
    std::size_t max_load = 0;

    //--- Size-uniformity detection ---
    std::unordered_set<std::size_t> sizes;

    //--- Interference-graph construction ---
    InterferenceGraph ig;
    PlacedJobRegistry registry;
    PlacedJobRegistry live;

    //--- BA configuration ---
    std::size_t h_min = std::numeric_limits<std::size_t>::max();
    std::size_t h_max = 0;
    std::size_t max_death = 0;
    std::uint32_t max_id = 0;

    //--- Hardness characterization ---
    std::size_t sizes_sum = 0;
    std::size_t deaths_sum = 0;

    Events evts = jobset::get_events(jobs);
    while (!evts.empty()) {
        Event e = evts.top();
        evts.pop();

        if (e.evt_t == EventKind::Birth) {
            const auto& ejob = e.job;

            if (ejob->size < h_min) h_min = ejob->size;
            if (ejob->size > h_max) h_max = ejob->size;
            if (ejob->id > max_id) max_id = ejob->id;
            sizes_sum += ejob->size;

            //--- MAX LOAD UPDATE ---
            running_load += ejob->size;
            if (running_load > max_load) max_load = running_load;
            //--- END MAX LOAD UPDATE ---

            sizes.insert(ejob->size);

            //--- IG BUILDING ---
            // Rust: let init_vec: PlacedJobSet = live.values().cloned().collect();
            PlacedJobSet init_vec;
            init_vec.reserve(live.size());
            for (const auto& [id, pj] : live) {
                init_vec.push_back(pj);   // copy shared_ptr == Rc::clone
            }

            PlacedJobPtr new_entry = std::make_shared<PlacedJob>(ejob);

            // Rust: ig.insert(e.job.id, init_vec); registry.insert(...);
            ig[ejob->id] = std::move(init_vec);
            registry[ejob->id] = new_entry;

            // Rust: for (_, j) in &live { assert!(j.overlaps_with(&new_entry)); ... }
            for (const auto& [id, pj] : live) {
                assert(pj->overlaps_with(*new_entry));
                ig.at(pj->descr->id).push_back(new_entry);
            }

            live[ejob->id] = new_entry;
            //--- END IG BUILDING ---

            if (last_evt_was_birth && !overlap_exists && !same_sizes) {
                overlap_exists = true;
                if (sizes.size() == 1) {
                    // Rust: let size_probe = sizes.take(&e.job.size).unwrap();
                    auto it = sizes.find(ejob->size);
                    assert(it != sizes.end());
                    const std::size_t size_probe = *it;
                    sizes.erase(it);

                    bool all_match = true;
                    for (const auto& j : jobs) {
                        if (j->size != size_probe) { all_match = false; break; }
                    }
                    if (all_match) same_sizes = true;
                }
            }
            last_evt_was_birth = true;

        } else {  // EventKind::Death
            //--- MAX LOAD UPDATE ---
            if (running_load >= e.job->size) {
                running_load -= e.job->size;
            } else {
                throw std::runtime_error("Almost overflowed load!");
            }
            //--- END MAX LOAD UPDATE ---

            if (!overlap_exists) last_evt_was_birth = false;
            live.erase(e.job->id);
            deaths_sum += e.job->death;
            if (e.job->death > max_death) max_death = e.job->death;
        }
    }

    std::cout << "Events processed.\n";

    auto report_prelude = [&] {
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - prelude_cost).count();
        std::cout << "Prelude overhead: " << us << " μs\n";
    };

    if (!overlap_exists) {
        report_prelude();
        return analysis_result::NoOverlap{ std::move(jobs) };
    }

    if (same_sizes) {
        report_prelude();
        return analysis_result::SameSizes{ std::move(jobs),
                                           std::move(ig),
                                           std::move(registry) };
    }

    //--- Third branch: BA is needed. ---

    // Rust: let ordered: PlacedJobSet = registry.values().sorted_by(...).cloned().collect();
    PlacedJobSet ordered;
    ordered.reserve(registry.size());
    for (const auto& [id, pj] : registry) {
        ordered.push_back(pj);
    }
    std::stable_sort(ordered.begin(), ordered.end(),
        [](const PlacedJobPtr& a, const PlacedJobPtr& b) {
            // Rust: b.size.cmp(&a.size).then(b.lifetime.cmp(&a.lifetime))
            //       Descending by size, then descending by lifetime.
            if (a->descr->size != b->descr->size) {
                return a->descr->size > b->descr->size;
            }
            return a->descr->lifetime() > b->descr->lifetime();
        });
    std::cout << "Size-life ordering done.\n";

    std::size_t symbolic_offset = 0;
    for (auto& pj : ordered) {
        pj->offset = symbolic_offset;
        ++symbolic_offset;
    }

    // Build a LoosePlacement from `ordered` and run do_best_fit.
    LoosePlacement loose(ordered.begin(), ordered.end(), PlacedJobPtrGreater{});
    const std::size_t best_opt = do_best_fit(
        std::move(loose),
        ig,
        /*iters_done=*/0,
        std::numeric_limits<std::size_t>::max(),
        /*first_fit=*/true,
        start_addr);
    std::cout << "First-fit done.\n";

    //--- Compute epsilon, mu_lim, etc. ---
    const double r = static_cast<double>(h_max) / static_cast<double>(h_min);
    const double lgr = std::log2(r);
    const double lg2r = lgr * lgr;
    const double small_end = std::pow(
        std::pow(lg2r, 7.0) / r, 1.0 / 6.0);
    const double mu_lim = (std::sqrt(5.0) - 1.0) / 2.0;
    const double big_end = mu_lim * lg2r;

    std::size_t to_box = jobs.size();
    std::shared_ptr<Job> dummy;  // empty = None
    const std::size_t real_load = max_load;

    //--- Instance characterization ---
    const double h_mean = static_cast<double>(sizes_sum) / to_box;
    const double death_mean = static_cast<double>(deaths_sum) / to_box;

    double height_squared_devs = 0.0;
    double death_squared_devs = 0.0;
    for (const auto& j : jobs) {
        const double ds = static_cast<double>(j->size) - h_mean;
        const double dd = static_cast<double>(j->death) - death_mean;
        height_squared_devs += ds * ds;
        death_squared_devs += dd * dd;
    }
    const double size_std = std::sqrt(height_squared_devs / to_box);
    const double death_std = std::sqrt(death_squared_devs / to_box);
    const double h_hardness = size_std / h_mean;
    const double death_hardness = death_std / death_mean;

    std::size_t double_num_conflicts = 0;
    for (const auto& [id, js] : ig) {
        double_num_conflicts += js.size();
    }
    assert(double_num_conflicts % 2 == 0);
    const std::size_t num_two_combos = to_box * (to_box - 1) / 2;
    const double conflict_hardness =
        (static_cast<double>(double_num_conflicts / 2)) /
        static_cast<double>(num_two_combos);

    if (small_end >= big_end) {
        // Plant a "dummy" job to force the r > 2216.53... condition.
        h_max = static_cast<std::size_t>(
            std::ceil(2216.54 * static_cast<double>(h_min)));

        Job dj;
        dj.size            = h_max;
        dj.req_size        = h_max;
        dj.birth           = 0;
        dj.death           = max_death;
        dj.originals_boxed = 0;
        dj.alignment       = std::nullopt;
        dj.contents        = std::nullopt;
        dj.id              = max_id + 1;

        std::shared_ptr<Job> dummy_job = std::make_shared<Job>(std::move(dj));
        jobs.push_back(dummy_job);
        ++to_box;
        max_load += h_max;
        dummy = dummy_job;
    }

    auto instance = std::make_shared<Instance>(std::move(jobs));
    instance->info.set_load(max_load);
    instance->info.set_heights(h_min, h_max);

    auto [inst_r, ctrl_small_end, ctrl_big_end, ctrl_ok] =
        instance->ctrl_prelude();
    (void)inst_r; (void)ctrl_ok;
    assert(ctrl_small_end < ctrl_big_end);

    auto [epsilon, pre_boxed] = init_rogue(instance, ctrl_small_end, ctrl_big_end);

    report_prelude();

    BACtrl ctrl;
    ctrl.input      = instance;
    ctrl.pre_boxed  = std::move(pre_boxed);
    ctrl.epsilon    = epsilon;
    ctrl.to_box     = to_box;
    ctrl.real_load  = real_load;
    ctrl.dummy      = std::move(dummy);
    ctrl.ig         = std::move(ig);
    ctrl.reg        = std::move(registry);
    ctrl.mu_lim     = mu_lim;
    ctrl.best_opt   = best_opt;
    ctrl.hardness   = std::make_tuple(h_hardness, conflict_hardness, death_hardness);

    return analysis_result::NeedsBA{ std::move(ctrl) };
}

std::pair<double, std::shared_ptr<Instance>>
init_rogue(std::shared_ptr<Instance> input, double small, double big) {
    // std::cerr << "init rogue: small=" << small << " , big=" << big << "\n";
    double e = small;
    double min_r = std::numeric_limits<double>::max();
    double best_e = e;
    int tries_left = 3;
    std::shared_ptr<Instance> best = input;

    while (true) {
        if (tries_left > 0) {
            std::shared_ptr<Instance> test = rogue(input, e);
            auto [r, _1, _2, _3] = test->get_safety_info(e);

            if (r < min_r) {
                min_r = r;
                best_e = e;
                best = std::move(test);
                tries_left = 3;
            } else {
                --tries_left;
            }
            e += (big - e) * 0.01;
        } else {
            break;
        }
    }

    return { best_e, std::move(best) };
}

bool placement_is_valid(
    const std::pair<InterferenceGraph, PlacedJobRegistry>& ig_reg)
{
    const InterferenceGraph& ig  = ig_reg.first;
    const PlacedJobRegistry& reg = ig_reg.second;

    for (const auto& [id, jobs] : ig) {
        // Rust: reg.get(id).unwrap()
        //   std::unordered_map::at throws on missing -- matches unwrap's panic.
        const PlacedJobPtr& this_job = reg.at(id);

        const std::size_t this_job_start = this_job->offset;
        const std::size_t this_job_end   = this_job->next_avail_offset() - 1;

        for (const auto& j : jobs) {
            const std::size_t that_job_start = j->offset;
            const std::size_t that_job_end   = j->next_avail_offset() - 1;

            if (that_job_start > this_job_end) {
                continue;
            } else if (that_job_start >= this_job_start) {
                std::cerr << *this_job << " conflicts with " << *j << "!\n";
                return false;
            } else if (that_job_end >= this_job_start) {
                std::cerr << *this_job << " conflicts with " << *j << "!\n";
                return false;
            }
        }
    }

    return true;
}