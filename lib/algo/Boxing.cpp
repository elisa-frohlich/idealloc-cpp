#include "algo/Boxing.h"
#include <cassert>
#include <cmath>
#include <execution>
#include <iostream>

std::shared_ptr<Instance> rogue(std::shared_ptr<Instance> input, double epsilon) {
    auto [r, mu, h, is_safe] = input->get_safety_info(epsilon);
    const std::size_t target_size = static_cast<std::size_t>(std::floor(mu * h));

    if (is_safe) {
        const double lgr  = std::log2(r);
        const double lg2r = lgr * lgr;
        const double rhs  = 1.0 / epsilon;
        // std::cerr << std::fixed << std::setprecision(14) << "rogue: r=" << r << " epsilon=" << epsilon
        //       << " lg2r^2=" << lg2r << " 1/eps=" << rhs << " target_size=" << target_size << "\n";
        assert(std::pow(std::log2(r), 2.0) >= 1.0 / epsilon);
        auto [x_s, x_l] = input->split_by_height(target_size);
        Instance small_boxed = c_15(std::make_shared<Instance>(std::move(x_s)), h, mu);

        // std::cerr << "Input: " << *input << "\n";
        // std::cerr << "x_s: " << x_s << "\n";
        // std::cerr << "x_l: " << x_l << "\n";
        // std::cerr << "small_boxed: " << small_boxed << "\n";
        return rogue(x_l.merge_with(std::move(small_boxed)), epsilon);
    }
    return input;
}


Instance c_15(std::shared_ptr<Instance> input, double h, double epsilon) {
    auto res = std::make_shared<SharedMutex<Instance>>(Instance(JobSet{}));

    auto buckets = Instance::make_buckets(std::move(input), epsilon);

    std::for_each(std::execution::par, buckets.begin(), buckets.end(),
        [&](auto& bucket) {
            auto [h_i, unit_jobs] = std::move(bucket);
            assert(static_cast<double>(h_i) <= h);

            const std::size_t h_param =
                static_cast<std::size_t>(std::floor(h / static_cast<double>(h_i)));
            const std::size_t h_cast = static_cast<std::size_t>(h);

            Instance boxed = t_2(std::move(unit_jobs), h_param, h_cast,
                                 epsilon, std::nullopt);

            std::lock_guard<std::mutex> guard(res->mtx);
            res->value.merge_via_ref(std::move(boxed));
        });

    // Rust: Arc::into_inner(res)
    if (res.use_count() != 1) {
        throw std::runtime_error("Could not unwrap Arc!");
    }
    return std::move(res->value);
}

Instance t_2(Instance input,
             std::size_t h,
             std::size_t h_real,
             double epsilon,
             std::optional<T2Control> ctrl)
{
    // std::cerr << "t_2: input=" << input << " h=" << h << " h_real=" << h_real << " eps=" << epsilon << "\n";
    JobSet res_jobs;
    JobSet all_unresolved;

    // Rust: if let Some(v) = ctrl { v } else { T2Control::new(&input) }
    T2Control ctrl_val = ctrl ? std::move(*ctrl) : T2Control(input);

    // Rust: ctrl.critical_points.iter().copied().collect::<Vec<_>>()
    std::vector<std::size_t> pts_vec(ctrl_val.critical_points.begin(),
                                      ctrl_val.critical_points.end());

    auto [r_coarse, x_is] = Instance::split_by_liveness(input, ctrl_val.critical_points);
    assert(!r_coarse.empty() && "Theorem 2 entered infinite loop");

    std::vector<JobSet> r_is = jobset::split_ris(std::move(r_coarse), pts_vec);

    for (auto& r_i : r_is) {
        auto [boxed, unresolved] = lemma_1(std::move(r_i), h, h_real, epsilon);

        all_unresolved.insert(all_unresolved.end(),
                              std::make_move_iterator(unresolved.begin()),
                              std::make_move_iterator(unresolved.end()));

        if (boxed) {
            res_jobs.insert(res_jobs.end(),
                            std::make_move_iterator(boxed->begin()),
                            std::make_move_iterator(boxed->end()));
        }
    }

    // std::cerr << "t_2: all_unresolved=" << all_unresolved << "\n";
    auto igc_rows = jobset::interval_graph_coloring(std::move(all_unresolved));

    std::set<std::size_t> points_to_allocate;
    std::size_t row_count = 0;
    // std::cerr << "t_2: igc_rows={\n";
    // for (auto& row : igc_rows) {
    //     std::cerr << "\t" << row << "\n";
    // }
    // std::cerr << "}\n";
    JobSet jobs_buf;
    for (auto& row : igc_rows) {
        auto [alpha, omega] = ctrl_val.bounding_interval;
        auto gaps = jobset::gap_finder(row, alpha, omega);
        points_to_allocate.insert(gaps.begin(), gaps.end());

        ++row_count;
        jobs_buf.insert(jobs_buf.end(),
                        std::make_move_iterator(row.begin()),
                        std::make_move_iterator(row.end()));

        if (row_count % h == 0) {
            // std::cerr << "Pushing job 1\n";
            res_jobs.push_back(std::make_shared<Job>(
                Job::new_box(std::move(jobs_buf), h_real)));
            jobs_buf.clear();
        }
    }
    if (!jobs_buf.empty()) {
        // std::cerr << "Pushing job 2\n";
        res_jobs.push_back(std::make_shared<Job>(
            Job::new_box(std::move(jobs_buf), h_real)));
    }

    auto res = std::make_shared<SharedMutex<Instance>>(
        Instance(std::move(res_jobs)));

    // Flatten x_is into a vector for parallel iteration.
    // Rust: x_is.into_par_iter().for_each(...)
    std::vector<std::pair<std::size_t, Instance>> x_is_vec;
    x_is_vec.reserve(x_is.size());
    for (auto& [k, v] : x_is) {
        x_is_vec.emplace_back(k, std::move(v));
    }

    std::for_each(std::execution::par, x_is_vec.begin(), x_is_vec.end(),
        [&](auto& kv) {
            const std::size_t i = kv.first;
            Instance& x_i = kv.second;

            // Local peekable iterator over points_to_allocate
            auto it       = points_to_allocate.begin();
            const auto en = points_to_allocate.end();

            const std::size_t bi_start = pts_vec[i];
            const std::size_t bi_end   = pts_vec[i + 1];

            std::set<std::size_t> crit_pts = { bi_start, bi_end };

            bool pts_ready = false;
            while (it != en) {
                const std::size_t v = *it;
                if (v <= bi_start) {
                    ++it;
                } else if (v >= bi_end) {
                    break;
                } else {
                    if (!pts_ready) {
                        for (const auto& j : x_i.jobs) {
                            if (j->is_live_at(v)) { pts_ready = true; break; }
                        }
                    }
                    crit_pts.insert(v);
                    ++it;
                }
            }
            if (!pts_ready) {
                // Loop until gen_crit returns a value not already in crit_pts.
                while (!crit_pts.insert(
                        T2Control::gen_crit(x_i, bi_start, bi_end)).second) {}
            }

            Instance x_i_res = t_2(std::move(x_i), h, h_real, epsilon, 
                                T2Control({bi_start, bi_end}, std::move(crit_pts)));

            std::lock_guard<std::mutex> guard(res->mtx);
            res->value.merge_via_ref(std::move(x_i_res));
    });

    if (res.use_count() != 1) {
        throw std::runtime_error("Bad multithreading @ T2!");
    }
    return std::move(res->value);
}

