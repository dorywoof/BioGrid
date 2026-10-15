#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

class Brain
{
public:
    enum Sensor { MoistLeft, MoistRight, CoolLeft, CoolRight, SensorCount };
    enum Motor { SteerLeft, SteerRight, MotorCount };
    enum Lesion { RelayNeurons, SteeringNeurons, MoistNeurons, CoolNeurons, LesionCount };
    enum class Kind : std::uint8_t { Central, Sensory, Descending, Visual };
    enum Group : std::uint8_t {
        None, VP5Left, VP5Right, VP3aLeft, VP3aRight, DNb05Left, DNb05Right,
        DNa01Left, DNa01Right, DNa02Left, DNa02Right, Relay, GroupCount
    };

    struct Command
    {
        float speed = 0.f;
        float turn = 0.f;
    };

    struct Synapse
    {
        std::uint32_t target;
        float weight;
    };

    struct Wiring
    {
        std::vector<float> px, py;
        std::vector<std::uint8_t> kinds, groups;
        std::vector<std::uint32_t> offsets, targets;
        std::vector<std::int16_t> weights;
        std::array<std::vector<std::uint32_t>, GroupCount> members;
        long long totalSynapses = 0;

        std::size_t size() const { return px.size(); }
    };

    static constexpr float kDt = 0.1f;

    static std::shared_ptr<const Wiring> load(const std::string& dataDir)
    {
        const std::string path = dataDir + "/brain.bin";
        std::ifstream file(path, std::ios::binary);
        if (!file)
            throw std::runtime_error("cannot open " + path);
        char magic[4];
        std::uint32_t header[3];
        file.read(magic, 4);
        file.read(reinterpret_cast<char*>(header), sizeof header);
        if (!file || std::memcmp(magic, "FLYB", 4) != 0 || header[0] != 1)
            throw std::runtime_error("not a BioGrid brain file: " + path);

        auto wiring = std::make_shared<Wiring>();
        Wiring& w = *wiring;
        const std::size_t n = header[1];
        const std::size_t m = header[2];
        std::vector<std::int8_t> sides;
        readInto(file, w.px, n);
        readInto(file, w.py, n);
        readInto(file, sides, n);
        readInto(file, w.kinds, n);
        readInto(file, w.groups, n);
        readInto(file, w.offsets, n + 1);
        readInto(file, w.targets, m);
        readInto(file, w.weights, m);
        for (std::int16_t weight : w.weights)
            w.totalSynapses += std::abs(weight);

        for (std::uint32_t i = 0; i < n; ++i)
            if (w.groups[i] < GroupCount)
                w.members[w.groups[i]].push_back(i);
        for (Group grp : {VP5Left, VP5Right, VP3aLeft, VP3aRight, DNb05Left, DNb05Right, Relay})
            if (w.members[grp].empty())
                throw std::runtime_error("brain file is missing a required neuron group");
        return wiring;
    }

    explicit Brain(const std::string& dataDir, unsigned seed = std::random_device{}())
        : Brain(load(dataDir), seed)
    {
    }

    Brain(std::shared_ptr<const Wiring> shared, unsigned seed)
        : wiring(std::move(shared)), rng(seed)
    {
        const std::size_t n = wiring->size();
        u.assign(n, 0.f);
        g.assign(n, 0.f);
        settledAt.assign(n, 0.f);
        refractoryUntil.assign(n, -1.f);
        lastSpike.assign(n, -1e9f);
        isActive.assign(n, 0);
        silenced.assign(n, 0);
        for (int s = 0; s < kDelaySteps; ++s) {
            pending[s].assign(n, 0.f);
            isTouched[s].assign(n, 0);
        }
        reset();
    }

    void feedSensor(Sensor s, float signal)
    {
        const float clamped = std::max(0.f, signal);
        sensorRate[s] = kMaxSensorRate * clamped / (clamped + kSensorHalfSignal);
    }

    void driveMotor(Motor m, float millivolts) { motorDrive[m] = millivolts; }

    void advance(float ms)
    {
        pendingTime += ms;
        while (pendingTime >= kDt) {
            step();
            pendingTime -= kDt;
        }
    }

    Command decode() const
    {
        const float turn = kTurnGain * (steerRate[SteerRight] - steerRate[SteerLeft]);
        return {kCruise, std::clamp(turn, -kMaxTurn, kMaxTurn)};
    }

    std::vector<std::uint32_t> takeFrameSpikes()
    {
        std::vector<std::uint32_t> frame;
        frame.swap(frameSpikes);
        return frame;
    }

