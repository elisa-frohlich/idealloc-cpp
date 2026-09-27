#include <algorithm>
#include <iterator>
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

std::size_t get_load(JobSet& jobs) {
    std::size_t running = 0, max = 0;

    Events evts = get_events(jobs);

    while (!evts.empty()) {
        Event evt = evts.top();
        evts.pop();

        if (evt.evt_t == EventKind::Birth) {
            running += evt.job->size;
            if (running > max) {
                max = running;
            }
        } else {
            if (running < evt.job->size) {
                throw std::runtime_error("Almost overflowed load!");
            }
            
            running = running - evt.job->size;
        }
    }
    return max;
}

Events get_events(JobSet& jobs) {
    Events res;
    for (auto j : jobs) {
        res.emplace(j, EventKind::Birth, j->birth);
        res.emplace(j, EventKind::Death, j->death);
    }

    return res;
}

} // namespace jobset