std::pair<std::optional<JobSet>, JobSet>
lemma_1(JobSet input, std::size_t h, std::size_t h_real, double e)
{
    const std::size_t outer_num =
        h * static_cast<std::size_t>(std::ceil(1.0 / (e * e)));
    std::size_t total_jobs = input.size();

    if (total_jobs <= 2 * outer_num) {
        return { std::nullopt, std::move(input) };
    }

    // hor_source: sorted ascending by death
    IndexedJobMap hor_source;
    hor_source.reserve(input.size());
    for (const auto& j : input) {
        hor_source.emplace_back(j->id, j);   // copy shared_ptr
    }
    std::sort(hor_source.begin(), hor_source.end(),
        [](const auto& a, const auto& b) {
            return a.second->death < b.second->death;
        });

    // vert_source: input in reverse order
    IndexedJobMap vert_source;
    vert_source.reserve(input.size());
    for (auto it = input.rbegin(); it != input.rend(); ++it) {
        vert_source.emplace_back((*it)->id, *it);   // copy shared_ptr
    }

    auto outer_vert = strip_cuttin<VertStripJob>(vert_source, hor_source, outer_num);
    auto outer_hor  = strip_cuttin<HorStripJob>(hor_source, vert_source, outer_num);

    total_jobs -= 2 * outer_num;
    std::size_t inner_jobs = 0;

    const std::size_t inner_num =
        h * static_cast<std::size_t>(std::ceil(1.0 / e));

    std::vector<std::priority_queue<VertStripJob>> inner_vert;
    std::vector<std::priority_queue<HorStripJob>>  inner_hor;

    while (inner_jobs < total_jobs) {
        auto vert_strip =
            strip_cuttin<VertStripJob>(vert_source, hor_source, inner_num);
        inner_jobs += vert_strip.size();
        inner_vert.push_back(std::move(vert_strip));
        if (inner_jobs == total_jobs) break;

        auto hor_strip =
            strip_cuttin<HorStripJob>(hor_source, vert_source, inner_num);
        inner_jobs += hor_strip.size();
        inner_hor.push_back(std::move(hor_strip));
    }

    assert(vert_source.empty() && hor_source.empty());

    JobSet outer_jobs;
    while (!outer_vert.empty()) {
        outer_jobs.push_back(outer_vert.top().job);
        outer_vert.pop();
    }
    while (!outer_hor.empty()) {
        outer_jobs.push_back(outer_hor.top().job);
        outer_hor.pop();
    }

    return {
        strip_boxin(std::move(inner_vert), std::move(inner_hor), h, h_real),
        std::move(outer_jobs)
    };
}