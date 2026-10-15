#include "Brain.hpp"
#include "Organism.hpp"
#include "World.hpp"

#include <SFML/Graphics.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#ifndef BIOGRID_ASSET_DIR
#define BIOGRID_ASSET_DIR "assets"
#endif

#ifndef BIOGRID_DATA_DIR
#define BIOGRID_DATA_DIR "data"
#endif

namespace {

constexpr unsigned kWindowWidth = 1200;
constexpr unsigned kWindowHeight = 720;
constexpr float kArenaSize = 720.f;
constexpr float kPanelLeft = kArenaSize;
constexpr float kPanelWidth = kWindowWidth - kArenaSize;
constexpr float kKeyDrive = 14.f;
constexpr float kManualSpeed = 190.f;
constexpr float kManualReverse = -70.f;
constexpr float kManualTurn = 3.4f;
constexpr float kAnatomyTop = 62.f;
constexpr float kAnatomyHeight = 236.f;
constexpr float kReadoutTop = 336.f;
constexpr float kRasterTop = 478.f;
constexpr float kRasterRow = 4.4f;
constexpr std::size_t kRasterColumns = 200;
constexpr float kRasterStep = 2.f;
constexpr float kRateScale = 150.f;
constexpr float kFlySpriteScale = 0.42f;
constexpr std::size_t kGlowFrames = 9;

namespace palette {
const sf::Color arenaFloor{30, 22, 18};
const sf::Color grid{44, 34, 28};
const sf::Color panel{8, 10, 14};
const sf::Color divider{40, 48, 62};
const sf::Color track{26, 31, 41};
const sf::Color text{205, 214, 226};
const sf::Color dim{105, 118, 138};
const sf::Color water{84, 186, 255};
const sf::Color shade{64, 120, 170};
const sf::Color heat{255, 98, 64};
const sf::Color body{232, 190, 96};
const sf::Color eye{196, 52, 46};
const sf::Color moist{84, 186, 255};
const sf::Color cool{120, 236, 222};
const sf::Color relay{255, 190, 84};
const sf::Color motor{224, 112, 255};
const sf::Color central{110, 128, 190};
const sf::Color visual{70, 92, 120};
const sf::Color sensory{90, 170, 150};
const sf::Color spike{255, 244, 214};
const sf::Color silenced{70, 74, 82};
}

sf::Color withAlpha(sf::Color c, float alpha)
{
    c.a = static_cast<sf::Uint8>(std::clamp(alpha, 0.f, 1.f) * 255.f);
    return c;
}

sf::Color mix(sf::Color a, sf::Color b, float t)
{
    t = std::clamp(t, 0.f, 1.f);
    auto lerp = [t](sf::Uint8 x, sf::Uint8 y) { return static_cast<sf::Uint8>(x + (y - x) * t); };
    return {lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b), lerp(a.a, b.a)};
}

sf::Vector2f screen(Vec2 v) { return {v.x, v.y}; }

void pushSegment(sf::VertexArray& tris, sf::Vector2f a, sf::Vector2f b, float thickness, sf::Color color)
{
    const sf::Vector2f d = b - a;
    const float len = std::hypot(d.x, d.y);
    if (len < 1e-3f)
        return;
    const sf::Vector2f n{-d.y / len * thickness * 0.5f, d.x / len * thickness * 0.5f};
    tris.append({a + n, color});
    tris.append({b + n, color});
    tris.append({b - n, color});
    tris.append({a + n, color});
    tris.append({b - n, color});
    tris.append({a - n, color});
}

void pushSquare(sf::VertexArray& quads, sf::Vector2f c, float half, sf::Color color)
{
    quads.append({{c.x - half, c.y - half}, color});
    quads.append({{c.x + half, c.y - half}, color});
    quads.append({{c.x + half, c.y + half}, color});
    quads.append({{c.x - half, c.y + half}, color});
}

void drawDisc(sf::RenderTarget& target, sf::Vector2f center, float radius, sf::Color fill,
              sf::Color outline = sf::Color::Transparent, float outlineWidth = 0.f, std::size_t points = 24)
{
    sf::CircleShape disc(radius, points);
    disc.setOrigin(radius, radius);
    disc.setPosition(center);
    disc.setFillColor(fill);
    disc.setOutlineColor(outline);
    disc.setOutlineThickness(outlineWidth);
    target.draw(disc);
}

