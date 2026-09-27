#ifndef JOBSET_H
#define JOBSET_H

#include "Helper.h"
#include "Job.h"

namespace jobset {
    JobSet init(std::vector<Job> in_elts);
    std::size_t get_load(JobSet& jobs);
    Events get_events(JobSet& jobs);
}


#endif // JOBSET_H