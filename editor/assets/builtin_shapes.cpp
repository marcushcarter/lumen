#include <editor/assets/builtin_shapes.h>
#include <editor/assets/import/mesh_cooker.h>
#include <editor/assets/import/collision_cooker.h>
#include <core/assets/asset_common.h>
#include <core/project/project.h>
#include <core/rendering/renderer.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <map>
#include <random>
#include <sstream>
#include <vector>

namespace lumen {

static constexpr uint64_t BUILTIN_VERSION = 2;
static constexpr uint64_t BUILTIN_MESH_BASE = 0x4C554D454E4D0000ull + (BUILTIN_VERSION << 8);
static constexpr uint64_t BUILTIN_MODEL_BASE = 0x4C554D454E4F0000ull + (BUILTIN_VERSION << 8);
static constexpr uint32_t ROUND_SEGMENTS = 32;
static constexpr uint32_t SPHERE_RINGS = 16;
static constexpr float HALF = 0.5f;

static constexpr uint32_t BOULDER_SEED = 7;
static constexpr uint32_t BOULDER_SUBDIVISIONS = 1;
static constexpr float BOULDER_LUMP = 0.22f;
static constexpr float BOULDER_LUMP_FREQ = 1.3f;
static constexpr float BOULDER_BUMP = 0.08f;
static constexpr float BOULDER_BUMP_FREQ = 3.1f;
static constexpr float BOULDER_FLAT_Y = -0.35f;
static constexpr float BOULDER_FLAT_KEEP = 0.3f;
static constexpr vec3 BOULDER_STRETCH = vec3(1.0f, 0.75f, 0.9f);
static constexpr float BOULDER_OFFSET_RANGE = 100.0f;

static constexpr const char* BUILTIN_NAMES[] = { "cube", "sphere", "cylinder", "capsule", "boulder0", "boulder1", "boulder2", "boulder3", "boulder4" };
static_assert(std::size(BUILTIN_NAMES) == (size_t)BuiltinMesh::COUNT);
static_assert((uint32_t)BuiltinMesh::COUNT - (uint32_t)BuiltinMesh::BOULDER_0 == BUILTIN_BOULDER_VARIANTS);

static float g_volumes[(size_t)BuiltinMesh::COUNT] = {};

struct ShapeMesh
{
    std::vector<vec3> positions;
    std::vector<vec3> normals;
    std::vector<vec2> uvs;
    std::vector<uvec3> triangles;

