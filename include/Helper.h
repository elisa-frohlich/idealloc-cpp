#ifndef HELPER_H
#define HELPER_H

#include <queue>
#include <sstream>
#include <stdexcept>
#include "Job.h"
#include "Instance.h"

static std::string to_string(const Job& j) {
    std::ostringstream os;
    os << j;
    return os.str();
}

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

enum EventKind {
    Birth,
    Death
};

struct Event {
    std::shared_ptr<Job> job;
    EventKind evt_t;
    std::size_t time;

    
    bool operator<(const Event& other) const {
        if (time != other.time) {
            return time < other.time;
        }

        if (evt_t == other.evt_t) {
            if (evt_t == EventKind::Birth) {
                return job->death > other.job->death;
            }
            // Both are death events, so *this == other
            return false;
        }

        
        return evt_t == EventKind::Death;
    }

    bool operator<=(const Event& other) const {
        return *this < other || (*this == other && !(other < *this));
    }

    bool operator>(const Event& other) const {
        return other <= *this;
    }
    
    bool operator==(const Event& other) const {
        return time == other.time;
    }
};

/// Traversal of a JobSet can be thought as an ordered stream
/// of events, with increasing time of occurence. Each Job generates
/// two events, corresponding to the start/end of its lifetime
/// respectively.
/// 
/// We use these events to calculate things such as maximum load,
/// interference graphs, fragmentation, critical points, etc.
using Events = std::priority_queue<Event, std::vector<Event>, std::greater<Event>>;

#endif // HELPER_H