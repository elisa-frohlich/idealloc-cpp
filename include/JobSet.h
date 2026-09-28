#ifndef JOBSET_H
#define JOBSET_H

#include <set>
#include <span>
#include "Helper.h"
#include "Job.h"

namespace jobset {
    JobSet init(std::vector<Job> in_elts);
    
    std::vector<JobSet> split_ris(JobSet jobs, std::span<const std::size_t> pts);

    std::size_t get_max_size(JobSet& jobs);

    std::size_t get_load(JobSet& jobs);

    std::uint32_t get_total_originals_boxed(JobSet& jobs);

    std::vector<JobSet> interval_graph_coloring(JobSet jobs);

    Events get_events(JobSet& jobs);

    std::set<std::size_t> gap_finder(JobSet& row_jobs, std::size_t alpha, std::size_t omega);
}


#endif // JOBSET_H