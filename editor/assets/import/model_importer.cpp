#include <editor/assets/import/model_importer.h>
#include <editor/editor_context.h>
#include <core/assets/asset_common.h>
#include <core/assets/lmodel.h>
#include <core/project/project.h>
#include <core/base/tasks.h>
#include <core/base/error.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/material.h>
#include <string>
#include <vector>

namespace lumen {

struct TextureRef {
    std::filesystem::path source;
    TextureUsage usage;
};

static constexpr aiTextureType TEXTURE_TYPES[] = {
    aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE,
    aiTextureType_NORMALS, aiTextureType_NORMAL_CAMERA, aiTextureType_HEIGHT,
    aiTextureType_METALNESS, aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_AMBIENT_OCCLUSION,
    aiTextureType_EMISSIVE, aiTextureType_EMISSION_COLOR,
};

static TextureUsage _texture_usage(aiTextureType p_type, const std::filesystem::path& p_path)
{
    switch (p_type) {
        case aiTextureType_NORMALS:
        case aiTextureType_NORMAL_CAMERA: return TextureUsage::NORMAL;
        case aiTextureType_HEIGHT: return texture_usage_guess(p_path) == TextureUsage::NORMAL ? TextureUsage::NORMAL : TextureUsage::LINEAR;
        case aiTextureType_METALNESS:
        case aiTextureType_DIFFUSE_ROUGHNESS:
        case aiTextureType_AMBIENT_OCCLUSION: return TextureUsage::MASK;
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

static std::vector<TextureCooker::Job> _texture_jobs(const ModelImporter::Job& p_job, const aiScene* p_scene)
{
    std::vector<TextureCooker::Job> jobs;
    const std::filesystem::path source_dir = p_job.source.parent_path();
    std::vector<TextureRef> refs;
    bool embedded_logged = false;
    for (uint32_t m = 0; m < p_scene->mNumMaterials; ++m) {
        const aiMaterial* mat = p_scene->mMaterials[m];
        for (aiTextureType type : TEXTURE_TYPES) {
            for (uint32_t i = 0; i < mat->GetTextureCount(type); ++i) {
                aiString ref;
                if (mat->GetTexture(type, i, &ref) != AI_SUCCESS || ref.length == 0) continue;
                if (ref.C_Str()[0] == '*') {
                    if (!embedded_logged) log_write("ModelImporter: embedded textures in %s are not supported yet.", p_job.source.string().c_str());
                    embedded_logged = true;
                    continue;
                }
                const std::filesystem::path path = _texture_path(source_dir, ref);
                if (path.empty()) {
                    log_write("ModelImporter: texture '%s' not found.", ref.C_Str());
                    continue;
                }
                const TextureUsage usage = _texture_usage(type, path);
                bool seen = false;
                for (const TextureRef& r : refs) seen = seen || (r.source == path && r.usage == usage);
                if (!seen) refs.push_back({ path, usage });
            }
        }
    }
    if (refs.empty()) return jobs;

    const std::filesystem::path& dir = p_job.dst_dir;

    std::vector<std::filesystem::path> used;
    for (const TextureRef& r : refs) {
        std::filesystem::path dst = dir / (r.source.stem().wstring() + L".ltexture");
        for (const std::filesystem::path& u : used) {
            if (u == dst) dst = dir / (r.source.stem().string() + "_" + texture_usage_name(r.usage) + ".ltexture");
        }
        used.push_back(dst);

        TextureCooker::Job tex;
        tex.source = r.source;
        tex.dst_ltexture = dst;
        tex.settings = p_job.settings.texture_settings;
        tex.settings.usage = r.usage;
        if (AssetImportTracker::resolve_import(*p_job.project, AssetType::TEXTURE, dst, tex.guid, tex.content_bin) != Error::OK) {
            log_write("ModelImporter: '%s' exists and is not a texture.", dst.string().c_str());
            continue;
        }
        tex.progress = std::make_shared<ImportControl>();
        if (p_job.progress) p_job.progress->push_child({ dst, tex.guid, tex.content_bin, tex.progress });
        jobs.push_back(std::move(tex));
    }
    return jobs;
}

static std::filesystem::path _model_folder(const std::filesystem::path& p_dst_dir, const std::filesystem::path& p_src)
{
    const std::wstring stem = p_src.stem().wstring();
    const std::string source = p_src.generic_string();
    std::error_code ec;
    for (uint32_t i = 0;; ++i) {
        const std::filesystem::path folder = p_dst_dir / (i == 0 ? stem : stem + L" " + std::to_wstring(i));
        if (!std::filesystem::exists(folder, ec)) return folder;
        const std::filesystem::path lmodel = folder / (stem + L".lmodel");
        LModel existing;
        if (std::filesystem::exists(lmodel, ec) && existing.load(lmodel) == Error::OK && existing.import.source == source) return folder;
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

    std::vector<TextureCooker::Job> textures;
    if (p_job.settings.mode == ModelImportMode::FULL) textures = _texture_jobs(p_job, scene);

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
    const std::filesystem::path folder = _model_folder(p_dst_dir, p_src);
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
    job.dst_lmesh = folder / (p_src.stem().wstring() + L".lmesh");
    job.dst_lmodel = folder / (p_src.stem().wstring() + L".lmodel");

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