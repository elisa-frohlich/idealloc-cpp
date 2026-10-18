#include "Helper.h"
#include "algo/Placement.h"

inline void append_heap(LoosePlacement& dst, LoosePlacement src) {
    while (!src.empty()) { dst.push(src.top()); src.pop(); }
}

inline Job unwrap_or_clone(std::shared_ptr<Job> p) {
    if (p.use_count() == 1) return std::move(*p);
    return *p;
}

LoosePlacement get_loose_placement(
    JobSet jobs,
    std::size_t start_offset,
    UnboxCtrl control_state,
    const PlacedJobRegistry& ig,
    std::uint32_t dumb_id)
{
    LoosePlacement res;

    if (jobs.size() == 1) {
        std::shared_ptr<Job> only_job = jobs[0];
        if (only_job->is_original()) {
            if (only_job->id != dumb_id) {
                // Rust: ig.get(&only_job.id).unwrap().clone()
                //   .at() throws std::out_of_range on missing, matching unwrap's panic.
                PlacedJobPtr to_put = ig.at(only_job->id);
                to_put->offset = start_offset;
                res.push(std::move(to_put));
            }
        } else {
            // Rust: Arc::unwrap_or_clone(only_job).contents.unwrap()
            Job inner = unwrap_or_clone(std::move(only_job));
            // .value() throws std::bad_optional_access, matching unwrap's panic.
            JobSet inner_contents = std::move(inner.contents.value());
            append_heap(res, get_loose_placement(
                std::move(inner_contents),
                start_offset,
                UnboxCtrl{ unbox_ctrl::Unknown{} },
                ig, dumb_id));
        }
    } else {
        std::visit([&](auto&& alt) {
            using T = std::decay_t<decltype(alt)>;

            if constexpr (std::is_same_v<T, unbox_ctrl::SameSizes>) {
                const std::size_t row_height = alt.size;
                auto rows = jobset::interval_graph_coloring(std::move(jobs));
                for (auto& row : rows) {
                    append_heap(res, get_loose_placement(
                        std::move(row),
                        start_offset,
                        UnboxCtrl{ unbox_ctrl::NonOverlapping{} },
                        ig, dumb_id));
                    start_offset += row_height;
                }
            } else if constexpr (std::is_same_v<T, unbox_ctrl::NonOverlapping>) {
                for (auto& j : jobs) {
                    if (j->is_original()) {
                        if (j->id != dumb_id) {
                            PlacedJobPtr to_put = ig.at(j->id);
                            to_put->offset = start_offset;
                            res.push(std::move(to_put));
                        }
                    } else {
                        Job inner = unwrap_or_clone(std::move(j));
                        JobSet inner_contents = std::move(inner.contents.value());
                        append_heap(res, get_loose_placement(
                            std::move(inner_contents),
                            start_offset,
                            UnboxCtrl{ unbox_ctrl::Unknown{} },
                            ig, dumb_id));
                    }
                }
            } else {
                // Unknown: probe for same size.
                const std::size_t size_probe = jobs[0]->size;
                bool all_same_size = true;
                for (std::size_t i = 1; i < jobs.size(); ++i) {
                    if (jobs[i]->size != size_probe) { all_same_size = false; break; }
                }

                if (all_same_size) {
                    append_heap(res, get_loose_placement(
                        std::move(jobs),
                        start_offset,
                        UnboxCtrl{ unbox_ctrl::SameSizes{ size_probe } },
                        ig, dumb_id));
                } else {
                    // Check non-overlap: births and deaths must alternate.
                    Events evts = jobset::get_events(jobs);
                    bool last_was_birth = false;
                    bool non_overlapping = true;
                    while (!evts.empty()) {
                        Event e = evts.top();
                        evts.pop();
                        if (e.evt_t == EventKind::Birth) {
                            if (last_was_birth) { non_overlapping = false; break; }
                            last_was_birth = true;
                        } else {
                            last_was_birth = false;
                        }
                    }

                    if (non_overlapping) {
                        append_heap(res, get_loose_placement(
                            std::move(jobs),
                            start_offset,
                            UnboxCtrl{ unbox_ctrl::NonOverlapping{} },
                            ig, dumb_id));
                    } else {
                        // Multiple sizes and overlapping: split into size buckets.
                        std::unordered_map<std::size_t, JobSet> size_buckets;
                        for (const auto& j : jobs) {
                            size_buckets[j->size].push_back(j);
                        }
                        for (auto& [row_height, size_class] : size_buckets) {
                            auto igc_rows = jobset::interval_graph_coloring(std::move(size_class));
                            for (auto& row : igc_rows) {
                                append_heap(res, get_loose_placement(
                                    std::move(row),
                                    start_offset,
                                    UnboxCtrl{ unbox_ctrl::NonOverlapping{} },
                                    ig, dumb_id));
                                start_offset += row_height;
                            }
                        }
                    }
                }
            }
        }, control_state);
    }

    return res;
}

