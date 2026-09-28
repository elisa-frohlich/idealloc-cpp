#include <algorithm>
#include <cassert>
#include <cmath>
#include <iterator>
#include <unordered_map>
#include "JobSet.h"

namespace jobset {
/// Initializes a JobSet with a given set of jobs.
/// A successfully returned JobSet is guaranteed to be
/// compliant with all of `idealloc`'s assumptions. These are:
/// - no job has zero size
/// - all deaths are bigger than all births
/// - no job has bad alignment (zero, or alloc. size not multiple of i)
/// - all jobs are original
/// - allocated job size is equal or greater to the requested one
///
/// This function is the gatekeeper to the rest of the library.
JobSet init(std::vector<Job> in_elts) {
    for (int idx = 0; idx < in_elts.size(); idx++) {
        Job j = in_elts[idx];
        if (j.size == 0) {
            throw JobError("Job with 0 size found!", j);
        } else if (j.birth >= j.death) {
            throw JobError("Job with birth >= death found!", j);
        } else if (j.alignment.has_value()) {
            if (j.alignment.value() == 0) {
                throw JobError("Job with 0 alignment found!", j);
            }
        } else if (!j.is_original()) {
            throw JobError("Unoriginal job found! (non-empty contents)", j);
        } else if (j.originals_boxed != 0) {
            throw JobError("Unoriginal job found! (non-zero originals boxed)", j);
        } else if (j.size < j.req_size) {
            throw JobError("Job with req > alloc size found!", j);
        }
    }

    JobSet result;
    result.reserve(in_elts.size());
    std::transform(
        std::make_move_iterator(in_elts.begin()),
        std::make_move_iterator(in_elts.end()),
        std::back_inserter(result),
        [](Job&& j) { return std::make_shared<Job>(std::move(j));}
    );
    return result;
}

/// Forms Theorem 2's R_i groups. 
std::vector<JobSet> split_ris(JobSet jobs, std::span<const std::size_t> pts) {
    std::vector<JobSet> res;
    // The algorithm recursively splits around ceil((q/2)), where
    // q = pts.size() - 2. The minimum value for the ceiling function
    // is 1. Thus the length of the points must be at least 3.
    if (pts.size() >= 3) {
        std::size_t q = pts.size() - 2;
        std::size_t idx_mid = (std::size_t)std::ceil((q/2.0));
        // The fact that we need to index within `pts` is why we're
        // passing a slice instead of the original `std::set`.
        std::size_t t_mid = pts[idx_mid];
        JobSet live_at;
        JobSet die_before;
        JobSet born_after;
        for (auto j : jobs) {
            if (j->is_live_at(t_mid)) live_at.emplace_back(j);
            else if (j->dies_before(t_mid)) die_before.emplace_back(j);
            else if (j->born_after(t_mid)) born_after.emplace_back(j);
            else throw std::runtime_error("Unreachable!");
        }
        res.emplace_back(std::move(live_at));
        if (!die_before.empty()) {
            auto tmp = split_ris(die_before, pts.first(idx_mid));
            res.insert(res.end(),
                std::make_move_iterator(tmp.begin()),
                std::make_move_iterator(tmp.end()));
        }
        if (!born_after.empty()) {
            auto tmp = split_ris(born_after, pts.subspan(idx_mid+1));
            res.insert(res.end(),
                std::make_move_iterator(tmp.begin()),
                std::make_move_iterator(tmp.end()));
        }
    } else {
        res.emplace_back(std::move(jobs));
    }
    return res;
}

std::size_t get_max_size(JobSet& jobs) {
    std::size_t max_size = 0;
    for (auto j : jobs) {
        max_size = std::max(max_size, j->size);
    }
    return max_size;
}

std::size_t get_load(JobSet& jobs) {
    std::size_t running = 0, max = 0;

    // The `evts` variable is a min-priority queue on the
    // births and deaths of the jobs. Deaths have priority
    // over births. By popping again and again, we have
    // our "traversal" from left to right.
    Events evts = get_events(jobs);

    while (!evts.empty()) {
        Event evt = evts.top();
        evts.pop();

        if (evt.evt_t == EventKind::Birth) {
            running += evt.job->size;
            if (running > max) {
                max = running;
            }
        } else { // EventKind::Death
            if (running < evt.job->size) {
                throw std::runtime_error("Almost overflowed load!");
            }
            
            running = running - evt.job->size;
        }
    }
    return max;
}

std::uint32_t get_total_originals_boxed(JobSet& jobs) {
    std::uint32_t total = 0;
    for (auto j : jobs) {
        total += j->originals_boxed;
    }
    return total;
}

/// Self-explanatory. Each JobSet of the returned vector
/// is an IGC row
std::vector<JobSet> interval_graph_coloring(JobSet jobs) {
    std::vector<JobSet> res;
    // This is our inventory of free rows. We'll be pulling
    // space from here (lowest first), and adding higher rows
    // along the way whenever we run out.
    std::set<std::size_t> free_rows{0};
    // The highest spawned row.
    std::size_t max_row = 0;
    // A mapping from job IDs to row nums.
    std::unordered_map<std::uint32_t, std::size_t> cheatsheet;

    // Traverse jobs...
    Events evts = get_events(jobs);
    while (!evts.empty()) {
        Event evt = evts.top();
        evts.pop();
        if (evt.evt_t == EventKind::Birth) {
            // Get the lowest free row.
            std::size_t row_to_fill = *free_rows.erase(free_rows.begin());
            // Update map.
            cheatsheet.emplace(evt.job->id, row_to_fill);
            
            if (row_to_fill < res.size()) {
                res[row_to_fill].emplace_back(std::move(evt.job));
            } else {
                assert(row_to_fill == res.size() && "Bad IGC impl!");
                res.emplace_back();
                res.back().emplace_back(std::move(evt.job));
            }

            if (free_rows.empty()) {
                // No free space! Add one more row to the top.
                free_rows.emplace(++max_row);
            }
        } else { // EventKind::Death
            std::size_t row_to_vacate = cheatsheet[evt.job->id];
            cheatsheet.erase(evt.job->id);
            free_rows.emplace(row_to_vacate);
        }
    }

    return res;
}

Events get_events(JobSet& jobs) {
    Events res;
    for (auto j : jobs) {
        res.emplace(j, EventKind::Birth, j->birth);
        res.emplace(j, EventKind::Death, j->death);
    }

    return res;
}

/// Finds gaps in between jobs of an IGC row, and adds
/// their endpoints to an ordered set, eventually returned.
/// 
/// Used in the context of Theorem 2.
std::set<std::size_t> gap_finder(JobSet& row_jobs, std::size_t alpha, std::size_t omega) {
    std::set<std::size_t> res;
    // Again we use event traversal. Row jobs are already sorted
    // since IGC itself is a product of event traversal.
    Events evts = get_events(row_jobs);
    // We either have found the next gap's start, or we haven't.
    // Initialize it optimistically to the left extreme of our
    // horizon.
    std::optional<std::size_t> gap_start = alpha;

    while (!evts.empty()) {
        Event evt = evts.top();
        evts.pop();

        if (evt.evt_t == EventKind::Birth) {
            if (gap_start.has_value()) {
                std::size_t v = gap_start.value();
                if (v < evt.time) {
                    res.emplace(v);
                    res.emplace(evt.time);
                }
                gap_start = std::nullopt;
            }
        } else { // EventKind::Death
            gap_start = evt.time;
        }
    }
    std::size_t last_gap_start = gap_start.value();
    if (last_gap_start < omega) res.emplace(last_gap_start);

    return res;
}

} // namespace jobset