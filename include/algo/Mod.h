#ifndef ALGO_MOD_H
#define ALGO_MOD_H

#include "algo/Boxing.h"
#include "algo/Placement.h"

std::pair<PlacedJobRegistry, std::size_t>
idealloc(JobSet original_input,
         double worst_case_frag,
         std::size_t start_address,
         std::uint32_t max_lives);

#endif // ALGO_MOD_H