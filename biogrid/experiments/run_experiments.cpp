#include "Trial.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef BIOGRID_DATA_DIR
#define BIOGRID_DATA_DIR "data"
#endif

int main(int argc, char** argv)
{
    const std::string path = argc > 1 ? argv[1] : "experiments.csv";
    const int seeds = argc > 2 ? std::atoi(argv[2]) : 12;
    const float seconds = argc > 3 ? static_cast<float>(std::atof(argv[3])) : 120.f;

    const std::vector<Condition> conditions{
        {"intact", {}},
        {"VP relay silenced", {Brain::RelayNeurons}},
        {"DNb05 silenced", {Brain::SteeringNeurons}},
        {"Ir68a mutant (VP5 silenced)", {Brain::MoistNeurons}},
        {"VP3a silenced", {Brain::CoolNeurons}},
        {"anosmic", {}, true},
    };

    struct Job
    {
        std::size_t condition;
        unsigned seed;
        Outcome outcome;
    };
    std::vector<Job> jobs;
    for (std::size_t c = 0; c < conditions.size(); ++c)
        for (int seed = 1; seed <= seeds; ++seed)
            jobs.push_back({c, static_cast<unsigned>(seed), {}});

    const auto wiring = Brain::load(BIOGRID_DATA_DIR);
    std::atomic<std::size_t> next{0};
    std::mutex printing;
    auto worker = [&] {
        for (std::size_t k = next++; k < jobs.size(); k = next++) {
            Job& job = jobs[k];
            job.outcome = runTrial(wiring, conditions[job.condition], job.seed, seconds);
            const std::lock_guard<std::mutex> lock(printing);
            std::printf("%-30s seed %2u  drinks %3d  shade %5.1fs  heat %.2f  deaths %d  |turn| %.2f\n",
                        conditions[job.condition].name.c_str(), job.seed, job.outcome.drinks, job.outcome.shadeSeconds,
                        job.outcome.meanHeat, job.outcome.deaths, job.outcome.meanAbsTurn);
            std::fflush(stdout);
        }
    };
    std::vector<std::thread> pool;
    const unsigned threads = std::max(1u, std::thread::hardware_concurrency());
    for (unsigned t = 0; t < threads; ++t)
        pool.emplace_back(worker);
    for (auto& t : pool)
        t.join();

    std::ofstream csv(path);
    csv << "condition,seed,seconds,drinks,shade_seconds,mean_heat,deaths,mean_abs_turn\n";
    for (const Job& job : jobs)
        csv << '"' << conditions[job.condition].name << "\"," << job.seed << ',' << seconds << ',' << job.outcome.drinks
            << ',' << job.outcome.shadeSeconds << ',' << job.outcome.meanHeat << ',' << job.outcome.deaths << ','
            << job.outcome.meanAbsTurn << '\n';
    std::printf("wrote %s\n", path.c_str());
}
