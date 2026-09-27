#include "Instance.h"

Info Info::merge(Instance &_this, Instance &_that) {
    Info res = {std::nullopt, std::nullopt};

    auto [this_min, this_max] = _this.min_max_height();
    auto [that_min, that_max] = _that.min_max_height();

    res.min_max_height = {std::min(this_min, that_min), std::max(this_max, that_max)};
    return res;
}

void Info::set_load(std::size_t l) {
    load = l;
}

void Info::set_heights(std::size_t min, std::size_t max) {
    min_max_height = std::make_pair(min,max);
}



std::pair<std::size_t, std::size_t> Instance::min_max_height() {
    if (this->info.min_max_height.has_value()) {
        return this->info.min_max_height.value();
    }

    std::size_t min = std::numeric_limits<std::size_t>::max();
    std::size_t max = std::numeric_limits<std::size_t>::min();
    for (auto j : jobs) {
        std::size_t curr = j->size;
        if (curr < min) min = curr;
        if (curr > max) max = curr;
    }
    return std::make_pair(min, max);
}