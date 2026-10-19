#include <editor/assets/import/model_importer.h>
#include <editor/editor_context.h>
#include <core/assets/asset_common.h>
#include <core/assets/lmodel.h>
#include <core/assets/lmaterial.h>
#include <core/project/project.h>
#include <core/base/tasks.h>
#include <core/base/error.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/material.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace lumen {

static constexpr float EMISSIVE_IMPORT_NITS = 100.0f;

enum class MapKind : uint8_t { BASE_COLOR, NORMAL, ROUGHNESS, METALLIC, AO, EMISSIVE, COUNT };

struct MapRef {
    std::filesystem::path source;
    TextureUsage usage = TextureUsage::COLOR;
    int32_t channel = -1;
    const char* suffix = "";
};

struct MaterialMaps {
    MapRef maps[(size_t)MapKind::COUNT];

    MapRef& operator[](MapKind p_kind) { return maps[(size_t)p_kind]; }
    const MapRef& operator[](MapKind p_kind) const { return maps[(size_t)p_kind]; }
};

static TextureUsage _texture_usage(aiTextureType p_type, const std::filesystem::path& p_path)
{
    switch (p_type) {
        case aiTextureType_NORMALS:
        case aiTextureType_NORMAL_CAMERA: return TextureUsage::NORMAL;
        case aiTextureType_HEIGHT: return texture_usage_guess(p_path) == TextureUsage::NORMAL ? TextureUsage::NORMAL : TextureUsage::LINEAR;
        case aiTextureType_DISPLACEMENT: return TextureUsage::LINEAR;
        case aiTextureType_METALNESS:
        case aiTextureType_DIFFUSE_ROUGHNESS:
        case aiTextureType_AMBIENT_OCCLUSION:
        case aiTextureType_LIGHTMAP: return TextureUsage::MASK;
        default: return TextureUsage::COLOR;
    }
}

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

static MapRef _find_map(const aiMaterial* p_mat, std::initializer_list<aiTextureType> p_types, const std::filesystem::path& p_source_dir, const std::filesystem::path& p_model)
{
    for (aiTextureType type : p_types) {
        if (p_mat->GetTextureCount(type) == 0) continue;
        aiString ref;
        if (p_mat->GetTexture(type, 0, &ref) != AI_SUCCESS || ref.length == 0) continue;
        if (ref.C_Str()[0] == '*') {
            log_write("ModelImporter: embedded textures in %s are not supported yet.", p_model.string().c_str());
            continue;
        }
        const std::filesystem::path path = _texture_path(p_source_dir, ref);
        if (path.empty()) {
            log_write("ModelImporter: texture '%s' not found.", ref.C_Str());
            continue;
        }
        MapRef r;
        r.source = path;
        r.usage = _texture_usage(type, path);
        return r;
    }
    return {};
}

static MaterialMaps _resolve_maps(const aiMaterial* p_mat, const std::filesystem::path& p_source_dir, const std::filesystem::path& p_model)
{
    MaterialMaps r;
    r[MapKind::BASE_COLOR] = _find_map(p_mat, { aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE }, p_source_dir, p_model);
    r[MapKind::NORMAL] = _find_map(p_mat, { aiTextureType_NORMALS, aiTextureType_NORMAL_CAMERA }, p_source_dir, p_model);
    r[MapKind::EMISSIVE] = _find_map(p_mat, { aiTextureType_EMISSIVE, aiTextureType_EMISSION_COLOR }, p_source_dir, p_model);

    if (r[MapKind::NORMAL].source.empty()) {
        const MapRef bump = _find_map(p_mat, { aiTextureType_HEIGHT }, p_source_dir, p_model);
        if (bump.usage == TextureUsage::NORMAL) r[MapKind::NORMAL] = bump;
    }

    r[MapKind::ROUGHNESS] = _find_map(p_mat, { aiTextureType_DIFFUSE_ROUGHNESS }, p_source_dir, p_model);
    r[MapKind::METALLIC] = _find_map(p_mat, { aiTextureType_METALNESS }, p_source_dir, p_model);
    r[MapKind::AO] = _find_map(p_mat, { aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP }, p_source_dir, p_model);

    const MapKind masks[] = { MapKind::AO, MapKind::ROUGHNESS, MapKind::METALLIC };
    const int32_t packed_channel[] = { 0, 1, 2 };
    const char* packed_suffix[] = { "ao", "roughness", "metallic" };
    for (int i = 0; i < 3; ++i) {
        MapRef& m = r[masks[i]];
        if (m.source.empty()) continue;
        bool shared = false;
        for (int j = 0; j < 3; ++j) shared = shared || (j != i && r[masks[j]].source == m.source);
        m.usage = TextureUsage::MASK;
        m.channel = shared ? packed_channel[i] : 0;
        m.suffix = shared ? packed_suffix[i] : "";
    }
    return r;
}

