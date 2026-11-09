#include <core/assets/lcollision.h>
#include <core/assets/asset_common.h>
#include <cstring>

namespace lumen {

const char* collision_type_name(CollisionType p_type)
{
    switch (p_type) {
        case CollisionType::MESH: return "mesh";
        case CollisionType::HULL: return "hull";
        case CollisionType::BOX: return "box";
        case CollisionType::SPHERE: return "sphere";
        case CollisionType::CAPSULE: return "capsule";
        default: return "mesh";
    }
}

const char* collision_type_label(CollisionType p_type)
{
    switch (p_type) {
        case CollisionType::MESH: return "Triangle Mesh";
        case CollisionType::HULL: return "Convex Hull";
        case CollisionType::BOX: return "Box";
        case CollisionType::SPHERE: return "Sphere";
        case CollisionType::CAPSULE: return "Capsule";
        default: return "Triangle Mesh";
    }
}

CollisionType collision_type_from_name(const std::string& p_name, CollisionType p_fallback)
{
    for (uint32_t i = 0; i < (uint32_t)CollisionType::COUNT; i++) {
        if (p_name == collision_type_name((CollisionType)i)) return (CollisionType)i;
    }
    return p_fallback;
}

Error LCollision::load_cooked(const std::filesystem::path& p_path)
{
    using enum Error;
    LAssetHeader header{};
    std::vector<uint8_t> payload;
    if (!read_cooked_asset(p_path, AssetType::COLLISION, header, payload) || payload.size() < sizeof(LCollisionPayloadHeader)) return FAILED;

    LCollisionPayloadHeader ph;
    std::memcpy(&ph, payload.data(), sizeof(ph));
    const size_t vertex_bytes = (size_t)ph.vertex_count * sizeof(vec3);
    const size_t index_bytes = (size_t)ph.triangle_count * 3 * sizeof(uint32_t);
    if (ph.type >= (uint32_t)CollisionType::COUNT || payload.size() != sizeof(ph) + vertex_bytes + index_bytes) return FAILED;

    *this = LCollision{};
    guid = header.guid;
    type = (CollisionType)ph.type;
    source_triangles = ph.source_triangle_count;
    bounds_min = vec3(ph.bounds_min);
    bounds_max = vec3(ph.bounds_max);
    center = vec3(ph.center);
    radius = ph.center.w;
    rotation = quat(ph.rotation.w, ph.rotation.x, ph.rotation.y, ph.rotation.z);
    half_extents = vec3(ph.half_extents);
    half_height = ph.half_extents.w;
    vertices.resize(ph.vertex_count);
    indices.resize((size_t)ph.triangle_count * 3);
    const uint8_t* p = payload.data() + sizeof(ph);
    if (vertex_bytes) std::memcpy(vertices.data(), p, vertex_bytes);
    if (index_bytes) std::memcpy(indices.data(), p + vertex_bytes, index_bytes);
    for (uint32_t i : indices) {
        if (i >= ph.vertex_count) return FAILED;
    }
    return OK;
}

Error LCollision::save_cooked(const std::filesystem::path& p_path) const
{
    LCollisionPayloadHeader ph{};
    ph.type = (uint32_t)type;
    ph.vertex_count = (uint32_t)vertices.size();
    ph.triangle_count = triangle_count();
    ph.source_triangle_count = source_triangles;
    ph.bounds_min = vec4(bounds_min, 0.0f);
    ph.bounds_max = vec4(bounds_max, 0.0f);
    ph.center = vec4(center, radius);
    ph.rotation = vec4(rotation.x, rotation.y, rotation.z, rotation.w);
    ph.half_extents = vec4(half_extents, half_height);

    const size_t vertex_bytes = vertices.size() * sizeof(vec3);
    const size_t index_bytes = (size_t)ph.triangle_count * 3 * sizeof(uint32_t);
    std::vector<uint8_t> payload(sizeof(ph) + vertex_bytes + index_bytes);
    std::memcpy(payload.data(), &ph, sizeof(ph));
    if (vertex_bytes) std::memcpy(payload.data() + sizeof(ph), vertices.data(), vertex_bytes);
    if (index_bytes) std::memcpy(payload.data() + sizeof(ph) + vertex_bytes, indices.data(), index_bytes);
    return write_cooked_asset(p_path, AssetType::COLLISION, guid, payload.data(), payload.size()) ? Error::OK : Error::FAILED;
}

}
