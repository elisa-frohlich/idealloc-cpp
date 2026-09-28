#ifndef INSTANCE_H
#define INSTANCE_H

#include <limits>
#include <set>
#include <unordered_map>
#include <utility>
#include "Job.h"

class Instance;

class Info {
    friend class Instance;
public:
    Info() = default;
    Info(
        std::optional<std::size_t> load,
        std::optional<std::pair<std::size_t, std::size_t>> min_max_height
    ) : load(load), min_max_height(min_max_height) {}

    void set_load(std::size_t l);

    void set_heights(std::size_t min, std::size_t max);
private:
    // **CAUTION:** we mean the MAXIMUM load!
    mutable std::optional<std::size_t> load;
    mutable std::optional<std::pair<std::size_t, std::size_t>> min_max_height;

    static Info merge(Instance &_this, Instance &_that);
};

class Instance {
public:
    Instance() = default;
    Instance(JobSet jobs) : jobs(jobs) {}

    std::unordered_map<std::size_t, Instance>
    make_buckets(std::shared_ptr<Instance> source, double epsilon);

    bool check_boxed_originals(std::uint32_t target);

    std::tuple<double, double, double, bool> get_safety_info(double epsilon);

    std::pair<std::size_t, std::size_t> get_horizon();

    std::pair<std::size_t, std::size_t> min_max_height();

    std::pair<Instance, Instance> split_by_height(std::size_t ceil_);

    std::pair<JobSet, std::unordered_map<std::size_t, Instance>>
    split_by_liveness(std::set<std::size_t>& pts);

    std::uint32_t total_originals_boxed();

    std::shared_ptr<Instance> merge_with(Instance other);

    void merge_via_ref(Instance other);

    std::tuple<double, double, double, double> ctrl_prelude();
private:
    JobSet jobs;
    Info info;
};

#endif // INSTANCE_H