#ifndef JOB_H
#define JOB_H

#include <compare>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>


class Job;

using JobSet = std::vector<std::shared_ptr<Job>>;

class Job {
public:
    std::size_t size;
    std::size_t birth;
    std::size_t death;
    std::size_t req_size;
    std::optional<std::size_t> alignment;
    
    std::optional<JobSet> contents;
    uint32_t originals_boxed;
    uint32_t id;

    Job() = default;


    bool overlaps_with(const Job &other);
    uint32_t get_id();
    std::size_t get_req_size();
    std::optional<std::size_t> get_alignment();

    static Job new_box(JobSet contents, std::size_t height);

    bool is_live_at(std::size_t t);
    bool lives_within(std::pair<std::size_t, std::size_t> space);
    bool is_original();
    bool dies_before(std::size_t t);
    bool born_after(std::size_t t);
    
    std::size_t lifetime();
    std::size_t area();

    std::partial_ordering operator<=>(const Job& other) const {
        return birth <=> other.birth;
    }

    bool operator==(const Job& other) const {
        return id == other.id;
    }

    friend std::ostream& operator<<(std::ostream& os, const Job& j) {
        return os << "Job {id: " << j.id <<
                     ", birth: " << j.birth <<
                     ", size: " << j.size << "}";
    }
};

namespace std {
    template <>
    struct hash<Job> {
        std::size_t operator()(const Job& j) const noexcept {
            return std::hash<std::size_t>{}(j.id);
        }
    };
}

#endif // JOB_H