static bool _same_texture(const TextureCooker::Job& p_job, const MapRef& p_ref)
{
    return p_job.source == p_ref.source && p_job.settings.usage == p_ref.usage && p_job.settings.source_channel == p_ref.channel;
}

static std::vector<TextureCooker::Job> _texture_jobs(const ModelImporter::Job& p_job, const std::vector<MaterialMaps>& p_maps)
{
    std::vector<TextureCooker::Job> jobs;
    std::vector<std::filesystem::path> used;
    const std::filesystem::path& dir = p_job.dst_dir;

    for (const MaterialMaps& mm : p_maps) {
        for (const MapRef& r : mm.maps) {
            if (r.source.empty()) continue;
            bool seen = false;
            for (const TextureCooker::Job& j : jobs) seen = seen || _same_texture(j, r);
            if (seen) continue;

            std::wstring base = r.source.stem().wstring();
            if (r.suffix[0]) base += L"_" + std::filesystem::path(r.suffix).wstring();
            std::filesystem::path dst = dir / (base + L".ltexture");
            for (uint32_t i = 0; std::find(used.begin(), used.end(), dst) != used.end(); ++i) {
                const std::wstring tag = i == 0 ? std::filesystem::path(texture_usage_name(r.usage)).wstring() : std::to_wstring(i);
                dst = dir / (base + L"_" + tag + L".ltexture");
            }
            used.push_back(dst);

            TextureCooker::Job tex;
            tex.source = r.source;
            tex.dst_ltexture = dst;
            tex.settings = p_job.settings.texture_settings;
            tex.settings.usage = r.usage;
            tex.settings.source_channel = r.channel;
            if (r.usage == TextureUsage::NORMAL) tex.settings.flip_green = true;
            if (AssetImportTracker::resolve_import(*p_job.project, AssetType::TEXTURE, dst, tex.guid, tex.content_bin) != Error::OK) {
                log_write("ModelImporter: '%s' exists and is not a texture.", dst.string().c_str());
                continue;
            }
            tex.progress = std::make_shared<ImportControl>();
            if (p_job.progress) p_job.progress->push_child({ dst, tex.guid, tex.content_bin, tex.progress });
            jobs.push_back(std::move(tex));
        }
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

static LMaterial _build_material(const std::vector<TextureCooker::Job>& p_textures, const aiMaterial* p_mat, const MaterialMaps& p_maps)
{
    LMaterial m;
    m.shading_model = ShadingModel::DEFAULT_LIT;

    aiColor4D c4;
    const bool pbr = p_mat->Get(AI_MATKEY_BASE_COLOR, c4) == AI_SUCCESS;
    m.base_color_texture = _texture_guid(p_textures, p_maps[MapKind::BASE_COLOR]);
    if (pbr) {
        m.base_color = vec4(c4.r, c4.g, c4.b, c4.a);
    } else if (p_mat->Get(AI_MATKEY_COLOR_DIFFUSE, c4) == AI_SUCCESS) {
        m.base_color = m.base_color_texture == Guid{} ? vec4(c4.r, c4.g, c4.b, c4.a) : vec4(1.0f, 1.0f, 1.0f, c4.a);
    }

    m.normal_texture = _texture_guid(p_textures, p_maps[MapKind::NORMAL]);

    const Guid rough = _texture_guid(p_textures, p_maps[MapKind::ROUGHNESS]);
    const Guid metal = _texture_guid(p_textures, p_maps[MapKind::METALLIC]);
    m.roughness_texture = rough;
    m.metallic_texture = metal;
    m.ao_texture = _texture_guid(p_textures, p_maps[MapKind::AO]);

    float f = 0.0f;
    if (p_mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, f) == AI_SUCCESS) m.roughness = f;
    else if (rough != Guid{}) m.roughness = 1.0f;
    else if (p_mat->Get(AI_MATKEY_SHININESS, f) == AI_SUCCESS && f > 0.0f) m.roughness = std::clamp(std::pow(2.0f / (f + 2.0f), 0.25f), 0.02f, 1.0f);
    if (p_mat->Get(AI_MATKEY_METALLIC_FACTOR, f) == AI_SUCCESS) m.metallic = f;
    else if (metal != Guid{}) m.metallic = 1.0f;

    aiColor3D e(0.0f, 0.0f, 0.0f);
    p_mat->Get(AI_MATKEY_COLOR_EMISSIVE, e);
    float strength = 1.0f;
#ifdef AI_MATKEY_EMISSIVE_INTENSITY
    p_mat->Get(AI_MATKEY_EMISSIVE_INTENSITY, strength);
#endif
    const Guid emissive = _texture_guid(p_textures, p_maps[MapKind::EMISSIVE]);
    const float e_max = std::max(e.r, std::max(e.g, e.b));
    if (e_max > 0.0f || (!pbr && emissive != Guid{})) {
        m.emissive_color = e_max > 0.0f ? vec3(e.r, e.g, e.b) : vec3(1.0f);
        m.emissive_nits = EMISSIVE_IMPORT_NITS * strength;
        m.emissive_texture = emissive;
    }

    int two_sided = 0;
    if (p_mat->Get(AI_MATKEY_TWOSIDED, two_sided) == AI_SUCCESS) m.two_sided = two_sided != 0;

    aiString alpha_mode;
    if (p_mat->Get("$mat.gltf.alphaMode", 0, 0, alpha_mode) == AI_SUCCESS) {
        const std::string mode = alpha_mode.C_Str();
        if (mode == "MASK" || mode == "BLEND") m.alpha_mode = AlphaMode::MASKED;
    }
    if (p_mat->Get("$mat.gltf.alphaCutoff", 0, 0, f) == AI_SUCCESS) m.alpha_cutoff = f;
    return m;
}

static std::vector<Guid> _import_materials(const ModelImporter::Job& p_job, const aiScene* p_scene, const std::vector<uint32_t>& p_slots, const std::vector<MaterialMaps>& p_maps, const std::vector<TextureCooker::Job>& p_textures)
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
            if (!std::filesystem::exists(dst, ec)) break;
        }

        LMaterial m = _build_material(p_textures, mat, p_maps[s]);
        m.guid = Guid::generate();
        m.import.source = source;
        m.import.name = original;
        if (m.save(dst) != Error::OK) {
            log_write("ModelImporter: failed to write '%s'.", dst.string().c_str());
            continue;
        }
        out[s] = m.guid;
    }
    return out;
}