void drawRect(sf::RenderTarget& target, sf::Vector2f pos, sf::Vector2f size, sf::Color fill)
{
    sf::RectangleShape rect(size);
    rect.setPosition(pos);
    rect.setFillColor(fill);
    target.draw(rect);
}

class Typewriter
{
public:
    explicit Typewriter(const sf::Font* font) : font(font) {}

    void write(sf::RenderTarget& target, const std::string& s, sf::Vector2f pos, unsigned size, sf::Color color,
               float anchor = 0.f) const
    {
        if (!font)
            return;
        sf::Text text(s, *font, size);
        text.setFillColor(color);
        const auto bounds = text.getLocalBounds();
        text.setPosition(std::round(pos.x - bounds.width * anchor), std::round(pos.y));
        target.draw(text);
    }

private:
    const sf::Font* font;
};

std::string grouped(long long value)
{
    std::string digits = std::to_string(value);
    for (int k = static_cast<int>(digits.size()) - 3; k > 0; k -= 3)
        digits.insert(static_cast<std::size_t>(k), ",");
    return digits;
}

bool loadFont(sf::Font& font)
{
    for (const std::string& path : {std::string(BIOGRID_ASSET_DIR) + "/DejaVuSansMono.ttf",
                                    std::string("assets/DejaVuSansMono.ttf"), std::string("../assets/DejaVuSansMono.ttf")})
        if (font.loadFromFile(path))
            return true;
    std::fprintf(stderr, "font not found, running without labels\n");
    return false;
}

std::optional<sf::Texture> loadTexture(const std::string& name)
{
    sf::Texture texture;
    for (const std::string& path : {std::string(BIOGRID_ASSET_DIR) + "/" + name, "assets/" + name, "../assets/" + name})
        if (texture.loadFromFile(path)) {
            texture.setSmooth(true);
            return texture;
        }
    return std::nullopt;
}

struct FlySprites
{
    std::optional<sf::Texture> body = loadTexture("fly_body.png");
    std::optional<sf::Texture> wing = loadTexture("fly_wing.png");
};

std::unique_ptr<Brain> loadBrain()
{
    std::string reason;
    for (const std::string& dir : {std::string("data"), std::string(BIOGRID_DATA_DIR), std::string("../data")}) {
        try {
            return std::make_unique<Brain>(dir);
        } catch (const std::exception& e) {
            reason = e.what();
        }
    }
    std::fprintf(stderr, "could not load the FlyWire brain: %s\n", reason.c_str());
    return nullptr;
}

sf::Color groupTone(Brain::Group grp)
{
    switch (grp) {
    case Brain::VP5Left:
    case Brain::VP5Right:
        return palette::moist;
    case Brain::VP3aLeft:
    case Brain::VP3aRight:
        return palette::cool;
    case Brain::Relay:
        return palette::relay;
    default:
        return palette::motor;
    }
}

sf::Color kindTone(Brain::Kind kind)
{
    switch (kind) {
    case Brain::Kind::Visual:
        return palette::visual;
    case Brain::Kind::Sensory:
        return palette::sensory;
    case Brain::Kind::Descending:
        return palette::motor;
    default:
        return palette::central;
    }
}

class BrainView
{
public:
    static constexpr std::array<Brain::Group, 7> kRasterGroups{
        Brain::VP5Left, Brain::VP5Right, Brain::VP3aLeft, Brain::VP3aRight, Brain::Relay, Brain::DNb05Left, Brain::DNb05Right};

