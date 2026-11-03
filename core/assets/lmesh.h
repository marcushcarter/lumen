#pragma once
#include <cstdint>
#include <string>
#include <filesystem>
#include <vector>
#include <core/assets/guid.h>
#include <type_traits>
#include <core/base/enum_flags.h>
#include <glm/glm.hpp>

namespace lumen {

using namespace glm;

enum class MeshDagLevel : uint8_t { FLAT, CLUSTERED, CLUSTERED_DAG };

const char* mesh_dag_level_name(MeshDagLevel p_level);
MeshDagLevel mesh_dag_level_from_name(const std::string& p_name, MeshDagLevel p_fallback);

enum class MeshFlags : uint32_t { NONE = 0, SKINNED = 1u << 0 };
LUMEN_ENUM_FLAGS(MeshFlags)

static constexpr uint32_t CLUSTER_GROUP_NONE = 0xffffffffu;
static constexpr uint32_t LMESH_VERSION_INTERLEAVED = 1;
static constexpr uint32_t LMESH_VERSION_SLOT_TABLE = 2;
static constexpr uint32_t LMESH_VERSION_NO_RIG = 3;
static constexpr uint32_t LMESH_VERSION = 4;

struct Vertex
{
    u16vec3 position;
    i16vec2 normal;
    u16vec2 uv;
};

struct VertexPosition
{
    u16vec3 position;
    uint16_t _pad;
};

struct VertexAttributes
{
    i16vec2 normal;
    u16vec2 uv;
};

static_assert(sizeof(VertexPosition) == 8 && sizeof(VertexAttributes) == 8, "vertex streams must stay 8 B (one aligned 64-bit GPU load)");

static constexpr uint32_t MESH_MAX_JOINTS = 256;

struct SkinVertex
{
    u8vec4 joints;
    u8vec4 weights;
};

static constexpr uint32_t BVH_LEAF_BIT = 0x80000000u;

struct BVHNode
{
    vec3 bounds_min;
    uint32_t left;
    vec3 bounds_max;
    uint32_t right;
};

struct Cluster
{
    uint32_t index_base;
    uint32_t index_count;
    vec4 cull_sphere;
    uint32_t self_group;
    uint32_t parent_group;
};

struct ClusterGroup
{
    vec4 sphere;
    float error;
    uint32_t level;
};

struct LMeshPayloadHeader
{
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

static constexpr uint32_t LMESH_RIG_ALIGN = 16;

struct LMeshRigHeader
{
    Guid skeleton;
    uint32_t joint_count;
    uint32_t name_bytes;
};

static_assert(sizeof(LMeshRigHeader) == 16, "LMeshRigHeader layout changed");

struct LMeshRig
{
    Guid skeleton;
    std::vector<std::string> joints;
    std::vector<mat4> inverse_binds;
};

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
    uint32_t joints = 0;
    vec3 size = vec3(0.0f);
};

bool lmesh_read_stats(const std::filesystem::path& p_bin, LMeshStats& r_stats);
size_t lmesh_rig_bytes(const LMeshRigHeader& p_header);
bool lmesh_parse_rig(const uint8_t* p_data, size_t p_size, LMeshRig& r_rig);
bool lmesh_read_rig(const std::filesystem::path& p_bin, LMeshRig& r_rig);
bool lmesh_set_skeleton(const std::filesystem::path& p_bin, Guid p_skeleton);

}
