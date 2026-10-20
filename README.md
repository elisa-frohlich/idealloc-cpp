# A C++ Implementation of Idealloc

This repository contains a C++ implementation of [idealloc](https://github.com/cappadokes/idealloc), a memory allocation algorithm described in the paper [Futureproof Static Memory Planning](https://arxiv.org/abs/2504.04874), by Christos Lamprakos, Panagiotis Xanthopoulos, Manolis Katsaragakis, Sotirios Xydis, and Francky Catthoor. Lamprakos' original implementation was written in rust.

# Dynamic Storage Allocation

[Dynamic Storage Allocation](https://www.cs.jhu.edu/~huang/cs318/fall21/readings/wilson.pdf) (DSA) is the problem of assigning memory regions to a set of buffers with known sizes and lifetimes. Buffers with overlapping lifetimes must occupy disjoint memory regions, while buffers with non-overlapping lifetimes may reuse the same memory. The objective is to minimize the total memory required to accommodate all buffers.

# Idealloc

[idealloc](https://github.com/cappadokes/idealloc) is a static memory planning algorithm based on the [Boxing Algorithm](https://dl.acm.org/doi/10.1145/780542.780624), by Adam L. Buchsbaum, Howard Karloff, Claire Kenyon, Nick Reingold, and Mikkel Thorup. Idealloc constructs a memory layout that respects buffer lifetimes while seeking to minimize memory consumption. Its distinctive feature is a stochastic, iterative search for low-fragmentation memory layouts. Idealloc repeatedly applies a box-and-place procedure, retaining the best solution found and using early stopping to discard unpromising attempts. This combination of theoretical foundations and randomized exploration is designed to scale to large problem instances..

# Download and Build

The following commands clone the repository and build the C++ implementation using CMake.

```sh
git https://github.com/elisa-frohlich/idealloc-cpp.git
cd idealloc-cpp
cmake -B build .
cmake --build build
```

# Run

With the commands above, the `idealloc` binary will be located under `./build/idealloc`.

You can run idealloc with the following command:

```
./build/idealloc [OPTIONS] <FORMAT>
```

The list of `[OPTIONS]` is the following:
- `-i, --input <PATH>`: Path to the input CSV file (required).
- `-f, --max-frag <RATIO>`: Maximum fragmentation allowed, expressed as a
  ratio of makespan to LOAD (default `1.0`). `1.0` means no fragmentation
  is allowed; `1.2` means up to 20% waste is acceptable. The algorithm
  stops as soon as it finds an allocation with
  `makespan ≤ max_frag × LOAD`.
- `-l, --max-lives <N>`: Maximum number of iterations of the local search
  (default `1`). The algorithm stops after `N` iterations, or as soon as
  the target fragmentation is reached, whichever comes first.

The `<FORMAT>` is either `in-csv` (intervals are in the format [l,r]), `in-ex-csv` (intervals are in the format [l,r)) or `ex-csv` (intervals are in the format (l,r)).

## Example

We will show how to interpret the output given the following run:

```
./build/idealloc -l 10000 -f 1.2 -i benchmarks/minimalloc/A.1048576.csv ex-csv 
```

Here, we run idealloc over the instance `minimalloc/A.1048576.csv` using the `ex-csv` format, with a maximum fragmentation of 20% (1.2) and allowing at most 10000 iterations of the local search algorithm.

The output can be broken down in 4 parts:

### Prelude
```
Events processed.
Size-life ordering done.
First-fit done.
Prelude overhead: 1187 μs
```

The first 4 lines are information about the prelude of the algorithm, where it computes the SLFF(size-lifetime first-fit) heuristic that serves as the initial solution.


### Local-search improvements
```
Beating heuristic by 10240 bytes! (4 iterations)
Beating heuristic by 12288 bytes! (6 iterations)
Beating heuristic by 21504 bytes! (728 iterations)
Beating heuristic by 46080 bytes! (2525 iterations)
Beating heuristic by 51200 bytes! (6147 iterations)
```

This run outputs 5 lines in this part, showing that the local search was able to improve the allocation 5 times, at iterations 4, 6, 728, 2525 and 6147. Each value is the cumulative improvement over the SLFF solution, not the incremental gain since the previous line.

### Instance characterization
```
Buffers treated:	154
Heights hardness:	141.31%
Conflicts hardness:	39.4024%
Deaths hardness:	50.4288%
Compound hardness:	5.06031
```

In this part, idealloc prints information characterizing the input instance:
- The number of buffers in the instance.
- The **heights hardness**: the coefficient of variation of buffer sizes
  (σ_size / mean_size). 0% means every buffer has the same size; higher
  values mean the size distribution is more heterogeneous.
- The **conflicts hardness**: the density of the interference graph, i.e.,
  the fraction of buffer pairs that are simultaneously alive. 0% means no
  two buffers ever overlap; 100% means every pair overlaps.
- The **deaths hardness**: the coefficient of variation of buffer end
  times. A lower value means most buffers end around the same time; a
  higher value means end times are scattered over the timeline.
- The **compound hardness**: computed as
  (1 + heights) × (1 + conflicts) × (1 + deaths). It's a single
  multiplicative score combining all three, and 0 only when all three
  components are 0 (uniform sizes, no conflicts, no variance in deaths).

### Final results
```
51200 fewer bytes than heuristic.
Total allocation time: 5019270 μs
Makespan:	1301504 bytes
LOAD:		1048576 bytes
Fragmentation:	 24.1211%
```

Finally, it shows some information about the results:
- how much it improved from the initial solution;
- the time taken to run the algorithm;
- the size of the found allocation (makespan);
- the theoretical lower bound for the DSA (LOAD, or max load, which is the maximum size needed by buffers that are alive at the same time at some point);
- the fragmentation, which is computed as makespan/LOAD - 1, in percentage.

Notice that, since the achieved fragmentation (24.12%) is still greater than the target (20%), the algorithm ran for 10000 iterations without reaching the target.