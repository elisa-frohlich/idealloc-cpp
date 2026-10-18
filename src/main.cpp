#include <cmath>
#include <iostream>
#include "Helper.h"
#include "Instance.h"
#include "Job.h"
#include "algo/Mod.h"

int main() {
    JobSet jobs = read_from_path<MinimallocCSVParser>("/home/elisa/Codes/idealloc-cpp/benchmarks/minimalloc/A.1048576.csv", 1);
    idealloc(jobs, 1, 0, 10000);
    
    return 0;
}