struct ModelLocation {
    std::filesystem::path folder;
    std::filesystem::path lmodel;
    std::filesystem::path lmesh;
};

static ModelLocation _model_location(const std::filesystem::path& p_dst_dir, const std::filesystem::path& p_src)
{
    const std::wstring stem = p_src.stem().wstring();
    const std::string source = p_src.generic_string();
    std::error_code ec;
    for (uint32_t i = 0;; ++i) {
        ModelLocation loc;
        loc.folder = p_dst_dir / (i == 0 ? stem : stem + L" " + std::to_wstring(i));
        loc.lmodel = loc.folder / (stem + L".lmodel");
        loc.lmesh = loc.folder / (stem + L".lmesh");
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
        return loc;
    }
}

Error ModelImporter::_import(const Job& p_job)
{
    using enum Error;

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(p_job.source.string(), MeshCooker::import_flags());
    if (!scene) {
        log_write("ModelImporter: assimp failed for %s (%s)", p_job.source.string().c_str(), importer.GetErrorString());
        return FAILED;
    }

    const bool full = p_job.settings.mode == ModelImportMode::FULL;
    const std::vector<uint32_t> slots = MeshCooker::material_slots(scene);
    std::vector<MaterialMaps> maps;
    std::vector<TextureCooker::Job> textures;
    if (full) {
        const std::filesystem::path source_dir = p_job.source.parent_path();
        for (uint32_t index : slots) maps.push_back(_resolve_maps(scene->mMaterials[index], source_dir, p_job.source));
        textures = _texture_jobs(p_job, maps);
    }

    MeshCooker::Job mesh;
    mesh.source = p_job.source;
    mesh.dst_lmesh = p_job.dst_lmesh;
    mesh.content_bin = p_job.mesh_content_bin;
    mesh.guid = p_job.mesh_guid;
    mesh.settings = p_job.settings.mesh_settings;
    mesh.progress = p_job.progress;
    if (MeshCooker::_cook_scene(mesh, scene) != OK) return FAILED;
    for (const TextureCooker::Job& tex : textures) {
        if (TextureCooker::_cook(tex) == OK) continue;
        log_write("ModelImporter: failed to cook texture '%s'.", tex.source.string().c_str());
        if (!tex.progress->cancel.load(std::memory_order_relaxed)) tex.progress->progress.store(1.0f, std::memory_order_relaxed);
    }

    LModel model;
    std::error_code ec;
    if (std::filesystem::exists(p_job.dst_lmodel, ec) && model.load(p_job.dst_lmodel) != OK) return FAILED;
    model.guid = p_job.model_guid;
    model.mesh = p_job.mesh_guid;
    const std::vector<Guid> imported = full ? _import_materials(p_job, scene, slots, maps, textures) : std::vector<Guid>(slots.size());
    model.materials.resize(std::max<size_t>(imported.size(), 1));
    for (size_t s = 0; s < imported.size(); ++s) {
        if (model.materials[s] == Guid{}) model.materials[s] = imported[s];
    }
    model.import.source = p_job.source.generic_string();
    model.import.mode = p_job.settings.mode;
    if (model.save(p_job.dst_lmodel) != OK) {
        log_write("ModelImporter: failed to write '%s'.", p_job.dst_lmodel.string().c_str());
        return FAILED;
    }
    return OK;
}

