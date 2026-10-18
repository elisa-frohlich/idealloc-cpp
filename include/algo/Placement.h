#ifndef ALGO_PLACEMENT_H
#define ALGO_PLACEMENT_H

#include "Jobplacement.h"

LoosePlacement get_loose_placement(
    JobSet jobs,
    std::size_t start_offset,
    UnboxCtrl control_state,
    const PlacedJobRegistry& ig,
    std::uint32_t dumb_id);

std::size_t do_best_fit(
    LoosePlacement loose,           // by value: Rust takes it by value
    const InterferenceGraph& ig,
    std::uint32_t iters_done,
    std::size_t makespan_lim,
    bool first_fit,
    std::size_t start_addr);

#endif //ALGO_PLACEMENT_H