    explicit BrainView(const Brain& brain) : brain(brain)
    {
        std::vector<float> xs, ys;
        for (std::uint32_t i = 0; i < brain.size(); ++i)
            if (brain.x(i) > 0.f || brain.y(i) > 0.f) {
                xs.push_back(brain.x(i));
                ys.push_back(brain.y(i));
            }
        auto quantile = [](std::vector<float>& v, float q) {
            auto at = v.begin() + static_cast<std::ptrdiff_t>(q * static_cast<float>(v.size() - 1));
            std::nth_element(v.begin(), at, v.end());
            return *at;
        };
        const float minX = quantile(xs, 0.0005f), maxX = quantile(xs, 0.9995f);
        const float minY = quantile(ys, 0.0005f), maxY = quantile(ys, 0.9995f);
        const float width = kPanelWidth - 40.f;
        const float scale = std::min(width / (maxX - minX), kAnatomyHeight / (maxY - minY));
        const float offsetX = kPanelLeft + 20.f + (width - (maxX - minX) * scale) * 0.5f;
        const float offsetY = kAnatomyTop + (kAnatomyHeight - (maxY - minY) * scale) * 0.5f;
        positions.reserve(brain.size());
        for (std::uint32_t i = 0; i < brain.size(); ++i) {
            const float px = std::clamp(brain.x(i), minX, maxX);
            const float py = std::clamp(brain.y(i), minY, maxY);
            positions.push_back({offsetX + (px - minX) * scale, offsetY + (py - minY) * scale});
        }

        cloud.create(kWindowWidth, kWindowHeight);
        cloud.clear(sf::Color::Transparent);
        sf::VertexArray points(sf::Points);
        for (std::uint32_t i = 0; i < brain.size(); ++i) {
            if (brain.x(i) < minX || brain.x(i) > maxX || brain.y(i) < minY || brain.y(i) > maxY)
                continue;
            const auto kind = brain.kind(i);
            const float alpha = kind == Brain::Kind::Visual ? 0.07f : kind == Brain::Kind::Central ? 0.14f : 0.18f;
            points.append({positions[i] + sf::Vector2f{0.5f, 0.5f}, withAlpha(kindTone(kind), alpha)});
        }
        cloud.draw(points, sf::BlendAdd);
        cloud.display();

        rowOf.assign(brain.size(), -1);
        float y = 0.f;
        for (Brain::Group grp : kRasterGroups) {
            const float height = brain.groupMembers(grp).size() == 1 ? kRasterRow * 2.5f : kRasterRow;
            for (std::uint32_t i : brain.groupMembers(grp)) {
                rowOf[i] = static_cast<int>(rows.size());
                rows.push_back(i);
                rowTop.push_back(y);
                rowHeight.push_back(height);
                y += height;
            }
        }
    }

    sf::Vector2f at(std::uint32_t i) const { return positions[i]; }
    const std::vector<std::uint32_t>& rasterRows() const { return rows; }
    int rasterRow(std::uint32_t i) const { return rowOf[i]; }
    float rowY(int r) const { return rowTop[static_cast<std::size_t>(r)]; }
    float rowH(int r) const { return rowHeight[static_cast<std::size_t>(r)]; }
    float rasterHeight() const { return rowTop.empty() ? 0.f : rowTop.back() + rowHeight.back(); }

    void drawAnatomy(sf::RenderTarget& target, const Typewriter& type, const std::deque<std::vector<std::uint32_t>>& recent) const
    {
        target.draw(sf::Sprite(cloud.getTexture()), sf::BlendAdd);

        sf::VertexArray sparks(sf::Quads);
        for (std::size_t f = recent.size() > kGlowFrames ? recent.size() - kGlowFrames : 0; f < recent.size(); ++f)
            for (std::uint32_t i : recent[f]) {
                const float glow = brain.glow(i);
                if (glow <= 0.f)
                    continue;
                const sf::Color tone = brain.group(i) != Brain::None ? groupTone(brain.group(i)) : mix(kindTone(brain.kind(i)), palette::spike, 0.45f);
                pushSquare(sparks, positions[i], 1.1f, withAlpha(tone, 0.4f * glow));
            }
        target.draw(sparks, sf::BlendAdd);

        for (Brain::Group grp : kRasterGroups)
            for (std::uint32_t i : brain.groupMembers(grp)) {
                const bool motor = grp == Brain::DNb05Left || grp == Brain::DNb05Right;
                const float radius = motor ? 4.5f : 2.4f;
                const sf::Color tone = brain.isSilenced(i) ? palette::silenced : groupTone(grp);
                const float glow = brain.isSilenced(i) ? 0.f : brain.glow(i);
                if (glow > 0.f)
                    drawDisc(target, positions[i], radius + 6.f * glow, withAlpha(tone, 0.3f * glow), sf::Color::Transparent, 0.f, 14);
                drawDisc(target, positions[i], radius, mix(withAlpha(tone, 0.55f), tone, glow), sf::Color::Transparent, 0.f, motor ? 20 : 10);
            }

        label(target, type, Brain::DNb05Left, "DNb05 L", {-4.f, 16.f}, 1.f);
        label(target, type, Brain::DNb05Right, "DNb05 R", {4.f, 16.f}, 0.f);
        label(target, type, Brain::Relay, "VP relay", {0.f, -22.f}, 0.5f);

        struct Key
        {
            sf::Color tone;
            const char* text;
        };
        const std::array<Key, 5> legend{{{palette::moist, "VP5 humid"}, {palette::cool, "VP3a cool"},
                                         {palette::relay, "VP relay"}, {palette::motor, "DNb05"},
                                         {mix(palette::central, palette::spike, 0.45f), "other"}}};
        float x = kPanelLeft + 24.f;
        const float y = kAnatomyTop + kAnatomyHeight + 10.f;
        for (const auto& key : legend) {
            drawDisc(target, {x + 4.f, y + 8.f}, 3.5f, key.tone);
            type.write(target, key.text, {x + 12.f, y}, 10, palette::dim);
            x += 88.f;
        }
    }

private:
    const Brain& brain;
    std::vector<sf::Vector2f> positions;
    sf::RenderTexture cloud;
    std::vector<std::uint32_t> rows;
    std::vector<int> rowOf;
    std::vector<float> rowTop, rowHeight;