std::size_t do_best_fit(
    LoosePlacement loose,           // by value: Rust takes it by value
    const InterferenceGraph& ig,
    std::uint32_t iters_done,
    std::size_t makespan_lim,
    bool first_fit,
    std::size_t start_addr)
{
    std::size_t max_address = 0;

    // Rust: while let Some(to_squeeze) = loose.pop()
    while (!loose.empty()) {
        PlacedJobPtr to_squeeze = loose.top();
        loose.pop();

        const std::size_t min_gap_size = to_squeeze->descr->size;
        std::size_t offset_runner = 0;
        std::size_t smallest_gap = std::numeric_limits<std::size_t>::max();
        std::optional<std::size_t> best_offset;

        // Rust: ig.get(&id).unwrap().iter()
        //            .filter(|j| j.times_squeezed.get() == iters_done + 1)
        //            .sorted_unstable().rev().peekable()
        //
        // We materialize into a vector and sort ascending by offset.
        std::vector<PlacedJobPtr> jobs_vec;
        {
            const PlacedJobSet& bucket = ig.at(to_squeeze->descr->id);
            for (const auto& j : bucket) {
                if (j->times_squeezed == iters_done + 1) {
                    jobs_vec.push_back(j);
                }
            }
        }
        std::sort(jobs_vec.begin(), jobs_vec.end(),
            [](const PlacedJobPtr& a, const PlacedJobPtr& b) {
                return a->offset < b->offset;
            });

        std::size_t idx = 0;
        // Rust: while let Some(next_job) = jobs_vec.peek()
        while (idx < jobs_vec.size()) {
            const PlacedJobPtr& next_job = jobs_vec[idx];
            const std::size_t njo = next_job->offset;

            if (njo > offset_runner) {
                const std::size_t test_off =
                    to_squeeze->get_corrected_offset(start_addr, offset_runner);

                if (njo > test_off && njo - test_off >= min_gap_size) {
                    if (!first_fit) {
                        const std::size_t gap = njo - test_off;
                        if (gap < smallest_gap) {
                            smallest_gap = gap;
                            best_offset = test_off;
                        }
                    } else {
                        best_offset = test_off;
                        break;
                    }
                }
                offset_runner = std::max(test_off, next_job->next_avail_offset());
            } else {
                offset_runner = std::max(offset_runner,
                                         next_job->next_avail_offset());
            }
            ++idx;
        }

        // Rust: if let Some(o) = best_offset { set o } else { set offset_runner }
        if (best_offset) {
            to_squeeze->offset = *best_offset;
        } else {
            to_squeeze->offset = offset_runner;
        }
        to_squeeze->times_squeezed = iters_done + 1;

        const std::size_t cand_makespan = to_squeeze->next_avail_offset();
        if (cand_makespan > max_address) {
            max_address = cand_makespan;
            if (max_address > makespan_lim) {
                return std::numeric_limits<std::size_t>::max();
            }
        }
    }

    return max_address;
}