#include <core/physics/collision_cache.h>
#include <core/physics/jolt_bridge.h>
#include <core/base/error.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <unordered_map>

namespace lumen {

struct CollisionCache::Impl
{
    struct Entry {
        JPH::RefConst<JPH::Shape> shape;
        CollisionType type = CollisionType::MESH;
    };
    std::unordered_map<Guid, Entry> entries;
};

static constexpr float MIN_EXTENT = 1e-3f;
static constexpr float PLACEMENT_EPSILON = 1e-6f;

static JPH::RefConst<JPH::Shape> _result(const JPH::ShapeSettings::ShapeResult& p_result, const char* p_what)
{
    if (p_result.HasError()) {
        log_write("CollisionCache: %s shape failed: %s", p_what, p_result.GetError().c_str());
        return nullptr;
    }
    return p_result.Get();
}

static JPH::RefConst<JPH::Shape> _place(JPH::RefConst<JPH::Shape> p_shape, const LCollision& p_collision)
{
    if (!p_shape) return nullptr;
    const bool moved = dot(p_collision.center, p_collision.center) > PLACEMENT_EPSILON;
    const bool rotated = std::abs(p_collision.rotation.w) < 1.0f - PLACEMENT_EPSILON;
    if (!moved && !rotated) return p_shape;
    JPH::RotatedTranslatedShapeSettings settings(jolt::to_jph(p_collision.center), jolt::to_jph(normalize(p_collision.rotation)), p_shape.GetPtr());
    return _result(settings.Create(), "placed");
}

static JPH::RefConst<JPH::Shape> _build(const LCollision& p_collision)
{
    switch (p_collision.type) {
        case CollisionType::MESH: {
            JPH::VertexList vertices;
            vertices.reserve(p_collision.vertices.size());
            for (const vec3& v : p_collision.vertices) vertices.push_back(JPH::Float3(v.x, v.y, v.z));
            JPH::IndexedTriangleList triangles;
            triangles.reserve(p_collision.triangle_count());
            for (size_t i = 0; i + 2 < p_collision.indices.size(); i += 3) triangles.push_back(JPH::IndexedTriangle(p_collision.indices[i], p_collision.indices[i + 1], p_collision.indices[i + 2]));
            JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles));
            return _result(settings.Create(), "mesh");
        }
        case CollisionType::HULL: {
            JPH::Array<JPH::Vec3> points;
            points.reserve(p_collision.vertices.size());
            for (const vec3& v : p_collision.vertices) points.push_back(jolt::to_jph(v));
            JPH::ConvexHullShapeSettings settings(points);
            return _result(settings.Create(), "hull");
        }
        case CollisionType::BOX: {
            const vec3 half = max(p_collision.half_extents, vec3(MIN_EXTENT));
            const float convex = std::min(JPH::cDefaultConvexRadius, std::min(half.x, std::min(half.y, half.z)) * 0.5f);
            JPH::BoxShapeSettings settings(jolt::to_jph(half), convex);
            return _place(_result(settings.Create(), "box"), p_collision);
        }
        case CollisionType::SPHERE:
            return _place(new JPH::SphereShape(std::max(p_collision.radius, MIN_EXTENT)), p_collision);
        case CollisionType::CAPSULE:
            return _place(new JPH::CapsuleShape(std::max(p_collision.half_height, MIN_EXTENT), std::max(p_collision.radius, MIN_EXTENT)), p_collision);
        default:
            return nullptr;
    }
}

CollisionCache::CollisionCache() : impl(std::make_unique<Impl>()) {}
CollisionCache::~CollisionCache() = default;

bool CollisionCache::load(Guid p_guid, const std::filesystem::path& p_content_bin)
{
    LCollision collision;
    if (collision.load_cooked(p_content_bin) != Error::OK) {
        log_write("CollisionCache: couldn't read '%s'.", p_content_bin.string().c_str());
        return false;
    }
    collision.guid = p_guid;
    return add(collision);
}

bool CollisionCache::add(const LCollision& p_collision)
{
    JPH::RefConst<JPH::Shape> shape = _build(p_collision);
    if (!shape) return false;
    impl->entries[p_collision.guid] = { shape, p_collision.type };
    version++;
    return true;
}

void CollisionCache::unload(Guid p_guid)
{
    if (impl->entries.erase(p_guid)) version++;
}

void CollisionCache::clear()
{
    if (impl->entries.empty()) return;
    impl->entries.clear();
    version++;
}

bool CollisionCache::has(Guid p_guid) const
{
    return impl->entries.contains(p_guid);
}

bool CollisionCache::find_type(Guid p_guid, CollisionType& r_type) const
{
    auto it = impl->entries.find(p_guid);
    if (it == impl->entries.end()) return false;
    r_type = it->second.type;
    return true;
}

namespace jolt {

const JPH::Shape* collision_shape(const CollisionCache& p_cache, Guid p_guid, CollisionType& r_type)
{
    auto it = p_cache.impl->entries.find(p_guid);
    if (it == p_cache.impl->entries.end()) return nullptr;
    r_type = it->second.type;
    return it->second.shape.GetPtr();
}

}

}
