#pragma once

#include "Brain.hpp"
#include "Organism.hpp"
#include "World.hpp"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

struct Condition
{
    std::string name;
    std::vector<Brain::Lesion> lesions;
    bool anosmic = false;
};

struct Outcome
{
    int drinks = 0;
    int deaths = 0;
    float shadeSeconds = 0.f;
    float meanHeat = 0.f;
    float meanAbsTurn = 0.f;
};

inline Outcome runTrial(const std::shared_ptr<const Brain::Wiring>& wiring, const Condition& condition, unsigned seed,
                        float seconds)
{
    constexpr float frame = 1.f / 60.f;
    World world(720.f, 720.f, seed);
    Organism fly;
    Brain brain(wiring, seed * 7919u + 13u);
    for (Brain::Lesion lesion : condition.lesions)
        brain.setLesion(lesion, true);
    world.reset(fly);

    Outcome out;
    double heat = 0.0;
    double turn = 0.0;
    long frames = 0;
    for (float t = 0.f; t < seconds; t += frame) {
        world.sense(fly);
        const float gate = condition.anosmic ? 0.f : 1.f;
        brain.feedSensor(Brain::MoistLeft, gate * fly.moisture[Organism::Left]);
        brain.feedSensor(Brain::MoistRight, gate * fly.moisture[Organism::Right]);
        brain.feedSensor(Brain::CoolLeft, gate * fly.coolness[Organism::Left]);
        brain.feedSensor(Brain::CoolRight, gate * fly.coolness[Organism::Right]);
        brain.advance(frame * 1000.f);
        brain.takeFrameSpikes();

        const auto command = brain.decode();
        fly.fly(command.speed, command.turn, frame);

        const int generation = world.generation;
        world.update(fly, frame);
        out.deaths += world.generation - generation;
        heat += fly.heat;
        turn += std::abs(command.turn);
        ++frames;
    }
    out.drinks = world.drinks;
    out.shadeSeconds = world.shadeTime;
    out.meanHeat = static_cast<float>(heat / frames);
    out.meanAbsTurn = static_cast<float>(turn / frames);
    return out;
}
