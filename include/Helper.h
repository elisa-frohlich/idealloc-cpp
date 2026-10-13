#ifndef HELPER_H
#define HELPER_H

#include <array>
#include <filesystem>
// #include <queue>
#include <sstream>
#include <stdexcept>
#include <variant>
#include "Job.h"
#include "Jobset.h"
#include "Instance.h"

static std::string to_string(const Job& j) {
    std::ostringstream os;
    os << j;
    return os.str();
}

/// Defines the interface for reading jobs.
///
/// For example: we will write a type that implements [JobGen]
/// and reads a `minimalloc`-style CSV. We will write another
/// type that reads from a Linux-born `.trc` binary file.
///
/// The user can implement their own types as needed.
template <typename T>
class JobGen {
public:
    virtual ~JobGen() = default;

    virtual std::vector<Job> read_jobs(std::size_t shift) const = 0;
    virtual Job gen_single(T d, std::uint32_t id) const = 0;
};

/// Appears while constructing the JobSet of *original*
/// jobs to be dealt with.
class JobError : public std::runtime_error {
public:
    JobError(std::string message, Job culprit)
        : std::runtime_error(make_what(message, culprit))
        , message_(message)
        , culprit_(culprit)
    {}

    const std::string& message() const noexcept { return message_; }
    const Job& culprit() const noexcept { return culprit_; }
private:
    static std::string make_what(const std::string& message,
        const Job& culprit) {
            return message + "\n" + to_string(culprit);  
    }

    std::string message_;
    Job culprit_;
};

//---START EXTERNAL INTERFACES
// The types listed below implement interfaces to several
// data sources for `idealloc`.
//
// To write your own interface, simply make sure that it
// satisfies the `JobGen` trait.

/// We adopt [`minimalloc`'s CSV](https://github.com/google/minimalloc)
/// as the most standard format.
class MinimallocCSVParser : JobGen<const std::array<std::size_t, 4>&> {
public:
    std::filesystem::path path;

    explicit MinimallocCSVParser(std::filesystem::path p) : path(std::move(p)) {}

    std::vector<Job> read_jobs(std::size_t shift) const override;
    Job gen_single(const std::array<std::size_t, 4>& d, std::uint32_t id) const override;
};

/// A helper type for parsing IREE-born buffers stored into
/// the standard minimalloc CSV format.
/// 
/// We introduce this additional type because IREE adopts
/// *inclusive* semantics on both ends of a buffer's lifetime.
/// Thus conversion is needed.
class IREECSVParser : JobGen<Job> {
public:
    std::filesystem::path dirty;

    explicit IREECSVParser(std::filesystem::path p) : dirty(std::move(p)) {}

    std::vector<Job> read_jobs(std::size_t shift) const override;
    Job gen_single(Job d, std::uint32_t id) const override;
};
//---END EXTERNAL INTERFACES

//---START PLACEMENT PRIMITIVES
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

//---END PLACEMENT PRIMITIVES

struct BACtrl {
    std::shared_ptr<Instance> input;
    std::shared_ptr<Instance> pre_boxed;
    double epsilon = 0.0;
    std::size_t to_box = 0;
    std::size_t real_load = 0;

    // Rust: Option<Arc<Job>>
    // C++: shared_ptr already has a null state, so Option is redundant here.
    // If you want to distinguish "unset" from "explicitly null", use
    // std::optional<std::shared_ptr<Job>> instead.
    std::shared_ptr<Job> dummy;

    InterferenceGraph ig;
    PlacedJobRegistry reg;
    double mu_lim = 0.0;
    std::size_t best_opt = 0;

    // Rust: (f64, f64, f64) -- three elements, so tuple, not pair.
    std::tuple<double, double, double> hardness;
};

namespace analysis_result {
    struct NoOverlap {
        JobSet jobs;
    };
    
    struct SameSizes {
        JobSet jobs;
        InterferenceGraph ig;
        PlacedJobRegistry reg;
    };
    
    struct NeedsBA {
        BACtrl ctrl;
    };
} // namespace analysis_result

using AnalysisResult = std::variant<
    analysis_result::NoOverlap,
    analysis_result::SameSizes,
    analysis_result::NeedsBA
>;

struct T2Control {
    std::pair<std::size_t, std::size_t> bounding_interval;
    std::set<std::size_t> critical_points;

    explicit T2Control(Instance& jobs);

    // Rust: pub fn gen_crit(jobs: &Instance, left: ByteSteps, right: ByteSteps) -> ByteSteps
    static std::size_t gen_crit(Instance& jobs,
                                std::size_t left,
                                std::size_t right);
};



template <typename T>
JobSet read_from_path(std::filesystem::path file_path, std::size_t shift) {
    T parser(std::move(file_path));                  // Rust: T::new(file_path)
    std::vector<Job> jobs = parser.read_jobs(shift); // Rust: parser.read_jobs(shift)?
    if (jobs.empty()) {
        throw std::runtime_error("read_from_path: no jobs were read");
    }
    return jobset::init(std::move(jobs));
}

#endif // HELPER_H