#include <algorithm>
#include <cassert>
#include <cmath>
#include "Instance.h"
#include "JobSet.h"

Info Info::merge(const Instance &_this, const Instance &_that) {
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

/// Splits instance to unit-height buckets, in the
/// context of Corollary 15. Each bucket is indexed
/// by the height to be given to Theorem 2.
std::unordered_map<std::size_t, Instance>
    Instance::make_buckets(std::shared_ptr<Instance> source, double epsilon)
{
    std::unordered_map<std::size_t, Instance> res;
    double prev_floor = 1.0 / (1.0 + epsilon);
    int i = 0;
    while (source->jobs.size() > 0) {
        double h = std::pow((1 + epsilon), i);
        if (std::ranges::any_of(source->jobs, [&](const auto& j) {
            return static_cast<double>(j->size) > prev_floor
                && static_cast<double>(j->size) <= h;
        })) {
            std::size_t h_split = static_cast<std::size_t>(h);
            auto [toward_bucket, rem] = source->split_by_height(h_split);
            res.emplace(h_split, toward_bucket);
            source = std::make_shared<Instance>(rem);
        }
        prev_floor = h;
        i++;
    }

    return res;
}

bool Instance::check_boxed_originals(std::uint32_t target) {
    return target == total_originals_boxed();
}

/// Checks an Instance a candidate ε-value and returns:
///     (i)     its max/min height ratio, `r`
///     (ii)    the implied `μ` = ε / (log`r`)^2
///     (iii)   the box size with which Corollary 15 would be called
///     (iv)    whether it's safe to mimic Theorem 16
std::tuple<double, double, double, bool> Instance::get_safety_info(double epsilon) {
    auto [h_min, h_max] = min_max_height();
    auto [x_1, _1, _2, lg2r] = ctrl_prelude();
    double mu = epsilon / lg2r;
    double h = std::ceil(std::pow(mu,5) * (static_cast<double>(h_max) / lg2r));
    double target_size = std::floor((mu * h));

    return {static_cast<double>(h_max)/static_cast<double>(h_min),
            mu,
            h,
            mu < x_1 && target_size >= static_cast<double>(h_min)};
}

std::pair<std::size_t, std::size_t> Instance::get_horizon() {
    std::pair<std::size_t, std::size_t> res = {std::numeric_limits<std::size_t>::max(), 0};
    for (auto j : jobs) {
        res.first = std::min(res.first, j->birth);
        res.second = std::max(res.second, j->death);
    }
    return res;
}

std::pair<std::size_t, std::size_t> Instance::min_max_height() const {
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

/// Splits an Instance into two new instances, the first
/// containing jobs of true size up to `ceil`.
std::pair<Instance, Instance> Instance::split_by_height(std::size_t ceil_) {
    std::size_t to_split = jobs.size();

    JobSet small, high;
    std::ranges::partition_copy(
        jobs,
        std::back_inserter(small),
        std::back_inserter(high),
        [ceil_](const std::shared_ptr<Job>& j) { return j->size < ceil_; }
    );

    assert(small.size() + high.size() == to_split);

    return {Instance{small}, Instance{high}};
}

/// Splits an Instance into multiple new instances, the first
/// containing jobs that are live in at least one moment of those
/// in `pts`.
std::pair<JobSet, std::unordered_map<std::size_t, Instance>>
Instance::split_by_liveness(Instance self, std::set<std::size_t>& pts)
{
    std::unordered_map<std::size_t, Instance> x_is_base;
    JobSet live;
    
    std::sort(self.jobs.begin(), self.jobs.end(),
            [](const std::shared_ptr<Job>& a, const std::shared_ptr<Job>& b) {
                return *a < *b;
            });
    
    std::size_t idx = 0;

    std::size_t q = 0;
    for (auto it = pts.begin(); it != pts.end(); ++it, ++q) {
        const std::size_t t_q = *it;
        auto next_it = std::next(it);
    }

    return {live, x_is_base};
}

/// Counts how many of the *ORIGINAL* buffers have
/// been boxed somewhere into the instance.
std::uint32_t Instance::total_originals_boxed() {
    return jobset::get_total_originals_boxed(jobs);
}

/// Merges `self` with another [Instance].
std::shared_ptr<Instance> Instance::merge_with(Instance other) const {
    std::size_t to_join = jobs.size() + other.jobs.size();
    Info new_info = Info::merge(*this, other);

    JobSet all;
    all.reserve(to_join);
    all.insert(all.end(), jobs.begin(), jobs.end());
    all.insert(all.end(),
        std::make_move_iterator(other.jobs.begin()),
        std::make_move_iterator(other.jobs.end()));

    assert(all.size() == to_join);

    return std::make_shared<Instance>(std::move(all), std::move(new_info));
}


/// Does the same as `Instance::merge_with`, but without consuming
/// `this`. Used in the context of consolidating `Mutex`-protected results.
void Instance::merge_via_ref(Instance other) {
    std::size_t to_join = jobs.size() + other.jobs.size();

    jobs.reserve(to_join);
    jobs.insert(jobs.end(), other.jobs.begin(), other.jobs.end());

    info = Info::merge(*this, other);

    assert(jobs.size() == to_join);
}

std::tuple<double, double, double, double> Instance::ctrl_prelude() {
    auto [h_min, h_max] = min_max_height();
    double r = static_cast<double>(h_min) / static_cast<double>(h_max);
    double lgr = log2(r);
    double lg2r = pow(r, 2);
    double small_end = pow((pow(lg2r, 7) / r), 1.0/6.0);
    double mu_lim = (sqrt(5.0) - 1.0) / 2.0;

    return {mu_lim, small_end, mu_lim * lg2r, lg2r};
}