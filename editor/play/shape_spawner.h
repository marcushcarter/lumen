#pragma once
#include <core/scene/scene.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <random>

namespace lumen {

using namespace glm;

enum class SpawnShape : uint8_t { RANDOM, BOX, PLANK, CRATE, BALL, BARREL, CAPSULE, LOG, BOULDER, COUNT };

const char* spawn_shape_name(SpawnShape p_shape);

struct ShapeSpawner
{
    static constexpr float DROP_HEIGHT = 2.5f;
    static constexpr float DROP_JITTER = 0.2f;
    static constexpr float MAX_SPIN = 2.0f;
    static constexpr float WOOD_DENSITY = 500.0f;
    static constexpr float ROCK_DENSITY = 2500.0f;
    static constexpr float MIN_MASS = 1.0f;
    static constexpr float MISS_DISTANCE = 10.0f;
    static constexpr float REPEAT_SECONDS = 0.1f;

    std::mt19937 rng{ 0x4C554D45u };
    float repeat_timer = 0.0f;

    Entity spawn(Scene& r_world, SpawnShape p_shape, const vec3& p_hit);
};

}
