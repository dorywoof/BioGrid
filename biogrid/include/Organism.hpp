#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <random>

struct Vec2
{
    float x = 0.f;
    float y = 0.f;

    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float k) const { return {x * k, y * k}; }
    float dot(Vec2 o) const { return x * o.x + y * o.y; }
    float length() const { return std::sqrt(x * x + y * y); }

    static Vec2 fromAngle(float a) { return {std::cos(a), std::sin(a)}; }
};

class Organism
{
public:
    static constexpr float kRadius = 11.f;
    static constexpr float kAntennaLength = 22.f;
    static constexpr float kAntennaSpread = 0.6f;
    static constexpr float kMaxEnergy = 100.f;

    enum Side { Left, Right };
    enum class Activity { Flying, Drinking, Resting };

    Vec2 position;
    float heading = 0.f;
    float speed = 0.f;
    float energy = kMaxEnergy;
    float heat = 0.f;
    float fatigue = 0.f;
    float altitude = 1.f;
    float wingPhase = 0.f;
    float hurtFlash = 0.f;
    float saccadeFlash = 0.f;
    Activity activity = Activity::Flying;
    std::array<float, 2> moisture{};
    std::array<float, 2> coolness{};
    std::deque<Vec2> trail;

    void seed(unsigned value) { rng.seed(value); }

    void respawn(Vec2 at, float facing)
    {
        position = at;
        heading = facing;
        speed = kTakeoffSpeed;
        energy = kMaxEnergy;
        heat = 0.f;
        fatigue = 0.f;
        altitude = 1.f;
        hurtFlash = saccadeFlash = 0.f;
        activity = Activity::Flying;
        groundTime = saccadeLeft = saccadeRate = evidence = 0.f;
        moisture.fill(0.f);
        coolness.fill(0.f);
        trail.clear();
        trailClock = 0.f;
    }

    bool flying() const { return activity == Activity::Flying; }
    float antennaAngle(Side side) const { return heading + (side == Left ? -kAntennaSpread : kAntennaSpread); }
    Vec2 antennaTip(Side side) const { return position + Vec2::fromAngle(antennaAngle(side)) * kAntennaLength; }

    void fly(float cruise, float steering, float dt)
    {
        if (!flying()) {
            settle(dt);
            return;
        }

        float target = cruise * kFlightScale;
        if (saccadeLeft > 0.f) {
            const float step = std::min(dt, saccadeLeft);
            heading += saccadeRate * step;
            saccadeLeft -= step;
            target *= kSaccadeSlowdown;
        } else {
            evidence += dt * (steering - evidence / kEvidenceLeak);
            heading += kDrift * steering * dt;
            if (std::abs(evidence) > kSaccadeThreshold) {
                saccade(std::clamp(std::abs(evidence) * kSaccadeGain, kMinSaccade, kMaxSaccade) * (evidence > 0.f ? 1.f : -1.f));
            } else if (uniform(rng) < kSpontaneousRate * dt) {
                saccade(std::uniform_real_distribution<float>(kMinSaccade, kSpontaneousMax)(rng) * (uniform(rng) < 0.5f ? -1.f : 1.f));
            }
        }

        speed += (target - speed) * std::min(1.f, dt * kResponsiveness);
        altitude = std::min(1.f, altitude + dt * kLiftRate);
        fatigue = std::min(1.f, fatigue + dt / kEndurance);
        if (fatigue >= 1.f)
            land(Activity::Resting, kRestTime);
        advance(dt);
    }

    void steer(float targetSpeed, float turnRate, float dt)
    {
        if (!flying()) {
            settle(dt);
            if (targetSpeed > 0.f && groundTime > kManualTakeoffDelay)
                groundTime = kManualTakeoffDelay;
            return;
        }
        speed += (targetSpeed - speed) * std::min(1.f, dt * kResponsiveness);
        heading += turnRate * dt;
        altitude = std::min(1.f, altitude + dt * kLiftRate);
        advance(dt);
    }

    void land(Activity why, float seconds)
    {
        activity = why;
        groundTime = seconds;
        saccadeLeft = 0.f;
        evidence = 0.f;
    }

    void bounceInside(float width, float height)
    {
        Vec2 dir = Vec2::fromAngle(heading);
        if (position.x < kRadius || position.x > width - kRadius)
            dir.x = -dir.x;
        if (position.y < kRadius || position.y > height - kRadius)
            dir.y = -dir.y;
        heading = std::atan2(dir.y, dir.x);
        position.x = std::clamp(position.x, kRadius, width - kRadius);
        position.y = std::clamp(position.y, kRadius, height - kRadius);
    }

private:
    static constexpr float kResponsiveness = 4.f;
    static constexpr float kFlightScale = 1.6f;
    static constexpr float kTakeoffSpeed = 60.f;
    static constexpr float kLiftRate = 3.f;
    static constexpr float kEvidenceLeak = 0.3f;
    static constexpr float kSaccadeThreshold = 0.15f;
    static constexpr float kSaccadeGain = 2.4f;
    static constexpr float kMinSaccade = 0.35f;
    static constexpr float kMaxSaccade = 1.7f;
    static constexpr float kSaccadeDuration = 0.07f;
    static constexpr float kSaccadeSlowdown = 0.6f;
    static constexpr float kDrift = 0.35f;
    static constexpr float kSpontaneousRate = 0.15f;
    static constexpr float kSpontaneousMax = 0.9f;
    static constexpr float kEndurance = 14.f;
    static constexpr float kRestTime = 1.6f;
    static constexpr float kManualTakeoffDelay = 0.1f;
    static constexpr float kWingBeat = 38.f;
    static constexpr float kTrailInterval = 0.04f;
    static constexpr std::size_t kTrailLength = 90;

    float groundTime = 0.f;
    float saccadeLeft = 0.f;
    float saccadeRate = 0.f;
    float evidence = 0.f;
    float trailClock = 0.f;
    std::mt19937 rng{7};
    std::uniform_real_distribution<float> uniform{0.f, 1.f};

    void saccade(float angle)
    {
        saccadeRate = angle / kSaccadeDuration;
        saccadeLeft = kSaccadeDuration;
        evidence = 0.f;
        saccadeFlash = 1.f;
    }

    void settle(float dt)
    {
        speed += (0.f - speed) * std::min(1.f, dt * 10.f);
        altitude = std::max(0.f, altitude - dt * kLiftRate);
        fatigue = std::max(0.f, fatigue - dt / kRestTime);
        groundTime -= dt;
        if (groundTime <= 0.f) {
            activity = Activity::Flying;
            speed = kTakeoffSpeed;
        }
        advance(dt);
    }

    void advance(float dt)
    {
        position = position + Vec2::fromAngle(heading) * (speed * dt);
        hurtFlash = std::max(0.f, hurtFlash - dt * 2.5f);
        saccadeFlash = std::max(0.f, saccadeFlash - dt * 6.f);
        if (flying())
            wingPhase += dt * kWingBeat;

        trailClock += dt;
        if (trailClock >= kTrailInterval) {
            trailClock = 0.f;
            trail.push_back(position);
            if (trail.size() > kTrailLength)
                trail.pop_front();
        }
    }
};