    void reset()
    {
        std::fill(u.begin(), u.end(), 0.f);
        std::fill(g.begin(), g.end(), 0.f);
        std::fill(settledAt.begin(), settledAt.end(), 0.f);
        std::fill(refractoryUntil.begin(), refractoryUntil.end(), -1.f);
        std::fill(lastSpike.begin(), lastSpike.end(), -1e9f);
        std::fill(isActive.begin(), isActive.end(), 0);
        active.clear();
        for (int s = 0; s < kDelaySteps; ++s) {
            for (std::uint32_t i : touched[s]) {
                pending[s][i] = 0.f;
                isTouched[s][i] = 0;
            }
            touched[s].clear();
        }
        frameSpikes.clear();
        sensorRate.fill(0.f);
        motorDrive.fill(0.f);
        steerRate.fill(0.f);
        pendingTime = now = 0.f;
        stepIndex = 0;
    }

    void setLesion(Lesion lesion, bool on)
    {
        lesions[lesion] = on;
        std::fill(silenced.begin(), silenced.end(), 0);
        auto mark = [this](std::initializer_list<Group> list) {
            for (Group grp : list)
                for (std::uint32_t i : wiring->members[grp])
                    silenced[i] = 1;
        };
        if (lesions[RelayNeurons])
            mark({Relay});
        if (lesions[SteeringNeurons])
            mark({DNb05Left, DNb05Right});
        if (lesions[MoistNeurons])
            mark({VP5Left, VP5Right});
        if (lesions[CoolNeurons])
            mark({VP3aLeft, VP3aRight});
    }

    bool lesioned(Lesion lesion) const { return lesions[lesion]; }
    bool isSilenced(std::uint32_t i) const { return silenced[i] != 0; }

    float glow(std::uint32_t i) const { return std::max(0.f, 1.f - (now - lastSpike[i]) / kGlowFade); }
    float steeringRate(Motor m) const { return steerRate[m]; }
    float timeMs() const { return now; }
    std::size_t activeCount() const { return active.size(); }

    std::size_t size() const { return wiring->size(); }
    std::size_t connectionCount() const { return wiring->targets.size(); }
    long long synapseCount() const { return wiring->totalSynapses; }
    float x(std::uint32_t i) const { return wiring->px[i]; }
    float y(std::uint32_t i) const { return wiring->py[i]; }
    Kind kind(std::uint32_t i) const { return static_cast<Kind>(wiring->kinds[i]); }
    Group group(std::uint32_t i) const { return static_cast<Group>(wiring->groups[i]); }
    const std::vector<std::uint32_t>& groupMembers(Group grp) const { return wiring->members[grp]; }
    const std::shared_ptr<const Wiring>& shared() const { return wiring; }

    template <typename Visit>
    void forEachSynapse(std::uint32_t pre, Visit&& visit) const
    {
        const Wiring& w = *wiring;
        for (std::uint32_t k = w.offsets[pre]; k < w.offsets[pre + 1]; ++k)
            visit(Synapse{w.targets[k], static_cast<float>(w.weights[k])});
    }

private:
    static constexpr float kGap = 7.f;
    static constexpr float kTauMembrane = 20.f;
    static constexpr float kTauSynapse = 5.f;
    static constexpr float kRefractory = 2.2f;
    static constexpr float kSynapseWeight = 0.275f;
    static constexpr float kPoissonKick = kSynapseWeight * 250.f;
    static constexpr int kDelaySteps = 18;
    static constexpr float kRateWindow = 200.f;
    static constexpr float kGlowFade = 150.f;
    static constexpr float kMaxSensorRate = 120.f;
    static constexpr float kSensorHalfSignal = 0.6f;
    static constexpr float kCruise = 80.f;
    static constexpr float kTurnGain = 0.035f;
    static constexpr float kMaxTurn = 4.f;

    std::shared_ptr<const Wiring> wiring;
    std::array<Group, SensorCount> sensorGroup{VP5Left, VP5Right, VP3aLeft, VP3aRight};
    std::array<Group, MotorCount> motorGroup{DNb05Left, DNb05Right};

    std::vector<float> u, g, settledAt, refractoryUntil, lastSpike;
    std::vector<std::uint8_t> isActive, silenced;
    std::vector<std::uint32_t> active, nextActive, frameSpikes;
    std::array<std::vector<float>, kDelaySteps> pending;
    std::array<std::vector<std::uint32_t>, kDelaySteps> touched;
    std::array<std::vector<std::uint8_t>, kDelaySteps> isTouched;
    std::array<float, SensorCount> sensorRate{};
    std::array<float, MotorCount> motorDrive{};
    std::array<float, MotorCount> steerRate{};
    std::array<bool, LesionCount> lesions{};
    std::mt19937 rng;
    std::uniform_real_distribution<float> uniform{0.f, 1.f};
    float pendingTime = 0.f;
    float now = 0.f;
    long long stepIndex = 0;

