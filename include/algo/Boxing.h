#ifndef ALGO_BOXING_H
#define ALGO_BOXING_H

#include "Helper.h"
#include <mutex>
#include <utility>

template <typename T>
struct SharedMutex {
    std::mutex mtx;
    T value;

    template <typename... Args>
    explicit SharedMutex(Args&&... args)
        : value(std::forward<Args>(args)...) {}
};

std::shared_ptr<Instance> rogue(std::shared_ptr<Instance> input, double epsilon);

Instance c_15(std::shared_ptr<Instance> input, double h, double epsilon);

Instance t_2(Instance input,
             std::size_t h,
             std::size_t h_real,
             double epsilon,
             std::optional<T2Control> ctrl);

std::pair<std::optional<JobSet>, JobSet>
lemma_1(JobSet input, std::size_t h, std::size_t h_real, double e);

#endif //ALGO_BOXING_H