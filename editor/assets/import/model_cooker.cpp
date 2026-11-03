#include <editor/assets/import/model_cooker.h>
#include <editor/application/editor_context.h>
#include <editor/assets/import/skeleton_cooker.h>
#include <core/assets/asset_common.h>
#include <core/assets/lmodel.h>
#include <core/assets/lmaterial.h>
#include <core/project/project.h>
#include <core/base/jobs.h>
#include <core/base/error.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/material.h>
#include <toml++/toml.hpp>
#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace lumen {

struct MapRef { std::filesystem::path source; float alpha_cutoff = 0.0f; };

static std::filesystem::path _texture_path(const std::filesystem::path& p_source_dir, const aiString& p_ref)
{
    std::filesystem::path p(std::u8string_view(reinterpret_cast<const char8_t*>(p_ref.C_Str()), p_ref.length));
    if (p.is_relative()) p = p_source_dir / p;
    p = p.lexically_normal();
    std::error_code ec;
    if (std::filesystem::exists(p, ec)) return p;
    const std::filesystem::path flat = p_source_dir / p.filename();
    return std::filesystem::exists(flat, ec) ? flat : std::filesystem::path{};
}

static AlphaMode _alpha_mode(const aiMaterial* p_mat)
{
    aiString mode;
    if (p_mat->Get("$mat.gltf.alphaMode", 0, 0, mode) != AI_SUCCESS) return AlphaMode::NONE;
    if (std::strcmp(mode.C_Str(), "MASK") == 0) return AlphaMode::MASKED;
    if (std::strcmp(mode.C_Str(), "BLEND") == 0) return AlphaMode::BLEND;
    return AlphaMode::NONE;
}

static float _alpha_cutoff(const aiMaterial* p_mat)
{
    float f = 0.5f;
    p_mat->Get("$mat.gltf.alphaCutoff", 0, 0, f);
    return f;
}

static MapRef _resolve_mask(const aiMaterial* p_mat, const std::filesystem::path& p_source_dir, const std::filesystem::path& p_model)
{
    if (_alpha_mode(p_mat) != AlphaMode::MASKED) return {};
    for (aiTextureType type : { aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE }) {
        if (p_mat->GetTextureCount(type) == 0) continue;
        aiString ref;
        if (p_mat->GetTexture(type, 0, &ref) != AI_SUCCESS || ref.length == 0) continue;
        if (ref.C_Str()[0] == '*') {
            log_write("ModelCooker: embedded textures in %s are not supported yet.", p_model.string().c_str());
            continue;
        }
        const std::filesystem::path path = _texture_path(p_source_dir, ref);
        if (path.empty()) {
            log_write("ModelCooker: texture '%s' not found.", ref.C_Str());
            continue;
        }
        return { path, _alpha_cutoff(p_mat) };
    }
    return {};
}

static bool _same_texture(const TextureCooker::Job& p_job, const MapRef& p_ref)
{
    return p_job.source == p_ref.source;
}

static std::vector<TextureCooker::Job> _texture_jobs(const ModelCooker::Job& p_job, const std::vector<MapRef>& p_masks, std::vector<std::filesystem::path>& r_finals)
{
    std::vector<TextureCooker::Job> jobs;
    std::vector<std::filesystem::path> used;
    const std::filesystem::path& dir = p_job.dst_dir;

    for (const MapRef& r : p_masks) {
        if (r.source.empty()) continue;
        bool seen = false;
        for (const TextureCooker::Job& j : jobs) seen = seen || _same_texture(j, r);
        if (seen) continue;

        const std::wstring base = r.source.stem().wstring();
        std::filesystem::path dst = dir / (base + L".ltexture");
        for (uint32_t i = 1; std::find(used.begin(), used.end(), dst) != used.end(); ++i) dst = dir / (base + L"_" + std::to_wstring(i) + L".ltexture");
        used.push_back(dst);

        TextureCooker::Job tex;
        tex.source = r.source;
        tex.dst_ltexture = dst;
        tex.settings = p_job.settings.texture_settings;
        tex.settings.usage = TextureUsage::COLOR;
        tex.settings.source_channel = -1;
        tex.settings.alpha_cutoff = r.alpha_cutoff;
        std::filesystem::path content_bin;
        if (AssetImportTracker::resolve_import(*p_job.project, AssetType::TEXTURE, dst, tex.guid, content_bin) != Error::OK) {
            log_write("ModelCooker: '%s' exists and is not a texture.", dst.string().c_str());
            continue;
        }
        const ImportSession::Staged staged = p_job.session->stage(*p_job.project, dst, tex.guid, true, false);
        tex.dst_ltexture = staged.asset;
        tex.content_bin = staged.content_bin;
        jobs.push_back(std::move(tex));
        r_finals.push_back(dst);
    }
    return jobs;
}

