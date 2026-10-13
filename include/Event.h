#ifndef EVENT_H
#define EVENT_H

#include <queue>
#include "Job.h"

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

    bool operator>(const Event& other) const {
        return other < *this;
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

#endif // EVENT_H