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

enum class MeshFlags : uint32_t { NONE = 0, SKINNED = 1u << 0, WIDE_SLOTS = 1u << 1 };
LUMEN_ENUM_FLAGS(MeshFlags)

static constexpr uint32_t CLUSTER_GROUP_NONE = 0xffffffffu;
static constexpr uint32_t LMESH_VERSION_INTERLEAVED = 1;
static constexpr uint32_t LMESH_VERSION_SLOT_TABLE = 2;
static constexpr uint32_t LMESH_VERSION_NO_RIG = 3;
static constexpr uint32_t LMESH_VERSION_RIG = 4;
static constexpr uint32_t LMESH_VERSION_PAGED = 5;
static constexpr uint32_t LMESH_VERSION_STREAMED = 6;
static constexpr uint32_t LMESH_VERSION = LMESH_VERSION_STREAMED;

static constexpr uint32_t LMESH_CLUSTER_MAX_TRIS = 256;
static constexpr uint32_t LMESH_CLUSTER_MAX_VERTS = 256;
static constexpr uint32_t LMESH_LOCAL_SLOT_OVERFLOW = 255;
static constexpr uint32_t LMESH_CLUSTER_SLOT_MIXED = 0xFFFFFFFFu;
static constexpr uint32_t LMESH_PAGE_BYTES = 64u * 1024;
static constexpr uint32_t LMESH_PART_MAX_CLUSTERS = 32;
static constexpr uint32_t LMESH_NODE_FANOUT = 8;
static constexpr uint32_t LMESH_NODE_INNER = 0xFFFFFFFFu;

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

struct ClusterVertex
{
    VertexPosition position;
    VertexAttributes attributes;
};

static_assert(sizeof(ClusterVertex) == 16, "ClusterVertex must match the GPU cluster vertex stride");

static constexpr uint32_t MESH_MAX_JOINTS = 256;

struct SkinVertex
{
    u8vec4 joints;
    u8vec4 weights;
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
    uint32_t flags;
    uint32_t slot_count;
    uint32_t source_vertex_count;
    uint32_t lod0_tri_count;
    uint32_t cluster_count;
    uint32_t tri_count;
    uint32_t vertex_count;
    uint32_t group_count;
    uint32_t node_count;
    uint32_t page_count;
    uint32_t root_page_count;
    uint32_t level_count;
    uint32_t section_count;
    uint32_t group_child_page_count;
    uint32_t _pad[2];
    vec3 pos_min, pos_extent;
    vec2 uv_min, uv_extent;
    vec4 bounds_sphere;
};

static_assert(sizeof(LMeshPayloadHeader) == 120, "LMeshPayloadHeader layout changed");
static_assert(std::is_trivially_copyable_v<LMeshPayloadHeader>, "must be blittable");

struct LMeshCluster
{
    vec4 cull_sphere;
    uint32_t self_group;
    uint32_t parent_group;
    uint32_t tri_offset;
    uint32_t vertex_offset;
    uint32_t tri_count;
    uint32_t vertex_count;
    uint32_t slot;
    uint32_t bounds[3];
};

static_assert(sizeof(LMeshCluster) == 56, "LMeshCluster layout changed");

struct LMeshPage
{
    uint64_t offset;
    uint32_t size;
    uint32_t cluster_first;
    uint32_t cluster_count;
    uint32_t tri_first;
    uint32_t tri_count;
    uint32_t vertex_first;
    uint32_t vertex_count;
    uint32_t tri_bytes;
    uint32_t vertex_bytes;
    uint32_t skin_bytes;
};

static_assert(sizeof(LMeshPage) == 48, "LMeshPage layout changed");

struct LMeshNode
{
    vec4 cull_sphere;
    vec4 lod_sphere;
    float max_parent_error;
    uint32_t first;
    uint32_t count;
    uint32_t page;
};

static_assert(sizeof(LMeshNode) == 48, "LMeshNode layout changed");

enum class LMeshSectionType : uint32_t { RASTER_RESIDENT = 1, RASTER_PAGES = 2, RT_PROXY = 3, SDF = 4, COLLISION = 5, JOINT_BOXES = 6 };

struct LMeshJointBox
{
    vec3 min;
    vec3 max;
};

static_assert(sizeof(LMeshJointBox) == 24, "LMeshJointBox layout changed");

struct LMeshSection
{
    LMeshSectionType type;
    uint32_t _pad;
    uint64_t offset;
    uint64_t size;
};

static_assert(sizeof(LMeshSection) == 24, "LMeshSection layout changed");

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