    void label(sf::RenderTarget& target, const Typewriter& type, Brain::Group grp, const std::string& text,
               sf::Vector2f offset, float anchor) const
    {
        const auto& ids = brain.groupMembers(grp);
        if (ids.empty())
            return;
        sf::Vector2f sum{0.f, 0.f};
        for (std::uint32_t i : ids)
            sum += positions[i];
        type.write(target, text, sum / static_cast<float>(ids.size()) + offset - sf::Vector2f{0.f, 6.f}, 10, palette::text, anchor);
    }
};

void drawArena(sf::RenderTarget& target, const World& world, float time)
{
    drawRect(target, {0.f, 0.f}, {kArenaSize, kArenaSize}, palette::arenaFloor);

    sf::VertexArray lines(sf::Lines);
    for (float g = 40.f; g < kArenaSize; g += 40.f) {
        lines.append({{g, 0.f}, palette::grid});
        lines.append({{g, kArenaSize}, palette::grid});
        lines.append({{0.f, g}, palette::grid});
        lines.append({{kArenaSize, g}, palette::grid});
    }
    target.draw(lines);

    for (const auto& s : world.shades) {
        const sf::Vector2f c = screen(s.position);
        for (int ring = 4; ring >= 1; --ring)
            drawDisc(target, c, s.radius * (1.f + 0.22f * ring), withAlpha(palette::shade, 0.035f), sf::Color::Transparent, 0.f, 48);
        const float pulse = 1.f + 0.02f * std::sin(time * 1.3f + s.phase);
        drawDisc(target, c, s.radius * pulse, withAlpha(palette::shade, 0.28f), withAlpha(palette::cool, 0.35f), 1.5f, 64);
    }

    for (const auto& p : world.water) {
        const float pulse = 1.f + 0.1f * std::sin(time * 3.f + p.phase);
        const sf::Vector2f c = screen(p.position);
        drawDisc(target, c, World::kWaterRadius * 3.f * pulse, withAlpha(palette::water, 0.08f));
        drawDisc(target, c, World::kWaterRadius * pulse, palette::water);
        drawDisc(target, c + sf::Vector2f{-1.8f, -1.8f}, World::kWaterRadius * 0.35f, withAlpha(sf::Color::White, 0.7f));
    }

    for (const auto& r : world.ripples) {
        const float t = r.age / World::kRippleLife;
        drawDisc(target, screen(r.position), 10.f + t * 34.f, sf::Color::Transparent, withAlpha(palette::water, 1.f - t), 2.f);
    }
}

float groupGlow(const Brain& brain, Brain::Group grp)
{
    const auto& ids = brain.groupMembers(grp);
    float sum = 0.f;
    for (std::uint32_t i : ids)
        sum += brain.glow(i);
    return ids.empty() ? 0.f : std::min(1.f, 2.5f * sum / static_cast<float>(ids.size()));
}

