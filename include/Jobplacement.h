#ifndef JOBPLACEMENT_H
#define JOBPLACEMENT_H

#include <queue>
#include <unordered_map>
#include <variant>
#include "Job.h"

/// A [Job] which has been assigned an offset in
/// some contiguous address space.
class PlacedJob {
public:
    // Rust: pub descr: Arc<Job>
    std::shared_ptr<Job> descr;

    // Rust: pub offset: Cell<ByteSteps>
    // Cell<T> is "mutate through shared reference" -- in C++ that's `mutable`.
    // (Every method here only reads, so making it mutable lets them be const,
    //  matching the Rust `&self` methods.)
    mutable std::size_t offset = 0;

    // Rust: pub times_squeezed: Cell<u32>
    mutable std::uint32_t times_squeezed = 0;

    // Rust: PlacedJob::new(descr)
    explicit PlacedJob(std::shared_ptr<Job> d)
        : descr(std::move(d)) {}

    // Rust: overlaps_with
    bool overlaps_with(const PlacedJob& other) const {
        return descr->birth < other.descr->death
            && other.descr->birth < descr->death;
    }

    // Rust: next_avail_offset
    std::size_t next_avail_offset() const {
        return offset + descr->size;
    }

    // Rust: get_corrected_offset
    std::size_t get_corrected_offset(std::size_t start_addr,
                                     std::size_t cand) const {
        if (descr->alignment) {
            const std::size_t a = *descr->alignment;
            const std::size_t cand_addr = start_addr + cand;
            if (cand_addr == 0 || cand_addr % a == 0) return cand;
            if (cand_addr < a) return a - start_addr;
            return (cand_addr / a + 1) * a - start_addr;
        }
        return cand;
    }

    // Rust: PartialEq -- compares the underlying Jobs by their Eq (i.e. by id)
    bool operator==(const PlacedJob& other) const {
        return *descr == *other.descr;
    }

    // Rust: Ord -- other.offset.cmp(&self.offset) -- REVERSED
    // C++ translation: define a *natural* < on offset, and use std::greater
    // at the priority_queue. Both interpretations agree on "smaller offset pops first".
    bool operator<(const PlacedJob& other) const {
        return offset < other.offset;
    }

    friend std::ostream& operator<<(std::ostream& os, const PlacedJob& pj) {
        return os << "PlacedJob{id=" << pj.descr->id
                << ", offset=" << pj.offset
                << ", times_squeezed=" << pj.times_squeezed
                << "}";
    }
};

namespace std {
    template <>
    struct hash<PlacedJob> {
        std::size_t operator()(const PlacedJob& pj) const noexcept {
            return std::hash<Job>{}(*pj.descr);
        }
    };
}

// Rust: pub type PlacedJobSet = Vec<Rc<PlacedJob>>;
using PlacedJobPtr = std::shared_ptr<PlacedJob>;
using PlacedJobSet = std::vector<PlacedJobPtr>;

// Rust: pub type InterferenceGraph = HashMap<u32, PlacedJobSet>;
using InterferenceGraph = std::unordered_map<std::uint32_t, PlacedJobSet>;

// Rust: pub type PlacedJobRegistry = HashMap<u32, Rc<PlacedJob>>;
using PlacedJobRegistry = std::unordered_map<std::uint32_t, PlacedJobPtr>;

// Comparator: orders shared_ptr<PlacedJob> by the *pointed-to* offset,
// reversed relative to <. "greater" in the priority_queue sense = smaller offset.
struct PlacedJobPtrGreater {
    bool operator()(const PlacedJobPtr& a, const PlacedJobPtr& b) const {
        return *b < *a;   // *a has greater offset than *b
    }
};

using LoosePlacement =
    std::priority_queue<PlacedJobPtr, std::vector<PlacedJobPtr>, PlacedJobPtrGreater>;

namespace unbox_ctrl {
    struct SameSizes { std::size_t size; };
    struct NonOverlapping {};
    struct Unknown {};
} // namespace unbox_ctrl

using UnboxCtrl = std::variant<
    unbox_ctrl::SameSizes,
    unbox_ctrl::NonOverlapping,
    unbox_ctrl::Unknown
>;

#endif //JOBPLACEMENT_H