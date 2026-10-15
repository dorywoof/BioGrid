#include "Trial.hpp"

#include <cstdio>
#include <cstdlib>

#ifndef BIOGRID_DATA_DIR
#define BIOGRID_DATA_DIR "data"
#endif

namespace {

Outcome total(const std::shared_ptr<const Brain::Wiring>& wiring, const Condition& condition, int seeds, float seconds)
{
    Outcome sum;
    for (int seed = 1; seed <= seeds; ++seed) {
        const Outcome o = runTrial(wiring, condition, static_cast<unsigned>(seed), seconds);
        sum.drinks += o.drinks;
        sum.deaths += o.deaths;
        sum.shadeSeconds += o.shadeSeconds;
    }
    return sum;
}

}

int main(int argc, char** argv)
{
    const int seeds = argc > 1 ? std::atoi(argv[1]) : 3;
    const float seconds = argc > 2 ? static_cast<float>(std::atof(argv[2])) : 60.f;

    const auto wiring = Brain::load(BIOGRID_DATA_DIR);
    const Outcome intact = total(wiring, {"intact", {}}, seeds, seconds);
    const Outcome anosmic = total(wiring, {"anosmic", {}, true}, seeds, seconds);

    std::printf("intact   drinks %4d  shade %6.1fs  deaths %3d\n", intact.drinks, intact.shadeSeconds, intact.deaths);
    std::printf("anosmic  drinks %4d  shade %6.1fs  deaths %3d\n", anosmic.drinks, anosmic.shadeSeconds, anosmic.deaths);

    const bool seeksWater = intact.drinks * 2 > anosmic.drinks * 3;
    const bool seeksShade = intact.shadeSeconds > anosmic.shadeSeconds * 1.5f;
    if (!seeksWater)
        std::puts("FAIL: the whole brain does not find water better than an anosmic fly");
    if (!seeksShade)
        std::puts("FAIL: the whole brain does not find shade better than an anosmic fly");
    return seeksWater && seeksShade ? 0 : 1;
}