void drawFly(sf::RenderTarget& target, const Organism& fly, const Brain& brain, const FlySprites& sprites)
{
    if (fly.trail.size() > 1) {
        sf::VertexArray trail(sf::LineStrip);
        const float n = static_cast<float>(fly.trail.size());
        float k = 0.f;
        for (const auto& p : fly.trail)
            trail.append({screen(p), withAlpha(palette::body, 0.3f * (++k / n))});
        trail.append({screen(fly.position), withAlpha(palette::body, 0.3f)});
        target.draw(trail);
    }

    const Vec2 forward = Vec2::fromAngle(fly.heading);
    const sf::Color tint = mix(sf::Color::White, palette::heat, fly.hurtFlash);
    const float scale = kFlySpriteScale * (1.f + 0.14f * fly.altitude);
    const float bodyAngle = fly.heading * 57.29578f + 90.f;

    sf::CircleShape shadow(9.f, 24);
    shadow.setOrigin(9.f, 9.f);
    shadow.setScale(1.f, 1.8f);
    shadow.setRotation(bodyAngle);
    shadow.setPosition(screen(fly.position) + sf::Vector2f{7.f, 10.f} * fly.altitude);
    shadow.setFillColor(withAlpha(sf::Color::Black, 0.45f - 0.2f * fly.altitude));
    target.draw(shadow);

    if (sprites.body) {
        sf::Sprite body(*sprites.body);
        const auto size = sprites.body->getSize();
        body.setOrigin(size.x * 0.5f, size.y * 0.5f);
        body.setScale(scale, scale);
        body.setRotation(bodyAngle);
        body.setPosition(screen(fly.position));
        body.setColor(tint);
        target.draw(body);
    } else {
        const Vec2 sideways = Vec2::fromAngle(fly.heading + 1.5707963f);
        const sf::Color shell = mix(palette::body, palette::heat, fly.hurtFlash);
        drawDisc(target, screen(fly.position - forward * 8.f), 7.f, mix(shell, palette::arenaFloor, 0.3f));
        drawDisc(target, screen(fly.position), 8.f, shell);
        drawDisc(target, screen(fly.position + forward * 8.f), 5.f, mix(shell, palette::arenaFloor, 0.15f));
        for (float s : {-1.f, 1.f})
            drawDisc(target, screen(fly.position + forward * 9.f + sideways * (s * 3.5f)), 2.4f, palette::eye);
    }

    if (sprites.wing) {
        const auto wingSize = sprites.wing->getSize();
        const float flap = 0.5f + 0.5f * std::sin(fly.wingPhase * 6.2831853f);
        for (float side : {-1.f, 1.f}) {
            const sf::Vector2f local{side * 5.5f * scale, -5.f * scale};
            const float radians = bodyAngle / 57.29578f;
            const sf::Vector2f hinge{local.x * std::cos(radians) - local.y * std::sin(radians),
                                     local.x * std::sin(radians) + local.y * std::cos(radians)};
            auto drawWing = [&](float spread, float alpha) {
                sf::Sprite wing(*sprites.wing);
                wing.setOrigin(wingSize.x * 0.5f, 5.f);
                wing.setScale(scale, scale);
                wing.setRotation(bodyAngle - side * spread);
                wing.setPosition(screen(fly.position) + hinge);
                wing.setColor(withAlpha(tint, alpha));
                target.draw(wing);
            };
            if (fly.flying()) {
                drawWing(48.f, 0.3f);
                drawWing(96.f, 0.3f);
                drawWing(48.f + 48.f * flap, 0.6f);
            } else {
                drawWing(16.f, 0.9f);
            }
        }
    }

    sf::VertexArray antennae(sf::Triangles);
    const Vec2 head = fly.position + forward * 11.f;
    for (auto side : {Organism::Left, Organism::Right}) {
        const bool left = side == Organism::Left;
        const float moist = groupGlow(brain, left ? Brain::VP5Left : Brain::VP5Right);
        const float cool = groupGlow(brain, left ? Brain::VP3aLeft : Brain::VP3aRight);
        const sf::Color tip = mix(mix(palette::dim, palette::moist, moist), palette::cool, cool * 0.8f);
        pushSegment(antennae, screen(head), screen(fly.antennaTip(side)), 1.2f, withAlpha(palette::dim, 0.7f));
        drawDisc(target, screen(fly.antennaTip(side)), 5.f + 5.f * std::max(moist, cool), withAlpha(tip, 0.25f));
        drawDisc(target, screen(fly.antennaTip(side)), 2.6f, tip);
    }
    target.draw(antennae);
}