    uint32_t vertex(const vec3& p_position, const vec3& p_normal, const vec2& p_uv)
    {
        positions.push_back(p_position);
        normals.push_back(p_normal);
        uvs.push_back(p_uv);
        return (uint32_t)positions.size() - 1;
    }
};

static bool _is_boulder(BuiltinMesh p_mesh)
{
    return p_mesh >= BuiltinMesh::BOULDER_0 && p_mesh < BuiltinMesh::COUNT;
}

Guid builtin_mesh(BuiltinMesh p_mesh)
{
    Guid g;
    g.value = BUILTIN_MESH_BASE + (uint64_t)p_mesh + 1;
    return g;
}

Guid builtin_model(BuiltinMesh p_mesh)
{
    Guid g;
    g.value = BUILTIN_MODEL_BASE + (uint64_t)p_mesh + 1;
    return g;
}

bool builtin_hull(BuiltinMesh p_mesh)
{
    return _is_boulder(p_mesh);
}

float builtin_volume(BuiltinMesh p_mesh)
{
    return (size_t)p_mesh < std::size(g_volumes) ? g_volumes[(size_t)p_mesh] : 0.0f;
}

CollisionShapeEntry builtin_collision(BuiltinMesh p_mesh)
{
    CollisionShapeEntry s;
    s.half_extents = vec3(HALF);
    s.radius = HALF;
    s.half_height = HALF;
    s.top_radius = HALF;
    switch (p_mesh) {
        case BuiltinMesh::SPHERE: s.type = CollisionShapeType::SPHERE; break;
        case BuiltinMesh::CYLINDER: s.type = CollisionShapeType::CYLINDER; break;
        case BuiltinMesh::CAPSULE:
            s.type = CollisionShapeType::CAPSULE;
            s.radius = BUILTIN_CAPSULE_RADIUS;
            s.half_height = BUILTIN_CAPSULE_HALF_HEIGHT;
            break;
        default: s.type = CollisionShapeType::BOX; break;
    }
    return s;
}

static void _cube(ShapeMesh& r_mesh)
{
    static constexpr vec3 NORMALS[6] = { vec3(1, 0, 0), vec3(-1, 0, 0), vec3(0, 1, 0), vec3(0, -1, 0), vec3(0, 0, 1), vec3(0, 0, -1) };
    for (const vec3& n : NORMALS) {
        const vec3 u = std::abs(n.y) > 0.5f ? vec3(1, 0, 0) : vec3(0, 1, 0);
        const vec3 v = cross(n, u);
        const vec3 c = n * HALF;
        const uint32_t a = r_mesh.vertex(c - u * HALF - v * HALF, n, vec2(0, 0));
        const uint32_t b = r_mesh.vertex(c + u * HALF - v * HALF, n, vec2(1, 0));
        const uint32_t d = r_mesh.vertex(c + u * HALF + v * HALF, n, vec2(1, 1));
        const uint32_t e = r_mesh.vertex(c - u * HALF + v * HALF, n, vec2(0, 1));
        r_mesh.triangles.push_back(uvec3(a, b, d));
        r_mesh.triangles.push_back(uvec3(a, d, e));
    }
}

static void _rows(ShapeMesh& r_mesh, const std::vector<float>& p_theta, const std::vector<float>& p_offset, bool p_round, float p_radius)
{
    const uint32_t first = (uint32_t)r_mesh.positions.size();
    const uint32_t rows = (uint32_t)p_theta.size();
    for (uint32_t i = 0; i < rows; i++) {
        for (uint32_t j = 0; j <= ROUND_SEGMENTS; j++) {
            const float phi = glm::two_pi<float>() * (float)j / (float)ROUND_SEGMENTS;
            const float st = p_round ? std::sin(p_theta[i]) : 1.0f;
            const float ct = p_round ? std::cos(p_theta[i]) : 0.0f;
            const vec3 n(st * std::cos(phi), ct, st * std::sin(phi));
            r_mesh.vertex(n * p_radius + vec3(0.0f, p_offset[i], 0.0f), n, vec2((float)j / (float)ROUND_SEGMENTS, (float)i / (float)(rows - 1)));
        }
    }
    const uint32_t stride = ROUND_SEGMENTS + 1;
    for (uint32_t i = 0; i + 1 < rows; i++) {
        for (uint32_t j = 0; j < ROUND_SEGMENTS; j++) {
            const uint32_t a = first + i * stride + j;
            const uint32_t b = a + stride;
            const uint32_t c = b + 1;
            const uint32_t d = a + 1;
            r_mesh.triangles.push_back(uvec3(a, d, b));
            r_mesh.triangles.push_back(uvec3(d, c, b));
        }
    }
}

static void _cap(ShapeMesh& r_mesh, float p_y, bool p_top)
{
    const vec3 n(0.0f, p_top ? 1.0f : -1.0f, 0.0f);
    const uint32_t center = r_mesh.vertex(vec3(0.0f, p_y, 0.0f), n, vec2(0.5f));
    const uint32_t first = (uint32_t)r_mesh.positions.size();
    for (uint32_t j = 0; j <= ROUND_SEGMENTS; j++) {
        const float phi = glm::two_pi<float>() * (float)j / (float)ROUND_SEGMENTS;
        const vec2 d(std::cos(phi), std::sin(phi));
        r_mesh.vertex(vec3(d.x * HALF, p_y, d.y * HALF), n, d * 0.5f + 0.5f);
    }
    for (uint32_t j = 0; j < ROUND_SEGMENTS; j++) {
        if (p_top) r_mesh.triangles.push_back(uvec3(center, first + j + 1, first + j));
        else r_mesh.triangles.push_back(uvec3(center, first + j, first + j + 1));
    }
}

struct Noise3
{
    std::array<uint8_t, 512> perm{};

