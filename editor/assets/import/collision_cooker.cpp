#include <Jolt/Jolt.h>
#include <Jolt/Geometry/ConvexHullBuilder.h>
#include <editor/assets/import/collision_cooker.h>
#include <editor/assets/import/model_cooker.h>
#include <editor/application/editor_context.h>
#include <core/assets/asset_common.h>
#include <core/base/jobs.h>
#include <core/io/file_io.h>
#include <core/project/project.h>
#include <assimp/config.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <meshoptimizer.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <toml++/toml.hpp>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

namespace lumen {

static constexpr float DEGENERATE_AREA = 1e-12f;
static constexpr float MIN_HULL_TOLERANCE = 1e-4f;

static bool _material_excluded(const aiMaterial* p_mat)
{
    aiString mode;
    if (p_mat->Get("$mat.gltf.alphaMode", 0, 0, mode) == AI_SUCCESS) {
        if (std::strcmp(mode.C_Str(), "MASK") == 0 || std::strcmp(mode.C_Str(), "BLEND") == 0) return true;
    }
    return p_mat->GetTextureCount(aiTextureType_OPACITY) > 0;
}

static void _gather(const aiScene* p_scene, const aiNode* p_node, const aiMatrix4x4& p_parent, const std::vector<bool>& p_excluded, std::vector<vec3>& r_positions, std::vector<uint32_t>& r_indices)
{
    const aiMatrix4x4 world = p_parent * p_node->mTransformation;
    const bool mirrored = aiMatrix3x3(world).Determinant() < 0.0f;

    for (uint32_t i = 0; i < p_node->mNumMeshes; i++) {
        const aiMesh* mesh = p_scene->mMeshes[p_node->mMeshes[i]];
        if (mesh->mMaterialIndex < p_excluded.size() && p_excluded[mesh->mMaterialIndex]) continue;
        const uint32_t base = (uint32_t)r_positions.size();
        for (uint32_t v = 0; v < mesh->mNumVertices; v++) {
            const aiVector3D p = world * mesh->mVertices[v];
            r_positions.push_back(vec3(p.x, p.y, p.z));
        }
        for (uint32_t f = 0; f < mesh->mNumFaces; f++) {
            const aiFace& face = mesh->mFaces[f];
            if (face.mNumIndices != 3) continue;
            r_indices.push_back(base + face.mIndices[0]);
            r_indices.push_back(base + face.mIndices[mirrored ? 2 : 1]);
            r_indices.push_back(base + face.mIndices[mirrored ? 1 : 2]);
        }
    }
    for (uint32_t c = 0; c < p_node->mNumChildren; c++) _gather(p_scene, p_node->mChildren[c], world, p_excluded, r_positions, r_indices);
}

static void _apply_transform(const MeshCooker::CookSettings& p_transform, std::vector<vec3>& r_positions)
{
    const bool rotate = p_transform.rotation != vec3(0.0f);
    const mat3 rot = mat3_cast(quat(radians(p_transform.rotation)));
    for (vec3& p : r_positions) {
        p *= p_transform.scale;
        if (rotate) p = rot * p;
        p += p_transform.translation;
    }
}

static void _weld(std::vector<vec3>& r_positions, std::vector<uint32_t>& r_indices)
{
    std::vector<unsigned int> remap(r_positions.size());
    const size_t unique = meshopt_generateVertexRemap(remap.data(), r_indices.data(), r_indices.size(), r_positions.data(), r_positions.size(), sizeof(vec3));
    std::vector<vec3> welded(unique);
    meshopt_remapVertexBuffer(welded.data(), r_positions.data(), r_positions.size(), sizeof(vec3), remap.data());
    meshopt_remapIndexBuffer(r_indices.data(), r_indices.data(), r_indices.size(), remap.data());
    r_positions = std::move(welded);
}

static void _drop_degenerate(const std::vector<vec3>& p_positions, std::vector<uint32_t>& r_indices)
{
    size_t out = 0;
    for (size_t t = 0; t + 2 < r_indices.size(); t += 3) {
        const uint32_t a = r_indices[t], b = r_indices[t + 1], c = r_indices[t + 2];
        if (a == b || b == c || a == c) continue;
        const vec3 n = cross(p_positions[b] - p_positions[a], p_positions[c] - p_positions[a]);
        if (dot(n, n) <= DEGENERATE_AREA) continue;
        r_indices[out++] = a;
        r_indices[out++] = b;
        r_indices[out++] = c;
    }
    r_indices.resize(out);
}

static void _compact(std::vector<vec3>& r_positions, std::vector<uint32_t>& r_indices)
{
    std::vector<uint32_t> map(r_positions.size(), UINT32_MAX);
    std::vector<vec3> kept;
    kept.reserve(r_positions.size());
    for (uint32_t& i : r_indices) {
        if (map[i] == UINT32_MAX) {
            map[i] = (uint32_t)kept.size();
            kept.push_back(r_positions[i]);
        }
        i = map[i];
    }
    r_positions = std::move(kept);
}

static uint32_t _root(std::vector<uint32_t>& r_parent, uint32_t p_v)
{
    while (r_parent[p_v] != p_v) {
        r_parent[p_v] = r_parent[r_parent[p_v]];
        p_v = r_parent[p_v];
    }
    return p_v;
}

static void _cull_islands(const std::vector<vec3>& p_positions, std::vector<uint32_t>& r_indices, float p_min_feature)
{
    if (p_min_feature <= 0.0f || r_indices.empty()) return;
    std::vector<uint32_t> parent(p_positions.size());
    std::iota(parent.begin(), parent.end(), 0u);
    for (size_t t = 0; t + 2 < r_indices.size(); t += 3) {
        const uint32_t a = _root(parent, r_indices[t]);
        const uint32_t b = _root(parent, r_indices[t + 1]);
        const uint32_t c = _root(parent, r_indices[t + 2]);
        parent[b] = a;
        parent[_root(parent, c)] = a;
    }

    std::vector<vec3> lo(p_positions.size(), vec3(FLT_MAX)), hi(p_positions.size(), vec3(-FLT_MAX));
    for (uint32_t i : r_indices) {
        const uint32_t r = _root(parent, i);
        lo[r] = min(lo[r], p_positions[i]);
        hi[r] = max(hi[r], p_positions[i]);
    }
    auto island_size = [&](uint32_t p_root) {
        const vec3 e = hi[p_root] - lo[p_root];
        return std::max(e.x, std::max(e.y, e.z));
    };

    uint32_t largest = UINT32_MAX;
    float largest_size = -1.0f;
    for (uint32_t i : r_indices) {
        const uint32_t r = _root(parent, i);
        const float s = island_size(r);
        if (s > largest_size) {
            largest_size = s;
            largest = r;
        }
    }

    size_t out = 0;
    for (size_t t = 0; t + 2 < r_indices.size(); t += 3) {
        const uint32_t r = _root(parent, r_indices[t]);
        if (r != largest && island_size(r) < p_min_feature) continue;
        r_indices[out++] = r_indices[t];
        r_indices[out++] = r_indices[t + 1];
        r_indices[out++] = r_indices[t + 2];
    }
    r_indices.resize(out);
}

static void _simplify(std::vector<vec3>& r_positions, std::vector<uint32_t>& r_indices, float p_error)
{
    if (p_error <= 0.0f || r_indices.empty()) return;
    std::vector<uint32_t> out(r_indices.size());
    float result_error = 0.0f;
    const size_t count = meshopt_simplify(out.data(), r_indices.data(), r_indices.size(), &r_positions[0].x, r_positions.size(), sizeof(vec3), 0, p_error, meshopt_SimplifyErrorAbsolute, &result_error);
    out.resize(count);
    r_indices = std::move(out);
    _drop_degenerate(r_positions, r_indices);
    _compact(r_positions, r_indices);
}

static bool _build_hull(const std::vector<vec3>& p_points, float p_tolerance, LCollision& r_collision)
{
    JPH::ConvexHullBuilder::Positions points;
    points.reserve(p_points.size());
    for (const vec3& p : p_points) points.push_back(JPH::Vec3(p.x, p.y, p.z));

    JPH::ConvexHullBuilder builder(points);
    const char* error = nullptr;
    const JPH::ConvexHullBuilder::EResult result = builder.Initialize(CollisionCooker::MAX_HULL_VERTICES, std::max(p_tolerance, MIN_HULL_TOLERANCE), error);
    if (result != JPH::ConvexHullBuilder::EResult::Success && result != JPH::ConvexHullBuilder::EResult::MaxVerticesReached) {
        log_write("CollisionCooker: convex hull failed (%s).", error ? error : "unknown");
        return false;
    }

    std::vector<uint32_t> map(p_points.size(), UINT32_MAX);
    auto vertex = [&](int p_index) {
        if (map[p_index] == UINT32_MAX) {
            map[p_index] = (uint32_t)r_collision.vertices.size();
            r_collision.vertices.push_back(p_points[p_index]);
        }
        return map[p_index];
    };
    for (const JPH::ConvexHullBuilder::Face* face : builder.GetFaces()) {
        if (face->mRemoved || !face->mFirstEdge) continue;
        std::vector<int> loop;
        const JPH::ConvexHullBuilder::Edge* e = face->mFirstEdge;
        do {
            loop.push_back(e->mStartIdx);
            e = e->mNextEdge;
        } while (e && e != face->mFirstEdge);
        for (size_t i = 1; i + 1 < loop.size(); i++) {
            r_collision.indices.push_back(vertex(loop[0]));
            r_collision.indices.push_back(vertex(loop[i]));
            r_collision.indices.push_back(vertex(loop[i + 1]));
        }
    }
    return !r_collision.indices.empty();
}

static void _fit_primitive(const std::vector<vec3>& p_points, CollisionType p_type, LCollision& r_collision)
{
    vec3 lo(FLT_MAX), hi(-FLT_MAX);
    for (const vec3& p : p_points) {
        lo = min(lo, p);
        hi = max(hi, p);
    }
    const vec3 center = (lo + hi) * 0.5f;
    const vec3 half = max((hi - lo) * 0.5f, vec3(CollisionCooker::MIN_PRIMITIVE_SIZE));
    r_collision.center = center;
    r_collision.half_extents = half;

    if (p_type == CollisionType::SPHERE) {
        float r2 = 0.0f;
        for (const vec3& p : p_points) r2 = std::max(r2, dot(p - center, p - center));
        r_collision.radius = std::max(std::sqrt(r2), CollisionCooker::MIN_PRIMITIVE_SIZE);
        return;
    }
    if (p_type != CollisionType::CAPSULE) return;

    const int axis = half.x >= half.y && half.x >= half.z ? 0 : (half.z >= half.y ? 2 : 1);
    vec3 dir(0.0f);
    dir[axis] = 1.0f;
    if (axis == 0) r_collision.rotation = angleAxis(-half_pi<float>(), vec3(0.0f, 0.0f, 1.0f));
    else if (axis == 2) r_collision.rotation = angleAxis(half_pi<float>(), vec3(1.0f, 0.0f, 0.0f));

    float r2 = 0.0f;
    for (const vec3& p : p_points) {
        const vec3 d = p - center;
        const vec3 perp = d - dir * dot(d, dir);
        r2 = std::max(r2, dot(perp, perp));
    }
    r_collision.radius = std::max(std::sqrt(r2), CollisionCooker::MIN_PRIMITIVE_SIZE);
    r_collision.half_height = std::max(half[axis] - r_collision.radius, CollisionCooker::MIN_PRIMITIVE_SIZE);
}

bool CollisionCooker::build(const aiScene* p_scene, const Settings& p_settings, const MeshCooker::CookSettings& p_transform, LCollision& r_collision)
{
    r_collision = LCollision{};
    r_collision.type = p_settings.type;
    if (!p_scene || !p_scene->mRootNode) return false;

    std::vector<bool> excluded(p_scene->mNumMaterials, false);
    if (p_settings.exclude_masked) {
        for (uint32_t m = 0; m < p_scene->mNumMaterials; m++) excluded[m] = _material_excluded(p_scene->mMaterials[m]);
    }

    std::vector<vec3> positions;
    std::vector<uint32_t> indices;
    _gather(p_scene, p_scene->mRootNode, aiMatrix4x4(), excluded, positions, indices);
    r_collision.source_triangles = (uint32_t)(indices.size() / 3);
    if (indices.empty()) {
        log_write("CollisionCooker: no triangles left to build collision from (all materials excluded?).");
        return false;
    }

    _apply_transform(p_transform, positions);
    _weld(positions, indices);
    _drop_degenerate(positions, indices);
    _cull_islands(positions, indices, p_settings.min_feature);
    _compact(positions, indices);
    if (indices.empty()) return false;

    switch (p_settings.type) {
        case CollisionType::MESH:
            _simplify(positions, indices, p_settings.error);
            r_collision.vertices = std::move(positions);
            r_collision.indices = std::move(indices);
            break;
        case CollisionType::HULL:
            if (!_build_hull(positions, p_settings.error, r_collision)) return false;
            break;
        default:
            _fit_primitive(positions, p_settings.type, r_collision);
            break;
    }

    vec3 lo(FLT_MAX), hi(-FLT_MAX);
    if (r_collision.primitive()) {
        lo = r_collision.center - r_collision.half_extents;
        hi = r_collision.center + r_collision.half_extents;
    } else {
        for (const vec3& p : r_collision.vertices) {
            lo = min(lo, p);
            hi = max(hi, p);
        }
    }
    r_collision.bounds_min = lo;
    r_collision.bounds_max = hi;
    return r_collision.primitive() || !r_collision.indices.empty();
}

Error CollisionCooker::cook(const Job& p_job, LCollision& r_collision)
{
    using enum Error;
    r_collision.guid = p_job.guid;
    if (r_collision.save_cooked(p_job.content_bin) != OK) {
        log_write("CollisionCooker: failed to write '%s'.", p_job.content_bin.string().c_str());
        return FAILED;
    }
    const MeshCooker::CookSettings& t = p_job.transform;
    toml::table tbl {
        { "asset", toml::table{
            { "version", static_cast<int64_t>(LASSET_VERSION) },
            { "guid", p_job.guid.to_string() },
            { "type", static_cast<int64_t>(AssetType::COLLISION) } }},
        { "import", toml::table{
            { "source", p_job.source.generic_string() },
            { "translation", toml::array{ t.translation.x, t.translation.y, t.translation.z } },
            { "rotation", toml::array{ t.rotation.x, t.rotation.y, t.rotation.z } },
            { "scale", static_cast<double>(t.scale) },
            { "type", collision_type_name(p_job.settings.type) },
            { "error", static_cast<double>(p_job.settings.error) },
            { "min_feature", static_cast<double>(p_job.settings.min_feature) },
            { "exclude_masked", p_job.settings.exclude_masked } }},
        { std::string(asset_type_section(AssetType::COLLISION)), toml::table{
            { "mesh", p_job.mesh.to_string() },
            { "type", collision_type_name(r_collision.type) },
            { "triangles", static_cast<int64_t>(r_collision.triangle_count()) },
            { "vertices", static_cast<int64_t>(r_collision.vertices.size()) },
            { "source_triangles", static_cast<int64_t>(r_collision.source_triangles) } }},
    };
    std::ostringstream ss;
    ss << tbl;
    return write_file_atomic(p_job.dst_lcollision, ss.str()) ? OK : FAILED;
}

static vec3 _read_vec3(const toml::node_view<toml::node>& p_node, const vec3& p_fallback)
{
    const toml::array* arr = p_node.as_array();
    if (!arr || arr->size() != 3) return p_fallback;
    vec3 v = p_fallback;
    for (size_t i = 0; i < 3; i++) v[(int)i] = (float)(*arr)[i].value_or((double)p_fallback[(int)i]);
    return v;
}

bool CollisionCooker::load_record(const std::filesystem::path& p_lcollision, Record& r_record)
{
    toml::table tbl;
    try { tbl = toml::parse_file(p_lcollision.string()); }
    catch (const toml::parse_error&) { return false; }
    const toml::node_view<toml::node> rec = tbl["import"];
    const std::string source = rec["source"].value_or(std::string());
    if (source.empty()) return false;

    r_record.source = std::filesystem::path(source);
    r_record.mesh = Guid::from_string(tbl[asset_type_section(AssetType::COLLISION)]["mesh"].value_or(std::string()));
    r_record.transform.translation = _read_vec3(rec["translation"], r_record.transform.translation);
    r_record.transform.rotation = _read_vec3(rec["rotation"], r_record.transform.rotation);
    r_record.transform.scale = (float)rec["scale"].value_or((double)r_record.transform.scale);
    r_record.settings.type = collision_type_from_name(rec["type"].value_or(std::string()), r_record.settings.type);
    r_record.settings.error = (float)rec["error"].value_or((double)r_record.settings.error);
    r_record.settings.min_feature = (float)rec["min_feature"].value_or((double)r_record.settings.min_feature);
    r_record.settings.exclude_masked = rec["exclude_masked"].value_or(r_record.settings.exclude_masked);
    return true;
}

std::filesystem::path CollisionCooker::find(const std::filesystem::path& p_folder, Guid p_guid)
{
    if (!p_guid.valid()) return {};
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(p_folder, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        if (it->path().extension() != ".lcollision") continue;
        const AssetInfo info = read_asset_info(it->path());
        if (info.valid() && info.type == AssetType::COLLISION && info.guid == p_guid) return it->path();
    }
    return {};
}

Guid CollisionCooker::mesh_collision(const std::filesystem::path& p_lmesh)
{
    std::error_code ec;
    if (!std::filesystem::exists(p_lmesh, ec)) return {};
    toml::table tbl;
    try { tbl = toml::parse_file(p_lmesh.string()); }
    catch (const toml::parse_error&) { return {}; }
    return Guid::from_string(tbl[asset_type_section(AssetType::MESH)]["collision"].value_or(std::string()));
}

bool CollisionCooker::load_for_mesh(const std::filesystem::path& p_lmesh, Settings& r_settings)
{
    const std::filesystem::path path = find(p_lmesh.parent_path(), mesh_collision(p_lmesh));
    Record record;
    if (path.empty() || !load_record(path, record)) return false;
    r_settings = record.settings;
    return true;
}

static Error _reimport_collision(const CollisionCooker::Job& p_job, ImportStep p_step)
{
    using enum Error;
    Assimp::Importer importer;
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
    const aiScene* scene = importer.ReadFile(p_job.source.string(), MeshCooker::import_flags());
    if (p_step.cancelled()) return CANCELED;
    if (!scene) {
        log_write("CollisionCooker: assimp failed for %s (%s)", p_job.source.string().c_str(), importer.GetErrorString());
        return FAILED;
    }
    ModelCooker::fbx_to_meters(scene);
    p_step.report(0.6f);

    LCollision collision;
    if (!CollisionCooker::build(scene, p_job.settings, p_job.transform, collision)) return FAILED;
    if (p_step.cancelled()) return CANCELED;
    const Error err = CollisionCooker::cook(p_job, collision);
    p_step.report(1.0f);
    return err;
}

Error CollisionCooker::reimport(EditorContext& ctx, const std::filesystem::path& p_lcollision, const Settings* p_override)
{
    using enum Error;
    Record record;
    if (!load_record(p_lcollision, record)) {
        log_write("Reimport failed: '%s' has no import source.", p_lcollision.filename().string().c_str());
        return FAILED;
    }
    std::error_code ec;
    if (!std::filesystem::exists(record.source, ec)) {
        log_write("Reimport failed: source '%s' no longer exists.", record.source.string().c_str());
        return FAILED;
    }
    const AssetInfo info = read_asset_info(p_lcollision);
    if (!info.valid() || info.type != AssetType::COLLISION) return FAILED;

    std::shared_ptr<ImportSession> session = ctx.imports->begin(*ctx.project, p_lcollision);
    if (!session) return FAILED;
    const ImportSession::Staged staged = session->stage(*ctx.project, p_lcollision, info.guid, true, true);

    Job job;
    job.source = record.source;
    job.dst_lcollision = staged.asset;
    job.content_bin = staged.content_bin;
    job.guid = info.guid;
    job.mesh = record.mesh;
    job.settings = p_override ? *p_override : record.settings;
    job.transform = record.transform;
    job_system().run_background_owned([job, session]{
        session->finish(_reimport_collision(job, session->step()));
    }, nullptr, "Collision Reimport");
    log_write("Reimporting '%s' from '%s'.", p_lcollision.filename().string().c_str(), record.source.string().c_str());
    return OK;
}

}