std::string lesionSummary(const Brain& brain)
{
    std::string summary;
    auto add = [&summary](const char* name) {
        if (!summary.empty())
            summary += " + ";
        summary += name;
    };
    if (brain.lesioned(Brain::RelayNeurons))
        add("VP relay");
    if (brain.lesioned(Brain::SteeringNeurons))
        add("DNb05");
    if (brain.lesioned(Brain::MoistNeurons))
        add("VP5");
    if (brain.lesioned(Brain::CoolNeurons))
        add("VP3a");
    return summary;
}

void drawMeter(sf::RenderTarget& target, const Typewriter& type, const char* name, float y, float value, sf::Color low,
               sf::Color high)
{
    type.write(target, name, {24.f, y - 5.f}, 10, palette::dim);
    drawRect(target, {96.f, y}, {166.f, 7.f}, palette::grid);
    drawRect(target, {96.f, y}, {166.f * std::clamp(value, 0.f, 1.f), 7.f}, mix(low, high, value));
}

void drawStatus(sf::RenderTarget& target, const Typewriter& type, const World& world, const Organism& fly, const Brain& brain,
                bool autonomous)
{
    const std::string lesion = lesionSummary(brain);
    drawRect(target, {12.f, 12.f}, {262.f, lesion.empty() ? 116.f : 138.f}, withAlpha(palette::panel, 0.6f));
    type.write(target, autonomous ? "AUTONOMOUS  WHOLE BRAIN" : "MANUAL  WASD", {24.f, 20.f}, 15,
               autonomous ? palette::motor : palette::body);
    const char* doing = fly.activity == Organism::Activity::Drinking ? "DRINKING"
                        : fly.activity == Organism::Activity::Resting ? "RESTING"
                                                                      : "FLYING";
    type.write(target, doing, {262.f, 44.f}, 11, palette::dim, 1.f);

    char line[64];
    std::snprintf(line, sizeof line, "SCORE %4d   BEST %4d", world.score, world.best);
    type.write(target, line, {24.f, 44.f}, 13, palette::text);
    std::snprintf(line, sizeof line, "GEN   %4d   TIME %5.1fs", world.generation, world.lifetime);
    type.write(target, line, {24.f, 62.f}, 13, palette::dim);

    const float hydration = std::clamp(fly.energy / Organism::kMaxEnergy, 0.f, 1.f);
    drawMeter(target, type, "HYDRATION", 89.f, hydration, palette::heat, palette::water);
    drawMeter(target, type, "HEAT", 107.f, fly.heat, palette::cool, palette::heat);
    if (!lesion.empty())
        type.write(target, "SILENCED  " + lesion, {24.f, 122.f}, 11, palette::heat);

    type.write(target, "WASD fly   SPACE whole-brain autopilot   R reset   ESC quit", {kArenaSize * 0.5f, kArenaSize - 40.f}, 12,
               palette::dim, 0.5f);
    type.write(target, "silence  1 VP relay   2 DNb05   3 VP5 humid   4 VP3a cool   0 restore",
               {kArenaSize * 0.5f, kArenaSize - 22.f}, 12, palette::dim, 0.5f);
}

