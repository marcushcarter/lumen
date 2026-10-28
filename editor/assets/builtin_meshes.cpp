#include <editor/assets/builtin_meshes.h>
#include <editor/assets/import/mesh_cooker.h>
#include <core/assets/asset_common.h>
#include <core/assets/lmesh.h>
#include <core/io/path.h>
#include <core/base/error.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <cmath>
#include <cstdio>
#include <string>

namespace lumen {

std::filesystem::path BuiltinMeshes::sphere_bin()
{
    char name[32];
    std::snprintf(name, sizeof(name), "sphere_r%u_v%u.bin", SPHERE_REVISION, LMESH_VERSION);
    return Paths::local_data(L"builtin") / name;
}

static std::string _sphere_obj()
{
    const uint32_t seg = BuiltinMeshes::SPHERE_SEGMENTS;
    const uint32_t rings = BuiltinMeshes::SPHERE_RINGS;
    const float r = BuiltinMeshes::SPHERE_RADIUS;
    const float pi = 3.14159265358979f;

    std::string obj;
    obj.reserve((size_t)(seg + 1) * (rings + 1) * 96 + (size_t)seg * rings * 64);
    char line[128];
    for (uint32_t y = 0; y <= rings; ++y) {
        const float v = (float)y / (float)rings;
        const float theta = v * pi;
        for (uint32_t x = 0; x <= seg; ++x) {
            const float u = (float)x / (float)seg;
            const float phi = u * 2.0f * pi;
            const float nx = std::sin(theta) * std::cos(phi);
            const float ny = std::cos(theta);
            const float nz = -std::sin(theta) * std::sin(phi);
            std::snprintf(line, sizeof(line), "v %.7f %.7f %.7f\nvn %.7f %.7f %.7f\nvt %.7f %.7f\n", nx * r, ny * r, nz * r, nx, ny, nz, u, 1.0f - v);
            obj += line;
        }
    }
    const uint32_t row = seg + 1;
    for (uint32_t y = 0; y < rings; ++y) {
        for (uint32_t x = 0; x < seg; ++x) {
            const uint32_t a = y * row + x + 1;
            const uint32_t b = a + row;
            const uint32_t c = b + 1;
            const uint32_t d = a + 1;
            if (y != 0) {
                std::snprintf(line, sizeof(line), "f %u/%u/%u %u/%u/%u %u/%u/%u\n", a, a, a, b, b, b, d, d, d);
                obj += line;
            }
            if (y != rings - 1) {
                std::snprintf(line, sizeof(line), "f %u/%u/%u %u/%u/%u %u/%u/%u\n", d, d, d, b, b, b, c, c, c);
                obj += line;
            }
        }
    }
    return obj;
}

bool BuiltinMeshes::ensure_sphere()
{
    const std::filesystem::path bin = sphere_bin();
    LAssetHeader ah{};
    if (read_asset_header(bin, ah) && ah.type == AssetType::MESH && ah.version == LMESH_VERSION && ah.guid == SPHERE) return true;

    const std::string obj = _sphere_obj();
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFileFromMemory(obj.data(), obj.size(), MeshCooker::import_flags(), "obj");
    if (!scene) {
        log_write("BuiltinMeshes: sphere generation failed (%s)", importer.GetErrorString());
        return false;
    }

    MeshCooker::Job job;
    job.source = "builtin://sphere";
    job.dst_lmesh = std::filesystem::path(bin).replace_extension(".lmesh");
    job.content_bin = bin;
    job.guid = SPHERE;
    if (MeshCooker::cook_scene(job, scene) != Error::OK) {
        log_write("BuiltinMeshes: sphere cook failed");
        return false;
    }
    return true;
}

}