    template <typename T>
    static void readInto(std::ifstream& file, std::vector<T>& out, std::size_t count)
    {
        out.resize(count);
        file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(count * sizeof(T)));
        if (!file)
            throw std::runtime_error("truncated brain file");
    }

    void activate(std::uint32_t i)
    {
        if (!isActive[i]) {
            isActive[i] = 1;
            active.push_back(i);
        }
    }

    void settle(std::uint32_t i)
    {
        const float elapsed = now - settledAt[i];
        if (elapsed > 0.f) {
            const float em = std::exp(-elapsed / kTauMembrane);
            const float es = std::exp(-elapsed / kTauSynapse);
            const float a = g[i] * kTauSynapse / (kTauSynapse - kTauMembrane);
            u[i] = u[i] * em + a * (es - em);
            g[i] *= es;
        }
        settledAt[i] = now;
    }

    void receive(std::uint32_t i, float millivolts)
    {
        if (silenced[i])
            return;
        if (isActive[i]) {
            g[i] += millivolts;
            return;
        }
        settle(i);
        g[i] += millivolts;
        if (std::max(u[i], g[i]) >= kGap)
            activate(i);
    }

    int motorOf(std::uint32_t i) const
    {
        const Group grp = static_cast<Group>(wiring->groups[i]);
        return grp == DNb05Left ? SteerLeft : grp == DNb05Right ? SteerRight : -1;
    }

    void step()
    {
        const Wiring& w = *wiring;
        const int slot = static_cast<int>(stepIndex % kDelaySteps);
        for (std::uint32_t i : touched[slot]) {
            receive(i, pending[slot][i]);
            pending[slot][i] = 0.f;
            isTouched[slot][i] = 0;
        }
        touched[slot].clear();

        for (int s = 0; s < SensorCount; ++s) {
            const float p = sensorRate[s] * kDt / 1000.f;
            if (p <= 0.f)
                continue;
            for (std::uint32_t i : w.members[sensorGroup[s]])
                if (uniform(rng) < p)
                    receive(i, kPoissonKick);
        }
        for (int m = 0; m < MotorCount; ++m)
            if (motorDrive[m] > 0.f)
                for (std::uint32_t i : w.members[motorGroup[m]])
                    if (!silenced[i]) {
                        settle(i);
                        activate(i);
                    }

        const float decay = std::exp(-kDt / kRateWindow);
        for (float& r : steerRate)
            r *= decay;

        nextActive.clear();
        for (std::uint32_t i : active) {
            if (silenced[i]) {
                u[i] = g[i] = 0.f;
                settledAt[i] = now + kDt;
                isActive[i] = 0;
                continue;
            }
            const int motor = motorOf(i);
            const float drive = motor >= 0 ? motorDrive[motor] : 0.f;

            if (now >= refractoryUntil[i]) {
                u[i] += (g[i] + drive - u[i]) * (kDt / kTauMembrane);
                g[i] -= g[i] * (kDt / kTauSynapse);
                if (u[i] > kGap) {
                    u[i] = 0.f;
                    g[i] = 0.f;
                    refractoryUntil[i] = now + kRefractory;
                    lastSpike[i] = now;
                    frameSpikes.push_back(i);
                    if (motor >= 0)
                        steerRate[motor] += 1000.f / kRateWindow / static_cast<float>(w.members[motorGroup[motor]].size());
                    for (std::uint32_t k = w.offsets[i]; k < w.offsets[i + 1]; ++k) {
                        const std::uint32_t t = w.targets[k];
                        if (!isTouched[slot][t]) {
                            isTouched[slot][t] = 1;
                            touched[slot].push_back(t);
                        }
                        pending[slot][t] += w.weights[k] * kSynapseWeight;
                    }
                }
            }
            const bool dormant = now + kDt >= refractoryUntil[i] && drive <= 0.f && std::max(u[i], g[i]) < kGap;
            if (dormant) {
                settledAt[i] = now + kDt;
                isActive[i] = 0;
            } else {
                nextActive.push_back(i);
            }
        }
        active.swap(nextActive);
        now += kDt;
        ++stepIndex;
    }
};