void drawReadout(sf::RenderTarget& target, const Typewriter& type, const Brain& brain, float spikeRate)
{
    const float left = kPanelLeft + 24.f;
    type.write(target, "STEERING OUTPUT", {left, kReadoutTop}, 11, palette::dim);

    const float barLeft = left + 64.f;
    const float barWidth = 170.f;
    const std::array<std::pair<const char*, Brain::Motor>, 2> bars{{{"DNb05 L", Brain::SteerLeft}, {"DNb05 R", Brain::SteerRight}}};
    for (std::size_t k = 0; k < bars.size(); ++k) {
        const float y = kReadoutTop + 24.f + k * 22.f;
        const float rate = brain.steeringRate(bars[k].second);
        type.write(target, bars[k].first, {left, y - 4.f}, 11, palette::text);
        drawRect(target, {barLeft, y}, {barWidth, 6.f}, palette::track);
        drawRect(target, {barLeft, y}, {barWidth * std::min(rate / kRateScale, 1.f), 6.f}, palette::motor);
        char hz[16];
        std::snprintf(hz, sizeof hz, "%3.0f Hz", rate);
        type.write(target, hz, {barLeft + barWidth + 8.f, y - 4.f}, 11, palette::dim);
    }

    const auto command = brain.decode();
    const float gaugeLeft = kPanelLeft + 360.f;
    const float gaugeWidth = 96.f;
    const float gaugeY = kReadoutTop + 40.f;
    type.write(target, "TURN", {gaugeLeft + gaugeWidth * 0.5f, kReadoutTop + 18.f}, 11, palette::dim, 0.5f);
    drawRect(target, {gaugeLeft, gaugeY}, {gaugeWidth, 8.f}, palette::track);
    const float t = std::clamp(command.turn / 4.f, -1.f, 1.f);
    const float mid = gaugeLeft + gaugeWidth * 0.5f;
    drawRect(target, {std::min(mid, mid + t * gaugeWidth * 0.5f), gaugeY}, {std::abs(t) * gaugeWidth * 0.5f, 8.f}, palette::motor);
    drawRect(target, {mid - 1.f, gaugeY - 4.f}, {2.f, 16.f}, palette::text);
    type.write(target, "L", {gaugeLeft - 4.f, gaugeY - 4.f}, 11, palette::dim, 1.f);
    type.write(target, "R", {gaugeLeft + gaugeWidth + 4.f, gaugeY - 4.f}, 11, palette::dim);

    char line[96];
    std::snprintf(line, sizeof line, "ACTIVE NEURONS %5zu      SPIKES %7s /s", brain.activeCount(),
                  grouped(static_cast<long long>(spikeRate)).c_str());
    type.write(target, line, {left, kReadoutTop + 84.f}, 11, palette::text);
}

void drawRaster(sf::RenderTarget& target, const Typewriter& type, const Brain& brain, const BrainView& view,
                const std::deque<std::vector<std::uint32_t>>& raster)
{
    const float left = kPanelLeft + 74.f;
    const auto& rows = view.rasterRows();
    char header[96];
    std::snprintf(header, sizeof header, "SPIKE RASTER  %zu NAMED NEURONS", rows.size());
    type.write(target, header, {kPanelLeft + 24.f, kRasterTop - 22.f}, 11, palette::dim);
    drawRect(target, {left, kRasterTop - 3.f}, {kRasterColumns * kRasterStep, view.rasterHeight() + 4.f},
             withAlpha(palette::track, 0.6f));

    int row = 0;
    const std::array<const char*, BrainView::kRasterGroups.size()> names{"VP5 L", "VP5 R", "VP3a L", "VP3a R", "relay", "DNb05 L", "DNb05 R"};
    for (std::size_t k = 0; k < BrainView::kRasterGroups.size(); ++k) {
        const int count = static_cast<int>(brain.groupMembers(BrainView::kRasterGroups[k]).size());
        const float top = view.rowY(row);
        const float bottom = view.rowY(row + count - 1) + view.rowH(row + count - 1);
        type.write(target, names[k], {left - 8.f, kRasterTop + (top + bottom) * 0.5f - 7.f}, 9, groupTone(BrainView::kRasterGroups[k]), 1.f);
        row += count;
    }

    sf::VertexArray ticks(sf::Triangles);
    float x = left;
    for (const auto& column : raster) {
        for (std::uint32_t i : column) {
            const int r = view.rasterRow(i);
            if (r < 0)
                continue;
            const float y = kRasterTop + view.rowY(r);
            pushSegment(ticks, {x + 0.5f, y}, {x + 0.5f, y + view.rowH(r) - 0.8f}, 1.f, groupTone(brain.group(i)));
        }
        x += kRasterStep;
    }
    target.draw(ticks);
}

void drawPanel(sf::RenderTarget& target, const Typewriter& type, const Brain& brain, const BrainView& view,
               const std::deque<std::vector<std::uint32_t>>& raster, float spikeRate)
{
    drawRect(target, {kPanelLeft, 0.f}, {kPanelWidth, static_cast<float>(kWindowHeight)}, palette::panel);
    drawRect(target, {kPanelLeft, 0.f}, {1.f, static_cast<float>(kWindowHeight)}, palette::divider);

    type.write(target, "FLYWIRE WHOLE BRAIN  " + grouped(static_cast<long long>(brain.size())) + " NEURONS",
               {kPanelLeft + 24.f, 14.f}, 14, palette::text);
    type.write(target, grouped(brain.synapseCount()) + " synapses  /  " + grouped(static_cast<long long>(brain.connectionCount())) +
                           " connections  /  LIF",
               {kPanelLeft + 24.f, 36.f}, 11, palette::dim);

    view.drawAnatomy(target, type, raster);
    drawReadout(target, type, brain, spikeRate);
    drawRaster(target, type, brain, view, raster);
}

}

