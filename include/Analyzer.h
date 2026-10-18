#ifndef ANALYZER_H
#define ANALYZER_H

#include "Helper.h"

AnalysisResult prelude_analysis(JobSet jobs, std::size_t start_addr);

std::pair<double, std::shared_ptr<Instance>>
init_rogue(std::shared_ptr<Instance> input, double small, double big);

bool placement_is_valid(
    const std::pair<InterferenceGraph, PlacedJobRegistry>& ig_reg);

#endif // ANALYZER_H