    explicit Noise3(uint32_t p_seed)
    {
        std::array<uint8_t, 256> p;
        for (uint32_t i = 0; i < 256; i++) p[i] = (uint8_t)i;
        std::shuffle(p.begin(), p.end(), std::mt19937(p_seed));
        for (uint32_t i = 0; i < 512; i++) perm[i] = p[i & 255];
    }

    static float _fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

    static float _grad(uint8_t p_hash, float x, float y, float z)
    {
        const uint8_t h = p_hash & 15;
        const float u = h < 8 ? x : y;
        const float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
        return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
    }

    float operator()(const vec3& p_p) const
    {
        const vec3 f = floor(p_p);
        const int xi = (int)f.x & 255;
        const int yi = (int)f.y & 255;
        const int zi = (int)f.z & 255;
        const vec3 d = p_p - f;
        const float u = _fade(d.x), v = _fade(d.y), w = _fade(d.z);
        const int a = perm[xi] + yi, aa = perm[a] + zi, ab = perm[a + 1] + zi;
        const int b = perm[xi + 1] + yi, ba = perm[b] + zi, bb = perm[b + 1] + zi;
        const auto lerp = [](float t, float p_a, float p_b) { return p_a + t * (p_b - p_a); };
        return lerp(w,
            lerp(v, lerp(u, _grad(perm[aa], d.x, d.y, d.z), _grad(perm[ba], d.x - 1, d.y, d.z)), lerp(u, _grad(perm[ab], d.x, d.y - 1, d.z), _grad(perm[bb], d.x - 1, d.y - 1, d.z))),
            lerp(v, lerp(u, _grad(perm[aa + 1], d.x, d.y, d.z - 1), _grad(perm[ba + 1], d.x - 1, d.y, d.z - 1)), lerp(u, _grad(perm[ab + 1], d.x, d.y - 1, d.z - 1), _grad(perm[bb + 1], d.x - 1, d.y - 1, d.z - 1))));
    }
};

static void _icosphere(uint32_t p_subdivisions, std::vector<vec3>& r_points, std::vector<uvec3>& r_faces)
{
    const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
    r_points = { vec3(-1, t, 0), vec3(1, t, 0), vec3(-1, -t, 0), vec3(1, -t, 0), vec3(0, -1, t), vec3(0, 1, t), vec3(0, -1, -t), vec3(0, 1, -t), vec3(t, 0, -1), vec3(t, 0, 1), vec3(-t, 0, -1), vec3(-t, 0, 1) };
    for (vec3& p : r_points) p = normalize(p);
    r_faces = { uvec3(0, 11, 5), uvec3(0, 5, 1), uvec3(0, 1, 7), uvec3(0, 7, 10), uvec3(0, 10, 11), uvec3(1, 5, 9), uvec3(5, 11, 4), uvec3(11, 10, 2), uvec3(10, 7, 6), uvec3(7, 1, 8),
                uvec3(3, 9, 4), uvec3(3, 4, 2), uvec3(3, 2, 6), uvec3(3, 6, 8), uvec3(3, 8, 9), uvec3(4, 9, 5), uvec3(2, 4, 11), uvec3(6, 2, 10), uvec3(8, 6, 7), uvec3(9, 8, 1) };
    for (uint32_t s = 0; s < p_subdivisions; s++) {
        std::map<uint64_t, uint32_t> mids;
        const auto mid = [&](uint32_t a, uint32_t b) {
            const uint64_t key = ((uint64_t)std::min(a, b) << 32) | std::max(a, b);
            const auto it = mids.find(key);
            if (it != mids.end()) return it->second;
            r_points.push_back(normalize(r_points[a] + r_points[b]));
            const uint32_t i = (uint32_t)r_points.size() - 1;
            mids[key] = i;
            return i;
        };
        std::vector<uvec3> next;
        for (const uvec3& f : r_faces) {
            const uint32_t ab = mid(f.x, f.y), bc = mid(f.y, f.z), ca = mid(f.z, f.x);
            next.insert(next.end(), { uvec3(f.x, ab, ca), uvec3(f.y, bc, ab), uvec3(f.z, ca, bc), uvec3(ab, bc, ca) });
        }
        r_faces = std::move(next);
    }
}

static float _boulder(uint32_t p_variant, ShapeMesh& r_mesh)
{
    std::mt19937 rng(BOULDER_SEED);
    std::uniform_real_distribution<float> offset(-BOULDER_OFFSET_RANGE, BOULDER_OFFSET_RANGE);
    vec3 o1(0.0f), o2(0.0f);
    for (uint32_t i = 0; i <= p_variant; i++) {
        o1 = vec3(offset(rng), offset(rng), offset(rng));
        o2 = vec3(offset(rng), offset(rng), offset(rng));
    }
    const Noise3 noise(BOULDER_SEED);

    std::vector<vec3> points;
    std::vector<uvec3> faces;
    _icosphere(BOULDER_SUBDIVISIONS, points, faces);
    vec3 lo(FLT_MAX), hi(-FLT_MAX);
    for (vec3& p : points) {
        const float n = 1.0f + BOULDER_LUMP * noise(p * BOULDER_LUMP_FREQ + o1) + BOULDER_BUMP * noise(p * BOULDER_BUMP_FREQ + o2);
        p *= n;
        if (p.y < BOULDER_FLAT_Y) p.y = BOULDER_FLAT_Y + (p.y - BOULDER_FLAT_Y) * BOULDER_FLAT_KEEP;
        p *= BOULDER_STRETCH;
        lo = min(lo, p);
        hi = max(hi, p);
    }
    const vec3 center = (lo + hi) * 0.5f;
    for (vec3& p : points) p -= center;

    float volume = 0.0f;
    for (const uvec3& f : faces) {
        vec3 a = points[f.x], b = points[f.y], c = points[f.z];
        vec3 n = cross(b - a, c - a);
        if (dot(n, a + b + c) < 0.0f) {
            std::swap(b, c);
            n = -n;
        }
        const float len = length(n);
        n = len > 0.0f ? n / len : vec3(0.0f, 1.0f, 0.0f);
        const uint32_t i0 = r_mesh.vertex(a, n, vec2(0.0f));
        const uint32_t i1 = r_mesh.vertex(b, n, vec2(1.0f, 0.0f));
        const uint32_t i2 = r_mesh.vertex(c, n, vec2(0.0f, 1.0f));
        r_mesh.triangles.push_back(uvec3(i0, i1, i2));
        volume += dot(a, cross(b, c)) / 6.0f;
    }
    return std::abs(volume);
}

static float _build(BuiltinMesh p_mesh, ShapeMesh& r_mesh)
{
    switch (p_mesh) {
        case BuiltinMesh::CUBE:
            _cube(r_mesh);
            return 1.0f;
        case BuiltinMesh::SPHERE: {
            std::vector<float> theta, offset;
            for (uint32_t i = 0; i <= SPHERE_RINGS; i++) {
                theta.push_back(glm::pi<float>() * (float)i / (float)SPHERE_RINGS);
                offset.push_back(0.0f);
            }
            _rows(r_mesh, theta, offset, true, HALF);
            return glm::pi<float>() / 6.0f;
        }
        case BuiltinMesh::CYLINDER:
            _rows(r_mesh, { 0.0f, 0.0f }, { HALF, -HALF }, false, HALF);
            _cap(r_mesh, HALF, true);
            _cap(r_mesh, -HALF, false);
            return glm::pi<float>() * HALF * HALF;
        case BuiltinMesh::CAPSULE: {
            std::vector<float> theta, offset;
            const uint32_t half_rings = SPHERE_RINGS / 2;
            for (uint32_t i = 0; i <= half_rings; i++) {
                theta.push_back(glm::pi<float>() * (float)i / (float)SPHERE_RINGS);
                offset.push_back(BUILTIN_CAPSULE_HALF_HEIGHT);
            }
            for (uint32_t i = half_rings; i <= SPHERE_RINGS; i++) {
                theta.push_back(glm::pi<float>() * (float)i / (float)SPHERE_RINGS);
                offset.push_back(-BUILTIN_CAPSULE_HALF_HEIGHT);
            }
            _rows(r_mesh, theta, offset, true, BUILTIN_CAPSULE_RADIUS);
            const float r = BUILTIN_CAPSULE_RADIUS;
            return glm::pi<float>() * r * r * 2.0f * BUILTIN_CAPSULE_HALF_HEIGHT + 4.0f / 3.0f * glm::pi<float>() * r * r * r;
        }
        default:
            return _boulder((uint32_t)p_mesh - (uint32_t)BuiltinMesh::BOULDER_0, r_mesh);
    }
}

static std::string _obj(const ShapeMesh& p_mesh)
{
    std::ostringstream ss;
    ss.precision(7);
    for (const vec3& p : p_mesh.positions) ss << "v " << p.x << ' ' << p.y << ' ' << p.z << '\n';
    for (const vec2& t : p_mesh.uvs) ss << "vt " << t.x << ' ' << t.y << '\n';
    for (const vec3& n : p_mesh.normals) ss << "vn " << n.x << ' ' << n.y << ' ' << n.z << '\n';
    for (const uvec3& t : p_mesh.triangles) {
        ss << 'f';
        for (int k = 0; k < 3; k++) {
            const uint32_t i = t[k] + 1;
            ss << ' ' << i << '/' << i << '/' << i;
        }
        ss << '\n';
    }
    return ss.str();
}

static bool _cooked(const std::filesystem::path& p_bin, Guid p_guid)
{
    LAssetHeader ah{};
    return read_asset_header(p_bin, ah) && ah.type == AssetType::MESH && ah.guid == p_guid;
}

static bool _cook(BuiltinMesh p_kind, const ShapeMesh& p_shape, const std::filesystem::path& p_lmesh, const std::filesystem::path& p_bin)
{
    const char* name = BUILTIN_NAMES[(size_t)p_kind];
    const std::string text = _obj(p_shape);
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFileFromMemory(text.data(), text.size(), MeshCooker::import_flags(), "obj");
    if (!scene) {
        log_write("Builtin: couldn't build the %s mesh (%s).", name, importer.GetErrorString());
        return false;
    }
    MeshCooker::Job job;
    job.source = std::filesystem::path(std::string("builtin:") + name);
    job.dst_lmesh = p_lmesh;
    job.content_bin = p_bin;
    job.guid = builtin_mesh(p_kind);
    job.settings.load_skeleton = false;
    if (builtin_hull(p_kind)) {
        job.settings.collision.type = CollisionType::HULL;
        LCollision collision;
        if (!CollisionCooker::build(scene, job.settings.collision, mat4(1.0f), collision)) {
            log_write("Builtin: couldn't build the %s collision hull.", name);
            return false;
        }
        collision.guid = job.guid;
        job.collision = collision.to_payload();
    }
    return MeshCooker::cook_scene(job, scene) == Error::OK;
}

void builtin_shapes_prepare(const Project& p_project, Renderer& r_renderer)
{
    const std::filesystem::path dir = p_project.editor_dir / "Builtin";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    for (uint32_t i = 0; i < (uint32_t)BuiltinMesh::COUNT; i++) {
        const BuiltinMesh kind = (BuiltinMesh)i;
        const Guid mesh = builtin_mesh(kind);
        const std::filesystem::path bin = p_project.content_path(mesh);
        std::filesystem::create_directories(bin.parent_path(), ec);
        ShapeMesh shape;
        g_volumes[i] = _build(kind, shape);
        if (!_cooked(bin, mesh) && !_cook(kind, shape, dir / (std::string(BUILTIN_NAMES[i]) + ".lmesh"), bin)) continue;
        if (r_renderer.geometry.find(mesh) == GeometryPool::INVALID_MESH) r_renderer.geometry.load(mesh, bin);
        r_renderer.models.set(builtin_model(kind), mesh, {});
    }
}

}