struct LMeshSource
{
    std::vector<VertexPosition> positions;
    std::vector<VertexAttributes> attributes;
    std::vector<SkinVertex> skin;
    std::vector<uint32_t> indices;
    std::vector<uint32_t> tri_slots;
    std::vector<Cluster> clusters;
    std::vector<ClusterGroup> groups;
    uint32_t slot_count = 0;
    vec3 pos_min = vec3(0.0f), pos_extent = vec3(0.0f);
    vec2 uv_min = vec2(0.0f), uv_extent = vec2(0.0f);
    vec4 bounds_sphere = vec4(0.0f);
    LMeshRig rig;
};

struct LMeshData
{
    LMeshPayloadHeader header{};
    LMeshRig rig;
    std::vector<LMeshSection> sections;
    std::vector<ClusterGroup> groups;
    std::vector<LMeshNode> nodes;
    std::vector<LMeshPage> pages;
    std::vector<LMeshCluster> clusters;
    std::vector<uint32_t> group_child_first;
    std::vector<uint32_t> group_child_pages;
    std::vector<uint32_t> local_tris;
    std::vector<ClusterVertex> vertices;
    std::vector<SkinVertex> skin;
    std::vector<uint32_t> tri_slots;
    std::vector<uint8_t> collision;
    std::vector<LMeshJointBox> joint_boxes;

    bool skinned() const { return (header.flags & (uint32_t)MeshFlags::SKINNED) != 0; }
    bool wide_slots() const { return (header.flags & (uint32_t)MeshFlags::WIDE_SLOTS) != 0; }
    bool has_page_data() const { return local_tris.size() == header.tri_count && vertices.size() == header.vertex_count; }
};

struct LMeshPageData
{
    std::vector<uint32_t> tris;
    std::vector<ClusterVertex> vertices;
    std::vector<SkinVertex> skin;
    std::vector<uint32_t> tri_slots;
};

static constexpr uint32_t LMESH_NORMAL_BITS = 12;
static constexpr uint32_t LMESH_WEIGHT_BITS = 8;
static constexpr uint32_t LMESH_STREAM_PAD_WORDS = 2;
static constexpr uint32_t LMESH_CONE_NONE = 0x7F000000u;

struct LMeshClusterStreams
{
    uint32_t position;
    uint32_t attribute;
    uint32_t skin;
    uint32_t pos_lo_xy;
    uint32_t pos_lo_z_count;
    uint32_t uv_lo;
    uint32_t bits;
    uint32_t skin_format;
};

static_assert(sizeof(LMeshClusterStreams) == 32, "LMeshClusterStreams must match the GPU ClusterStreams layout");

struct LMeshPackedPage
{
    std::vector<uint32_t> positions;
    std::vector<uint32_t> attributes;
    std::vector<uint32_t> skin;
    std::vector<LMeshClusterStreams> clusters;
    std::vector<uint32_t> cones;
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

const char* lmesh_build(const LMeshSource& p_src, LMeshData& r_data);
const char* lmesh_validate_tables(const LMeshData& p_data);
const char* lmesh_validate_page(const LMeshData& p_tables, uint32_t p_page, const LMeshPageData& p_data);
const char* lmesh_validate(const LMeshData& p_data);
bool lmesh_write(const std::filesystem::path& p_bin, Guid p_guid, const LMeshData& p_data);
bool lmesh_open(const std::filesystem::path& p_bin, LMeshData& r_tables, uint32_t& r_version, std::string& r_error);
const char* lmesh_decode_page(const uint8_t* p_blob, size_t p_size, const LMeshPage& p_page, uint32_t p_flags, LMeshPageData& r_out);
void lmesh_page_slice(const LMeshData& p_data, uint32_t p_page, LMeshPageData& r_out);
void lmesh_pack_page(const LMeshData& p_tables, uint32_t p_page, const LMeshPageData& p_data, LMeshPackedPage& r_out);
bool lmesh_load(const std::filesystem::path& p_bin, LMeshData& r_data, std::string& r_error);

bool lmesh_read_stats(const std::filesystem::path& p_bin, LMeshStats& r_stats);
size_t lmesh_rig_bytes(const LMeshRigHeader& p_header);
bool lmesh_parse_rig(const uint8_t* p_data, size_t p_size, LMeshRig& r_rig);
bool lmesh_read_rig(const std::filesystem::path& p_bin, LMeshRig& r_rig);
bool lmesh_set_skeleton(const std::filesystem::path& p_bin, Guid p_skeleton);

enum class LMeshCollisionWrite : uint8_t { OK, NO_SLOT, FAILED };
bool lmesh_read_collision(const std::filesystem::path& p_bin, std::vector<uint8_t>& r_collision);
bool lmesh_has_collision_slot(const std::filesystem::path& p_bin);
LMeshCollisionWrite lmesh_write_collision(const std::filesystem::path& p_src, const std::filesystem::path& p_dst, const std::vector<uint8_t>& p_collision);

}