void ModelImporter::import_async(EditorContext& ctx, const std::filesystem::path& p_src, const std::filesystem::path& p_dst_dir, const Settings& p_settings)
{
    const ModelLocation loc = _model_location(p_dst_dir, p_src);
    const std::filesystem::path& folder = loc.folder;
    std::error_code dir_ec;
    std::filesystem::create_directories(folder, dir_ec);
    if (dir_ec) {
        log_write("Model import failed: cannot create '%s'.", folder.string().c_str());
        return;
    }

    Job job;
    job.project = ctx.project;
    job.source = p_src;
    job.dst_dir = folder;
    job.settings = p_settings;
    job.dst_lmesh = loc.lmesh;
    job.dst_lmodel = loc.lmodel;

    if (AssetImportTracker::resolve_import(*ctx.project, AssetType::MESH, job.dst_lmesh, job.mesh_guid, job.mesh_content_bin) != Error::OK) {
        log_write("Model import failed: %s", p_src.string().c_str());
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

    job.progress = ctx.imports->add(job.dst_lmesh, job.mesh_guid, job.mesh_content_bin, nullptr, false);
    ctx.imports->add(job.dst_lmodel, job.model_guid, {}, job.progress, true);
    ctx.tasks->dispatch([job]{
        if (ModelImporter::_import(job) != Error::OK && job.progress && !job.progress->cancel.load(std::memory_order_relaxed)) job.progress->progress.store(1.0f, std::memory_order_relaxed);
    });
}

}