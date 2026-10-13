#include <cmath>
#include <iostream>
#include "Helper.h"
#include "Instance.h"
#include "Job.h"

int main() {
    // JobSet jobs = read_from_path<IREECSVParser>("/home/elisa/Codes/idealloc-cpp/benchmarks/linux/lldb0.csv", 1);
    JobSet jobs = read_from_path<IREECSVParser>("/home/elisa/Codes/idealloc-cpp/benchmarks/minimalloc/A.1048576.csv", 0);
    for (auto job : jobs) {
        std::cout << *job << "\n";
    }
    return 0;
}