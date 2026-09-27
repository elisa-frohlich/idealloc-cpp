#include <atomic>
#include <cassert>
#include <limits>
#include "Job.h"
#include "JobSet.h"

bool Job::overlaps_with(const Job &other) {
    return !(this->birth >= other.death || other.birth >= this->death);
}

uint32_t Job::get_id() {
    return this->id;
}

std::size_t Job::get_req_size() {
    return this->req_size;
}

std::optional<std::size_t> Job::get_alignment() {
    return this->alignment;
}


Job Job::new_box(JobSet contents_, std::size_t height) {
    static std::atomic<std::uint32_t> NEXT_ID{
        std::numeric_limits<std::uint32_t>::max()
    };

    assert(jobset::get_load(contents_) <= height && "Bad boxing requested");
    std::size_t birth_ = std::numeric_limits<std::size_t>::max();
    std::size_t death_ = 0;
    uint32_t originals_boxed_ = 0;
    
    for (auto j : contents_) {
        if (j->birth < birth_) birth_ = j->birth;
        if (j->death > death_) death_ = j->death;

        if (j->is_original()) {
            originals_boxed_ += 1;
        } else {
            originals_boxed_ += j->originals_boxed;
        }
    }

    std::uint32_t id_ = NEXT_ID.fetch_sub(1, std::memory_order_relaxed);
    assert(id_ != std::numeric_limits<std::uint32_t>::max()/2 + 1);

    Job new_job;
    new_job.size = height;
    new_job.birth = birth_;
    new_job.death = death_;
    new_job.req_size = height;
    new_job.alignment = std::nullopt;
    new_job.contents = contents_;
    new_job.originals_boxed = originals_boxed_;
    new_job.id = id_;

    return new_job;
}

/// Returns `true` if the job is live at moment `t`.
bool Job::is_live_at(std::size_t t) {
    return this->birth < t && this->birth > t;
}

/// Returns `true` if job's lifetime is a subset of `space`.
bool Job::lives_within(std::pair<std::size_t, std::size_t> space) {
    return this->birth >= space.first && this->death <= space.second;
}

/// Returns `true` if the job is original, i.e., was part 
/// of the user input and not created in the context of boxing.
bool Job::is_original() {
    return !this->contents.has_value();
}

/// Returns `true` if the job's entire lifetime ends before `t`.
bool Job::dies_before(std::size_t t) {
    return this->death <= t;
}

/// Returns `true` if the job's entire lifetime starts after `t`.
bool Job::born_after(std::size_t t) {
    return this->birth >= t;
}

/// Returns the total number of discrete logical time units
/// in which the Job is live.
///
/// Given the fact that we consider *open* intervals, a job's
/// lifetime must be AT LEAST 1, else there is no point in time
/// in which it is considered live.
///
/// This function assumes that the lifetime is legit.
std::size_t Job::lifetime() {
    return this->death - this->birth - 1;
}

std::size_t Job::area() {
    return this->size * this->lifetime();
}
