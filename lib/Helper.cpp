#include <cassert>
#include <charconv>
#include <fstream>
#include <memory>
#include <random>
#include <queue>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include "Helper.h"

std::vector<Job> MinimallocCSVParser::read_jobs(std::size_t shift) const {
    std::vector<Job> res;
    std::array<std::size_t, 4> data_buf{};

    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open file: " + path.string());

    std::string line;
    if (!std::getline(in, line)) return res;   // skip header

    while (std::getline(in, line)) {
        std::size_t idx = 0, start = 0;
        while (true) {
            const auto comma = line.find(',', start);
            const auto field = (comma == std::string::npos)
                ? line.substr(start)
                : line.substr(start, comma - start);

            std::size_t val = 0;
            auto [ptr, ec] = std::from_chars(
                field.data(), field.data() + field.size(), val);
            if (ec != std::errc{} || ptr != field.data() + field.size())
                throw std::runtime_error("Error while parsing CSV.");

            data_buf.at(idx) = val;
            ++idx;

            if (comma == std::string::npos) break;
            start = comma + 1;
        }

        res.push_back(gen_single(data_buf,
                                 static_cast<std::uint32_t>(data_buf[0])));
    }

    return res;
}

Job MinimallocCSVParser::gen_single(
    const std::array<std::size_t, 4>& d,
    std::uint32_t id) const
{
    return Job{
        .size            = d[3],
        .birth           = d[1],
        .death           = d[2],
        .req_size        = d[3],
        .alignment       = std::nullopt,
        .contents        = std::nullopt,
        .originals_boxed = 0,
        .id              = id,
    };
}

// Generated with Deepseek
// Must review it later
std::vector<Job> IREECSVParser::read_jobs(std::size_t shift) const {
    // Rust: let helper = MinimalloCSVParser::new(self.dirty.clone());
    MinimallocCSVParser helper(dirty);

    // Rust: let increment = (shift + 1) % 2;
    const std::size_t increment = (shift + 1) % 2;

    // Rust: let dirty_jobs: JobSet = helper.read_jobs(0).unwrap()...
    // `.unwrap()` -- we don't catch, so the exception propagates.
    std::vector<Job> raw_jobs = helper.read_jobs(0);

    JobSet dirty_jobs;
    dirty_jobs.reserve(raw_jobs.size());
    for (Job& j : raw_jobs) {
        // Rust: if shift == 1 { j } else { Job { ..., death: j.death + increment, req_size: j.size, ... } }
        if (shift != 1) {
            j.death    += increment;
            j.req_size  = j.size;
        }
        dirty_jobs.push_back(std::make_shared<Job>(std::move(j)));
    }

    // Rust: let mut evts = get_events(&dirty_jobs);
    Events evts = jobset::get_events(dirty_jobs);

    // Rust state variables
    std::size_t num_generations = 0;
    std::optional<std::size_t> deadline;
    bool last_was_birth = true;
    Events upcoming_deaths;
    std::vector<Job> res;

    // Rust: while let Some(e) = evts.pop()
    while (!evts.empty()) {
        Event e = evts.top();
        evts.pop();

        if (e.evt_t == EventKind::Birth) {
            // Rust: if let Some(d) = deadline { if d + increment == e.time && !last_was_birth { num_generations += 1; } }
            if (deadline && *deadline + increment == e.time && !last_was_birth) {
                ++num_generations;
            }

            // Rust: upcoming_deaths.push(Event { job: e.job.clone(), evt_t: Death, time: e.job.death - increment });
            upcoming_deaths.push(Event{
                .job   = e.job,                            // copy shared_ptr == Arc::clone
                .evt_t = EventKind::Death,
                .time  = e.job->death - increment
            });

            if (!last_was_birth) {
                last_was_birth = true;
            }

            // Rust: res.push(Job { ... })
            Job new_job {
                .size            = e.job->size,
                .birth           = e.job->birth + num_generations,
                .death           = e.job->death + num_generations + increment,
                .req_size        = e.job->size,
                .alignment       = std::nullopt,
                .contents        = std::nullopt,
                .originals_boxed = 0,
                .id              = e.job->id,
            };
            // alignment, contents, originals_boxed keep their defaults
            res.push_back(std::move(new_job));

        } else {  // EventKind::Death
            // Rust: assert!(...)
            if (upcoming_deaths.empty()) {
                throw std::logic_error("IREECSVParser: upcoming_deaths underflow");
            }
            if (upcoming_deaths.top().time != e.job->death - increment) {
                throw std::logic_error("IREECSVParser: deadline mismatch");
            }

            deadline = upcoming_deaths.top().time;
            upcoming_deaths.pop();

            if (last_was_birth) {
                last_was_birth = false;
            }
        }
    }

    return res;
}


Job IREECSVParser::gen_single(Job d, std::uint32_t id) const {
    return d;
}


T2Control::T2Control(Instance& jobs)
    : bounding_interval(jobs.get_horizon())
{
    const auto [start, end] = bounding_interval;

    // Rust: debug_assert!(start < end, "...")
    // debug_assert! is compiled out in release; C++ assert() matches that
    // (it's compiled out when NDEBUG is defined).
    assert(start < end && "Same-ends horizon met.");

    const std::size_t mid = gen_crit(jobs, start, end);

    // Rust: BTreeSet::from([start, end, mid])
    critical_points = { start, end, mid };
}

std::size_t T2Control::gen_crit(Instance& jobs,
                                std::size_t left,
                                std::size_t right) {
    // Rust: debug_assert!(left + 1 < right, "Bad range found.");
    assert(left + 1 < right && "Bad range found.");

    std::vector<std::size_t> pts;

    // Rust: let mut evts = get_events(&jobs.jobs); while let Some(evt) = evts.pop()
    Events evts = jobset::get_events(jobs.jobs);
    while (!evts.empty()) {
        Event evt = evts.top();
        evts.pop();

        // Rust: match evt.evt_t { Birth => time + 1, Death => time - 1 }
        const std::size_t cand = (evt.evt_t == EventKind::Birth)
            ? evt.time + 1
            : evt.time - 1;

        if (cand > left && cand < right) {
            pts.push_back(cand);
        }
    }

    // Rust: pts[thread_rng().gen_range(0..pts.len())]
    // gen_range(0..n) is low-inclusive, upper-exclusive => [0, n).
    // C++ uniform_int_distribution is inclusive on both ends => (0, n-1).
    // Both produce the same set of integers.
    if (pts.empty()) {
        // Rust's gen_range on an empty range panics with "cannot sample
        // empty range". Match that behavior.
        throw std::runtime_error("gen_crit: cannot sample empty range");
    }

    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<std::size_t> dist(0, pts.size() - 1);
    return pts[dist(rng)];
}