int main()
{
    auto brain = loadBrain();
    if (!brain)
        return 1;

    sf::RenderWindow window(sf::VideoMode(kWindowWidth, kWindowHeight), "BioGrid", sf::Style::Titlebar | sf::Style::Close);
    window.setFramerateLimit(60);

    sf::Font font;
    const Typewriter type(loadFont(font) ? &font : nullptr);
    const BrainView view(*brain);
    const FlySprites sprites;

    World world(kArenaSize, kArenaSize);
    Organism fly;
    world.reset(fly);

    bool autonomous = false;
    std::deque<std::vector<std::uint32_t>> raster;
    sf::Clock clock;
    float time = 0.f;
    float spikeRate = 0.f;

    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed)
                window.close();
            if (event.type != sf::Event::KeyPressed)
                continue;
            switch (event.key.code) {
            case sf::Keyboard::Escape:
                window.close();
                break;
            case sf::Keyboard::Space:
                autonomous = !autonomous;
                break;
            case sf::Keyboard::Num1:
            case sf::Keyboard::Num2:
            case sf::Keyboard::Num3:
            case sf::Keyboard::Num4: {
                const auto lesion = static_cast<Brain::Lesion>(event.key.code - sf::Keyboard::Num1);
                brain->setLesion(lesion, !brain->lesioned(lesion));
                break;
            }
            case sf::Keyboard::Num0:
                for (int l = 0; l < Brain::LesionCount; ++l)
                    brain->setLesion(static_cast<Brain::Lesion>(l), false);
                break;
            case sf::Keyboard::R:
                world.reset(fly);
                brain->reset();
                raster.clear();
                clock.restart();
                break;
            default:
                break;
            }
        }

        const float dt = std::min(clock.restart().asSeconds(), 1.f / 30.f);
        time += dt;

        world.sense(fly);
        brain->feedSensor(Brain::MoistLeft, fly.moisture[Organism::Left]);
        brain->feedSensor(Brain::MoistRight, fly.moisture[Organism::Right]);
        brain->feedSensor(Brain::CoolLeft, fly.coolness[Organism::Left]);
        brain->feedSensor(Brain::CoolRight, fly.coolness[Organism::Right]);

        const bool focused = window.hasFocus();
        auto held = [focused](sf::Keyboard::Key a, sf::Keyboard::Key b) {
            return focused && (sf::Keyboard::isKeyPressed(a) || sf::Keyboard::isKeyPressed(b));
        };
        const bool up = !autonomous && held(sf::Keyboard::W, sf::Keyboard::Up);
        const bool down = !autonomous && held(sf::Keyboard::S, sf::Keyboard::Down);
        const bool left = !autonomous && held(sf::Keyboard::A, sf::Keyboard::Left);
        const bool right = !autonomous && held(sf::Keyboard::D, sf::Keyboard::Right);
        brain->driveMotor(Brain::SteerLeft, left ? kKeyDrive : 0.f);
        brain->driveMotor(Brain::SteerRight, right ? kKeyDrive : 0.f);

        brain->advance(dt * 1000.f);
        raster.push_back(brain->takeFrameSpikes());
        if (raster.size() > kRasterColumns)
            raster.pop_front();
        if (dt > 0.f)
            spikeRate += (static_cast<float>(raster.back().size()) / dt - spikeRate) * std::min(1.f, dt * 3.f);

        if (autonomous) {
            const auto command = brain->decode();
            fly.fly(command.speed, command.turn, dt);
        } else {
            const float target = up ? kManualSpeed : down ? kManualReverse : 0.f;
            fly.steer(target, (right - left) * kManualTurn, dt);
        }
        world.update(fly, dt);

        window.clear(palette::panel);
        drawArena(window, world, time);
        drawStatus(window, type, world, fly, *brain, autonomous);
        drawFly(window, fly, *brain, sprites);
        drawPanel(window, type, *brain, view, raster, spikeRate);
        window.display();
    }
}
