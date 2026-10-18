#pragma once
#include <cstdint>
#include <string>
#include <filesystem>
#include <type_traits>
#include <glm/glm.hpp>

namespace lumen {

using namespace glm;

enum class MeshDagLevel : uint8_t { FLAT, CLUSTERED, CLUSTERED_DAG };

const char* mesh_dag_level_name(MeshDagLevel p_level);
MeshDagLevel mesh_dag_level_from_name(const std::string& p_name, MeshDagLevel p_fallback);

enum MeshFlags : uint32_t { MESH_FLAG_SKINNED = 1u << 0 };

static constexpr uint32_t CLUSTER_GROUP_NONE = 0xffffffffu;
static constexpr uint32_t LMESH_VERSION_INTERLEAVED = 1;
static constexpr uint32_t LMESH_VERSION_SLOT_TABLE = 2;
static constexpr uint32_t LMESH_VERSION = 3;

struct Vertex {
    u16vec3 position;
    i16vec2 normal;
    u16vec2 uv;
};

struct VertexPosition {
    u16vec3 position;
    uint16_t _pad;
};

struct VertexAttributes {
    i16vec2 normal;
    u16vec2 uv;
};

static_assert(sizeof(VertexPosition) == 8 && sizeof(VertexAttributes) == 8, "vertex streams must stay 8 B (one aligned 64-bit GPU load)");

struct SkinVertex {
    u8vec4 joints;
    u8vec4 weights;
};

static constexpr uint32_t BVH_LEAF_BIT = 0x80000000u;

struct BVHNode {
    vec3 bounds_min;
    uint32_t left;
    vec3 bounds_max;
    uint32_t right;
};

struct Cluster {
    uint32_t index_base;
    uint32_t index_count;
    vec4 cull_sphere;
    uint32_t self_group;
    uint32_t parent_group;
};

struct ClusterGroup {
    vec4 sphere;
    float error;
    uint32_t level;
};

struct LMeshPayloadHeader {
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t tri_count;
    uint32_t slot_count;
    uint32_t cluster_count;
    uint32_t group_count;
    uint32_t bvh_node_count;
    uint32_t flags;
    vec3 pos_min, pos_extent;
    vec2 uv_min, uv_extent;
    vec4 bounds_sphere;
};

static_assert(sizeof(LMeshPayloadHeader) == 88, "LMeshPayloadHeader layout changed");
static_assert(std::is_trivially_copyable_v<LMeshPayloadHeader>, "must be blittable");

struct LMeshStats
{
    uint64_t disk_size = 0;
    uint32_t vertices = 0;
    uint32_t triangles = 0;
    uint32_t slots = 0;
    uint32_t clusters = 0;
    uint64_t cluster_vertices = 0;
    uint64_t cluster_triangles = 0;
    uint32_t lods = 0;
    vec3 size = vec3(0.0f);
};

bool lmesh_read_stats(const std::filesystem::path& p_bin, LMeshStats& r_stats);

}