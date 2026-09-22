#ifndef INSTANCE_H
#define INSTANCE_H

#include <limits>
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

    std::pair<std::size_t, std::size_t> min_max_height();
private:
    JobSet jobs;
    Info info;
};

#endif // INSTANCE_H