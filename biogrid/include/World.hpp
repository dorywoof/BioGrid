#pragma once

#include "Organism.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

struct Droplet
{
    Vec2 position;
    float phase = 0.f;
};

struct Shade
{
    Vec2 position;
    Vec2 velocity;
    float radius = 0.f;
    float phase = 0.f;
};

struct Ripple
{
    Vec2 position;
    float age = 0.f;
};

class World
{
public:
    static constexpr float kWaterRadius = 6.f;
    static constexpr int kWaterCount = 12;
    static constexpr int kShadeCount = 3;
    static constexpr float kShadeRadius = 62.f;
    static constexpr float kRippleLife = 0.6f;

    float width;
    float height;
    std::vector<Droplet> water;
    std::vector<Shade> shades;
    std::vector<Ripple> ripples;
    int score = 0;
    int best = 0;
    int generation = 1;
    int drinks = 0;
    float lifetime = 0.f;
    float shadeTime = 0.f;

    World(float w, float h, unsigned seed = std::random_device{}())
        : width(w), height(h), rng(seed)
    {
    }

    void reset(Organism& organism)
    {
        score = drinks = 0;
        generation = 1;
        lifetime = shadeTime = shadeCredit = 0.f;
        ripples.clear();
        water.clear();
        shades.clear();
        organism.seed(static_cast<unsigned>(rng()));
        organism.respawn(center(), randomAngle());
        for (int i = 0; i < kShadeCount; ++i)
            shades.push_back(spawnShade(organism));
        for (int i = 0; i < kWaterCount; ++i)
            water.push_back(spawnDroplet(organism));
    }

    bool inShade(Vec2 at) const
    {
        return std::any_of(shades.begin(), shades.end(),
                           [at](const Shade& s) { return (s.position - at).length() < s.radius; });
    }

    void update(Organism& organism, float dt)
    {
        lifetime += dt;
        drift(dt);

        for (auto& r : ripples)
            r.age += dt;
        ripples.erase(std::remove_if(ripples.begin(), ripples.end(),
                                     [](const Ripple& r) { return r.age > kRippleLife; }),
                      ripples.end());

        organism.bounceInside(width, height);

        const bool cool = inShade(organism.position);
        if (cool) {
            organism.heat = std::max(0.f, organism.heat - kCoolingRate * dt);
            shadeTime += dt;
            shadeCredit += dt;
            while (shadeCredit >= 1.f) {
                shadeCredit -= 1.f;
                ++score;
            }
        } else {
            organism.heat = std::min(1.f, organism.heat + kHeatingRate * dt);
        }
        const float overheat = std::max(0.f, organism.heat - kHeatTolerance) / (1.f - kHeatTolerance);
        organism.energy -= (kEnergyDrain * (organism.flying() ? 1.f : kGroundedDrain) + kHeatDamage * overheat) * dt;
        if (overheat > 0.f)
            organism.hurtFlash = std::max(organism.hurtFlash, 0.35f * overheat);

        for (auto& p : water) {
            if (organism.flying() && (p.position - organism.position).length() < Organism::kRadius + kWaterRadius) {
                ripples.push_back({p.position, 0.f});
                score += 10;
                ++drinks;
                organism.energy = std::min(Organism::kMaxEnergy, organism.energy + kWaterHydration);
                organism.land(Organism::Activity::Drinking, kDrinkTime);
                p = spawnDroplet(organism);
            }
        }

        best = std::max(best, score);
        if (organism.energy <= 0.f) {
            ++generation;
            score = 0;
            lifetime = 0.f;
            organism.respawn(center(), randomAngle());
        }
    }

