#include <editor/play/shape_spawner.h>
#include <editor/assets/builtin_shapes.h>
#include <core/scene/components.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <iterator>

namespace lumen {

static constexpr const char* SPAWN_SHAPE_NAMES[] = { "Random", "Box", "Plank", "Crate", "Ball", "Barrel", "Capsule", "Log", "Boulder" };
static_assert(std::size(SPAWN_SHAPE_NAMES) == (size_t)SpawnShape::COUNT);

static constexpr vec3 PLANK_SIZE = vec3(1.8f, 0.1f, 0.34f);
static constexpr float CRATE_SIZE = 0.7f;
static constexpr float BARREL_RADIUS = 0.32f;
static constexpr float BARREL_HEIGHT = 0.9f;
static constexpr vec3 BOULDER_COLOR = vec3(0.62f, 0.60f, 0.57f);
static constexpr vec3 PLANK_COLOR = vec3(0.78f, 0.62f, 0.45f);
static constexpr vec3 CRATE_COLOR = vec3(0.72f, 0.55f, 0.36f);
static constexpr vec3 BARREL_COLOR = vec3(0.62f, 0.40f, 0.28f);
static constexpr vec3 LOG_COLOR = vec3(0.55f, 0.42f, 0.30f);
static constexpr float PASTEL_HUE = 0.55f;
static constexpr float PASTEL_DARKEN = 0.85f;

static vec3 _pastel(float p_hue)
{
    const vec3 c = clamp(abs(mod(vec3(p_hue * 6.0f) + vec3(0.0f, 4.0f, 2.0f), 6.0f) - 3.0f) - 1.0f, 0.0f, 1.0f);
    return mix(vec3(1.0f), c, PASTEL_HUE) * PASTEL_DARKEN;
}

static float _to_srgb(float p_linear)
{
    return p_linear <= 0.0031308f ? p_linear * 12.92f : 1.055f * std::pow(p_linear, 1.0f / 2.4f) - 0.055f;
}

static vec4 _tint(const vec3& p_linear)
{
    return vec4(_to_srgb(p_linear.x), _to_srgb(p_linear.y), _to_srgb(p_linear.z), 1.0f);
}

const char* spawn_shape_name(SpawnShape p_shape)
{
    return SPAWN_SHAPE_NAMES[std::min((size_t)p_shape, std::size(SPAWN_SHAPE_NAMES) - 1)];
}

Entity ShapeSpawner::spawn(Scene& r_world, SpawnShape p_shape, const vec3& p_hit)
{
    const auto range = [&](float p_lo, float p_hi) { return std::uniform_real_distribution<float>(p_lo, p_hi)(rng); };
    if (p_shape == SpawnShape::RANDOM || p_shape >= SpawnShape::COUNT) p_shape = (SpawnShape)std::uniform_int_distribution<int>(1, (int)SpawnShape::COUNT - 1)(rng);

    BuiltinMesh mesh = BuiltinMesh::CUBE;
    vec3 scale(1.0f);
    vec3 color = _pastel(range(0.0f, 1.0f));
    float density = WOOD_DENSITY;
    bool lying = false;
    switch (p_shape) {
        case SpawnShape::BOX:
            scale = vec3(range(0.4f, 0.9f), range(0.3f, 0.7f), range(0.4f, 0.9f));
            break;
        case SpawnShape::PLANK:
            scale = PLANK_SIZE;
            color = PLANK_COLOR;
            break;
        case SpawnShape::CRATE:
            scale = vec3(CRATE_SIZE);
            color = CRATE_COLOR;
            break;
        case SpawnShape::BALL:
            mesh = BuiltinMesh::SPHERE;
            scale = vec3(2.0f * range(0.25f, 0.45f));
            break;
        case SpawnShape::BARREL:
            mesh = BuiltinMesh::CYLINDER;
            scale = vec3(2.0f * BARREL_RADIUS, BARREL_HEIGHT, 2.0f * BARREL_RADIUS);
            color = BARREL_COLOR;
            break;
        case SpawnShape::CAPSULE:
            mesh = BuiltinMesh::CAPSULE;
            break;
        case SpawnShape::LOG: {
            mesh = BuiltinMesh::CYLINDER;
            const float d = 2.0f * range(0.17f, 0.24f);
            scale = vec3(d, range(1.8f, 3.0f), d);
            lying = true;
            color = LOG_COLOR;
            break;
        }
        case SpawnShape::BOULDER:
            mesh = (BuiltinMesh)((uint32_t)BuiltinMesh::BOULDER_0 + std::uniform_int_distribution<uint32_t>(0, BUILTIN_BOULDER_VARIANTS - 1)(rng));
            scale = vec3(range(0.3f, 0.55f));
            density = ROCK_DENSITY;
            color = BOULDER_COLOR;
            break;
        default:
            break;
    }

    const float heading = range(0.0f, glm::two_pi<float>());
    quat rotation;
    vec3 spin(0.0f);
    if (lying) {
        rotation = angleAxis(heading, vec3(0.0f, 1.0f, 0.0f)) * angleAxis(glm::half_pi<float>(), vec3(0.0f, 0.0f, 1.0f));
    } else {
        quat q;
        do q = quat(range(-1.0f, 1.0f), range(-1.0f, 1.0f), range(-1.0f, 1.0f), range(-1.0f, 1.0f));
        while (dot(q, q) < 0.01f || dot(q, q) > 1.0f);
        rotation = normalize(q);
        spin = vec3(range(-MAX_SPIN, MAX_SPIN), range(-MAX_SPIN, MAX_SPIN), range(-MAX_SPIN, MAX_SPIN));
    }

    const Entity e = r_world.create();
    TransformComponent xf;
    xf.position = p_hit + vec3(range(-DROP_JITTER, DROP_JITTER), DROP_HEIGHT, range(-DROP_JITTER, DROP_JITTER));
    xf.rotation = rotation;
    xf.scale = scale;
    r_world.add<TransformComponent>(e, xf);
    r_world.add<ModelComponent>(e, { builtin_model(mesh), _tint(color) });

    CollisionComponent col;
    col.motion = PhysicsMotion::DYNAMIC;
    col.mass = std::max(builtin_volume(mesh) * scale.x * scale.y * scale.z * density, MIN_MASS);
    if (builtin_hull(mesh)) {
        col.source = CollisionSource::MESH;
    } else {
        col.source = CollisionSource::SHAPES;
        col.shape_count = 1;
        col.shapes[0] = builtin_collision(mesh);
    }
    r_world.add<CollisionComponent>(e, col);
    if (spin != vec3(0.0f)) r_world.add<InitialVelocityComponent>(e, { vec3(0.0f), spin });
    return e;
}

}