static Guid _texture_guid(const std::vector<TextureCooker::Job>& p_jobs, const MapRef& p_ref)
{
    if (p_ref.source.empty()) return {};
    for (const TextureCooker::Job& j : p_jobs) {
        if (_same_texture(j, p_ref)) return j.guid;
    }
    return {};
}

static std::string _material_file_name(std::string p_name, uint32_t p_slot)
{
    for (char& c : p_name) {
        if ((unsigned char)c < 32 || std::strchr("<>:\"/\\|?*", c)) c = '_';
    }
    while (!p_name.empty() && (p_name.back() == ' ' || p_name.back() == '.')) p_name.pop_back();
    while (!p_name.empty() && p_name.front() == ' ') p_name.erase(p_name.begin());
    if (p_name.empty()) p_name = "Material_" + std::to_string(p_slot);
    return p_name;
}

static uint64_t _scene_triangles(const aiScene* p_scene)
{
    uint64_t tris = 0;
    for (uint32_t i = 0; i < p_scene->mNumMeshes; i++) tris += p_scene->mMeshes[i]->mNumFaces;
    return tris;
}

static LMaterial _build_material(const std::vector<TextureCooker::Job>& p_textures, const aiMaterial* p_mat, const MapRef& p_mask)
{
    LMaterial m;
    int two_sided = 0;
    if (p_mat->Get(AI_MATKEY_TWOSIDED, two_sided) == AI_SUCCESS) m.two_sided = two_sided != 0;
    m.alpha_mode = _alpha_mode(p_mat);
    m.alpha_cutoff = _alpha_cutoff(p_mat);
    m.mask_texture = _texture_guid(p_textures, p_mask);
    return m;
}

static std::vector<Guid> _import_materials(const ModelCooker::Job& p_job, const aiScene* p_scene, const std::vector<uint32_t>& p_slots, const std::vector<MapRef>& p_masks, const std::vector<TextureCooker::Job>& p_textures)
{
    struct Existing {
        Guid guid;
        std::string name;
        bool taken = false;
    };

    const std::string source = p_job.source.generic_string();
    std::vector<Existing> existing;
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(p_job.dst_dir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        if (it->path().extension() != ".lmaterial") continue;
        LMaterial m;
        if (m.load(it->path()) == Error::OK && m.import.source == source) existing.push_back({ m.guid, m.import.name });
    }

    std::vector<Guid> out(p_slots.size());
    for (uint32_t s = 0; s < (uint32_t)p_slots.size(); ++s) {
        const aiMaterial* mat = p_scene->mMaterials[p_slots[s]];
        aiString ai_name;
        mat->Get(AI_MATKEY_NAME, ai_name);
        const std::string original = ai_name.C_Str();

        bool reused = false;
        for (Existing& e : existing) {
            if (e.taken || e.name != original) continue;
            e.taken = true;
            out[s] = e.guid;
            reused = true;
            break;
        }
        if (reused) continue;

        const std::string name = _material_file_name(original, s);
        std::filesystem::path dst;
        for (uint32_t i = 0;; ++i) {
            const std::string candidate = i == 0 ? name : name + " " + std::to_string(i);
            dst = p_job.dst_dir / std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(candidate.data()), candidate.size())).concat(".lmaterial");
            if (!std::filesystem::exists(dst, ec) && !p_job.session->staged_exists(dst)) break;
        }

        LMaterial m = _build_material(p_textures, mat, p_masks[s]);
        m.guid = Guid::generate();
        m.import.source = source;
        m.import.name = original;
        if (m.save(p_job.session->stage(*p_job.project, dst, m.guid, false, false).asset) != Error::OK) {
            log_write("ModelCooker: failed to write '%s'.", dst.string().c_str());
            p_job.session->unstage(dst);
            continue;
        }
        out[s] = m.guid;
    }
    return out;
}

struct ModelLocation
{
    std::filesystem::path folder;
    std::filesystem::path lmodel;
    std::filesystem::path lmesh;
    std::filesystem::path lskeleton;
};

