#ifndef JOB_H
#define JOB_H

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class Job;

using JobSet = std::vector<std::shared_ptr<Job>>;

class Job {
public:
    std::size_t sz;
    std::size_t birth;
    std::size_t death;
    std::size_t req_size;
    std::optional<std::size_t> alignment;
    
    std::optional<JobSet> contents;
    uint32_t originals_boxed;
    uint32_t id;

    Job() = default;
};

#endif // JOB_H