    void sense(Organism& organism) const
    {
        for (auto side : {Organism::Left, Organism::Right}) {
            const Vec2 dir = Vec2::fromAngle(organism.antennaAngle(side));
            const Vec2 tip = organism.antennaTip(side);
            float moist = 0.f;
            for (const auto& p : water)
                moist += reach(p.position - tip, 0.f, dir, kMoistureScale, kMoistureRange);
            float cool = 0.f;
            for (const auto& s : shades)
                cool += reach(s.position - tip, s.radius, dir, kCoolScale, kCoolRange);
            organism.moisture[side] = std::min(kSignalCap, moist);
            organism.coolness[side] = std::min(kSignalCap, cool);
        }
    }

private:
    static constexpr float kEnergyDrain = 2.6f;
    static constexpr float kGroundedDrain = 0.35f;
    static constexpr float kHeatingRate = 1.f / 16.f;
    static constexpr float kCoolingRate = 1.f / 3.f;
    static constexpr float kHeatTolerance = 0.6f;
    static constexpr float kHeatDamage = 9.f;
    static constexpr float kDrinkTime = 0.9f;
    static constexpr float kWaterHydration = 22.f;
    static constexpr float kSignalCap = 1.6f;
    static constexpr float kMoistureScale = 90.f;
    static constexpr float kMoistureRange = 380.f;
    static constexpr float kCoolScale = 70.f;
    static constexpr float kCoolRange = 320.f;
    static constexpr float kShadeDrift = 9.f;
    static constexpr float kSpawnMargin = 30.f;
    static constexpr float kSafeDistance = 90.f;

    std::mt19937 rng;
    float shadeCredit = 0.f;

    Vec2 center() const { return {width * 0.5f, height * 0.5f}; }

    float randomAngle()
    {
        return std::uniform_real_distribution<float>(-3.14159265f, 3.14159265f)(rng);
    }

    Vec2 randomPoint(float margin)
    {
        std::uniform_real_distribution<float> xs(margin, width - margin);
        std::uniform_real_distribution<float> ys(margin, height - margin);
        return {xs(rng), ys(rng)};
    }

    Droplet spawnDroplet(const Organism& organism)
    {
        Vec2 at = randomPoint(kSpawnMargin);
        for (int attempt = 0; attempt < 40; ++attempt) {
            const bool clear = (at - organism.position).length() > kSafeDistance && !inShade(at) &&
                               std::none_of(water.begin(), water.end(),
                                            [at](const Droplet& d) { return (d.position - at).length() < 28.f; });
            if (clear)
                break;
            at = randomPoint(kSpawnMargin);
        }
        return {at, randomAngle()};
    }

    Shade spawnShade(const Organism& organism)
    {
        Vec2 at = randomPoint(kShadeRadius);
        for (int attempt = 0; attempt < 40; ++attempt) {
            const bool clear = (at - organism.position).length() > kShadeRadius + kSafeDistance &&
                               std::none_of(shades.begin(), shades.end(), [at](const Shade& s) {
                                   return (s.position - at).length() < 2.6f * kShadeRadius;
                               });
            if (clear)
                break;
            at = randomPoint(kShadeRadius);
        }
        return {at, Vec2::fromAngle(randomAngle()) * kShadeDrift, kShadeRadius, randomAngle()};
    }

    void drift(float dt)
    {
        for (auto& s : shades) {
            s.position = s.position + s.velocity * dt;
            if (s.position.x < s.radius || s.position.x > width - s.radius)
                s.velocity.x = -s.velocity.x;
            if (s.position.y < s.radius || s.position.y > height - s.radius)
                s.velocity.y = -s.velocity.y;
        }
    }

    static float reach(Vec2 offset, float radius, Vec2 dir, float scale, float range)
    {
        const float centre = std::max(offset.length(), 1e-3f);
        const float dist = std::max(0.f, centre - radius);
        if (dist > range)
            return 0.f;
        const float facing = std::clamp(0.5f + 0.5f * dir.dot(offset * (1.f / centre)), 0.f, 1.f);
        const float falloff = 1.f / (1.f + (dist / scale) * (dist / scale));
        return facing * facing * falloff;
    }
};