static ModelLocation _model_location(const std::filesystem::path& p_dst_dir, const std::filesystem::path& p_src, const std::wstring& p_name)
{
    const std::wstring stem = p_name.empty() ? p_src.stem().wstring() : p_name;
    const std::string source = p_src.generic_string();
    std::error_code ec;
    for (uint32_t i = 0;; ++i) {
        ModelLocation loc;
        loc.folder = p_dst_dir / (i == 0 ? stem : stem + L" " + std::to_wstring(i));
        loc.lmodel = loc.folder / (stem + L".lmodel");
        loc.lmesh = loc.folder / (stem + L".lmesh");
        loc.lskeleton = loc.folder / (stem + L".lskeleton");
        if (!std::filesystem::exists(loc.folder, ec)) return loc;

        LModel existing;
        bool owned = false;
        for (auto it = std::filesystem::directory_iterator(loc.folder, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
            if (it->path().extension() != ".lmodel") continue;
            if (existing.load(it->path()) != Error::OK || existing.import.source != source) continue;
            loc.lmodel = it->path();
            owned = true;
            break;
        }
        if (!owned) continue;

        for (auto it = std::filesystem::directory_iterator(loc.folder, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
            if (it->path().extension() != ".lmesh") continue;
            const AssetInfo info = read_asset_info(it->path());
            if (info.valid() && info.type == AssetType::MESH && info.guid == existing.mesh) {
                loc.lmesh = it->path();
                break;
            }
        }
        for (auto it = std::filesystem::directory_iterator(loc.folder, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
            if (it->path().extension() != ".lskeleton") continue;
            toml::table tbl;
            try { tbl = toml::parse_file(it->path().string()); }
            catch (const toml::parse_error&) { continue; }
            if (tbl["import"]["source"].value_or(std::string{}) != source) continue;
            loc.lskeleton = it->path();
            break;
        }
        return loc;
    }
}

Error ModelCooker::_import(const Job& p_job)
{
    using enum Error;
    ImportSession& session = *p_job.session;
    const ImportStep root = session.step();

    Assimp::Importer importer;
    AssimpImportProgress* read_progress = new AssimpImportProgress();
    read_progress->step = root.sub(0.0f, READ_END);
    importer.SetProgressHandler(read_progress);
    const aiScene* scene = importer.ReadFile(p_job.source.string(), MeshCooker::import_flags());
    if (root.cancelled()) return CANCELED;
    if (!scene) {
        log_write("ModelCooker: assimp failed for %s (%s)", p_job.source.string().c_str(), importer.GetErrorString());
        return FAILED;
    }

    const bool full = p_job.settings.mode == ModelImportMode::FULL;
    const std::vector<uint32_t> slots = MeshCooker::material_slots(scene);
    std::vector<MapRef> masks;
    std::vector<TextureCooker::Job> textures;
    std::vector<std::filesystem::path> texture_finals;
    if (full) {
        const std::filesystem::path source_dir = p_job.source.parent_path();
        for (uint32_t index : slots) masks.push_back(_resolve_mask(scene->mMaterials[index], source_dir, p_job.source));
        textures = _texture_jobs(p_job, masks, texture_finals);
    }

    std::vector<uint64_t> tex_cost(textures.size());
    const uint64_t mesh_cost = std::max<uint64_t>(_scene_triangles(scene) * MESH_COST_PER_TRI, 1);
    uint64_t total_cost = mesh_cost;
    for (size_t i = 0; i < textures.size(); i++) total_cost += tex_cost[i] = TextureCooker::cost(textures[i].source, textures[i].settings);
    const float work_span = 1.0f - READ_END - SAVE_SHARE;
    float at = READ_END;
    auto next_step = [&](uint64_t p_cost) {
        const float from = at;
        at += work_span * (float)((double)p_cost / (double)total_cost);
        return root.sub(from, at);
    };

    LSkeleton skeleton;
    const bool skinned = p_job.settings.mesh_settings.load_skeleton && SkeletonCooker::build(scene, p_job.settings.mesh_settings, skeleton);
    if (skinned) {
        skeleton.guid = p_job.skeleton_guid;
        const ImportSession::Staged staged = session.stage(*p_job.project, p_job.dst_lskeleton, p_job.skeleton_guid, true, false);
        SkeletonCooker::Job skel;
        skel.source = p_job.source;
        skel.dst_lskeleton = staged.asset;
        skel.content_bin = staged.content_bin;
        if (const Error err = SkeletonCooker::cook(skel, skeleton); err != OK) return err;
    }

    MeshCooker::Job mesh;
    mesh.source = p_job.source;
    mesh.skeleton = skinned ? &skeleton : nullptr;
    mesh.guid = p_job.mesh_guid;
    mesh.settings = p_job.settings.mesh_settings;
    const ImportSession::Staged mesh_staged = session.stage(*p_job.project, p_job.dst_lmesh, p_job.mesh_guid, true, false);
    mesh.dst_lmesh = mesh_staged.asset;
    mesh.content_bin = mesh_staged.content_bin;
    mesh.step = next_step(mesh_cost);
    if (const Error err = MeshCooker::cook_scene(mesh, scene); err != OK) return err;

    for (size_t i = 0; i < textures.size(); i++) {
        TextureCooker::Job& tex = textures[i];
        tex.step = next_step(tex_cost[i]);
        const Error err = TextureCooker::_cook(tex);
        if (root.cancelled()) return CANCELED;
        if (err == OK) continue;
        log_write("ModelCooker: failed to cook texture '%s'.", tex.source.string().c_str());
        session.unstage(texture_finals[i]);
        tex.guid = {};
    }

    LModel model;
    std::error_code ec;
    if (std::filesystem::exists(p_job.dst_lmodel, ec) && model.load(p_job.dst_lmodel) != OK) return FAILED;
    model.guid = p_job.model_guid;
    model.mesh = p_job.mesh_guid;
    const std::vector<Guid> imported = full ? _import_materials(p_job, scene, slots, masks, textures) : std::vector<Guid>(slots.size());
    model.materials.resize(std::max<size_t>(imported.size(), 1));
    for (size_t s = 0; s < imported.size(); ++s) {
        if (!model.materials[s].valid()) model.materials[s] = imported[s];
    }
    model.import.source = p_job.source.generic_string();
    model.import.mode = p_job.settings.mode;
    if (model.save(session.stage(*p_job.project, p_job.dst_lmodel, p_job.model_guid, false, true).asset) != OK) {
        log_write("ModelCooker: failed to write '%s'.", p_job.dst_lmodel.string().c_str());
        return FAILED;
    }
    root.report(1.0f);
    return OK;
}

void ModelCooker::import_async(EditorContext& ctx, const std::filesystem::path& p_src, const std::filesystem::path& p_dst_dir, const Settings& p_settings)
{
    const ModelLocation loc = _model_location(p_dst_dir, p_src, p_settings.name);

    Job job;
    job.project = ctx.project;
    job.source = p_src;
    job.dst_dir = loc.folder;
    job.settings = p_settings;
    job.dst_lmesh = loc.lmesh;
    job.dst_lmodel = loc.lmodel;
    job.dst_lskeleton = loc.lskeleton;

    std::filesystem::path mesh_content_bin;
    if (AssetImportTracker::resolve_import(*ctx.project, AssetType::MESH, job.dst_lmesh, job.mesh_guid, mesh_content_bin) != Error::OK) {
        log_write("Model import failed: %s", p_src.string().c_str());
        return;
    }
    std::filesystem::path skeleton_content_bin;
    if (AssetImportTracker::resolve_import(*ctx.project, AssetType::SKELETON, job.dst_lskeleton, job.skeleton_guid, skeleton_content_bin) != Error::OK) {
        log_write("Model import failed: '%s' exists and is not a skeleton.", job.dst_lskeleton.string().c_str());
        return;
    }
    std::error_code ec;
    if (std::filesystem::exists(job.dst_lmodel, ec)) {
        const AssetInfo existing = read_asset_info(job.dst_lmodel);
        if (!existing.valid() || existing.type != AssetType::MODEL) {
            log_write("Model import failed: '%s' exists and is not a model.", job.dst_lmodel.string().c_str());
            return;
        }
        job.model_guid = existing.guid;
    } else {
        job.model_guid = Guid::generate();
    }

    std::shared_ptr<ImportSession> session = ctx.imports->begin(*ctx.project, loc.folder);
    if (!session) return;
    job.session = session.get();
    job_system().run_background_owned([job, session]{
        session->finish(ModelCooker::_import(job));
    }, nullptr, "Model Import");
}

}
