#include <editor/assets/import/skeleton_cooker.h>
#include <core/assets/asset_common.h>
#include <core/base/error.h>
#include <core/io/file_io.h>
#include <assimp/scene.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <toml++/toml.hpp>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace lumen {

struct BoneBind { std::string name; mat4 offset; };

static mat4 _to_glm(const aiMatrix4x4& m)
{
    return mat4(m.a1, m.b1, m.c1, m.d1, m.a2, m.b2, m.c2, m.d2, m.a3, m.b3, m.c3, m.d3, m.a4, m.b4, m.c4, m.d4);
}

static mat4 _node_world(const aiNode* p_node)
{
    mat4 m(1.0f);
    for (const aiNode* n = p_node; n; n = n->mParent) m = _to_glm(n->mTransformation) * m;
    return m;
}

static bool _is_ancestor(const aiNode* p_ancestor, const aiNode* p_node)
{
    for (const aiNode* n = p_node; n; n = n->mParent) {
        if (n == p_ancestor) return true;
    }
    return false;
}

static void _collect_bones(const aiScene* p_scene, const aiNode* p_node, const mat4& p_parent, std::vector<BoneBind>& r_bones)
{
    const mat4 world = p_parent * _to_glm(p_node->mTransformation);
    for (uint32_t i = 0; i < p_node->mNumMeshes; i++) {
        const aiMesh* mesh = p_scene->mMeshes[p_node->mMeshes[i]];
        for (uint32_t b = 0; b < mesh->mNumBones; b++) {
            const aiBone* bone = mesh->mBones[b];
            bool seen = false;
            for (const BoneBind& e : r_bones) seen = seen || e.name == bone->mName.C_Str();
            if (!seen) r_bones.push_back({ bone->mName.C_Str(), _to_glm(bone->mOffsetMatrix) * inverse(world) });
        }
    }
    for (uint32_t c = 0; c < p_node->mNumChildren; c++) _collect_bones(p_scene, p_node->mChildren[c], world, r_bones);
}

static void _order_joints(const aiNode* p_node, int32_t p_parent, const std::unordered_set<const aiNode*>& p_marked, std::vector<const aiNode*>& r_nodes, std::vector<int32_t>& r_parents)
{
    int32_t self = p_parent;
    if (p_marked.contains(p_node)) {
        self = (int32_t)r_nodes.size();
        r_nodes.push_back(p_node);
        r_parents.push_back(p_parent);
    }
    for (uint32_t c = 0; c < p_node->mNumChildren; c++) _order_joints(p_node->mChildren[c], self, p_marked, r_nodes, r_parents);
}

static void _decompose(const mat4& p_m, vec3& r_translation, quat& r_rotation, vec3& r_scale)
{
    r_translation = vec3(p_m[3]);
    r_scale = vec3(length(vec3(p_m[0])), length(vec3(p_m[1])), length(vec3(p_m[2])));
    if (determinant(mat3(p_m)) < 0.0f) r_scale.x = -r_scale.x;
    mat3 rot(1.0f);
    for (int c = 0; c < 3; c++) {
        if (r_scale[c] != 0.0f) rot[c] = vec3(p_m[c]) / r_scale[c];
    }
    r_rotation = normalize(quat_cast(rot));
}

bool SkeletonCooker::build(const aiScene* p_scene, const MeshCooker::CookSettings& p_settings, LSkeleton& r_skeleton)
{
    r_skeleton.joints.clear();
    if (!p_scene || !p_scene->mRootNode) return false;

    std::vector<BoneBind> bones;
    _collect_bones(p_scene, p_scene->mRootNode, mat4(1.0f), bones);
    if (bones.empty()) return false;

    std::vector<const aiNode*> bone_nodes;
    for (const BoneBind& b : bones) {
        const aiNode* node = p_scene->mRootNode->FindNode(b.name.c_str());
        if (!node) {
            log_write("SkeletonCooker: bone '%s' has no node in the scene.", b.name.c_str());
            return false;
        }
        bone_nodes.push_back(node);
    }

    const aiNode* root = nullptr;
    for (const aiNode* n = bone_nodes[0]; n && !root; n = n->mParent) {
        bool all = true;
        for (const aiNode* b : bone_nodes) all = all && _is_ancestor(n, b);
        if (all) root = n;
    }
    std::unordered_set<const aiNode*> marked;
    for (const aiNode* b : bone_nodes) {
        for (const aiNode* n = b; n; n = n->mParent) {
            marked.insert(n);
            if (n == root) break;
        }
    }

    std::vector<const aiNode*> nodes;
    std::vector<int32_t> parents;
    _order_joints(root, SKELETON_NO_JOINT, marked, nodes, parents);
    if (nodes.size() > SKELETON_MAX_JOINTS) {
        log_write("SkeletonCooker: %zu joints exceeds the limit of %u.", nodes.size(), SKELETON_MAX_JOINTS);
        return false;
    }

    const mat4 import = translate(mat4(1.0f), p_settings.translation) * mat4_cast(quat(radians(p_settings.rotation))) * glm::scale(mat4(1.0f), vec3(p_settings.scale));
    const mat4 import_inv = inverse(import);

    std::vector<mat4> global(nodes.size());
    r_skeleton.joints.resize(nodes.size());
    for (size_t i = 0; i < nodes.size(); i++) {
        LSkeleton::Joint& j = r_skeleton.joints[i];
        j.name = nodes[i]->mName.C_Str();
        j.parent = parents[i];
        global[i] = import * _node_world(nodes[i]);
        const mat4 local = j.parent == SKELETON_NO_JOINT ? global[i] : inverse(global[j.parent]) * global[i];
        _decompose(local, j.translation, j.rotation, j.scale);

        j.inverse_bind = inverse(global[i]);
        for (const BoneBind& b : bones) {
            if (b.name == j.name) j.inverse_bind = b.offset * import_inv;
        }
    }
    return true;
}

Error SkeletonCooker::cook(const Job& p_job, const LSkeleton& p_skeleton)
{
    using enum Error;
    if (p_skeleton.save_cooked(p_job.content_bin) != OK) {
        log_write("SkeletonCooker: failed to write '%s'.", p_job.content_bin.string().c_str());
        return FAILED;
    }

    toml::table tbl {
        { "asset", toml::table{
            { "version", static_cast<int64_t>(LASSET_VERSION) },
            { "guid", p_skeleton.guid.to_string() },
            { "type", static_cast<int64_t>(AssetType::SKELETON) } }},
        { "import", toml::table{
            { "source", p_job.source.generic_string() } }},
        { std::string(asset_type_section(AssetType::SKELETON)), toml::table{
            { "joints", static_cast<int64_t>(p_skeleton.joints.size()) } }},
    };
    std::ostringstream ss;
    ss << tbl;
    return write_file_atomic(p_job.dst_lskeleton, ss.str()) ? OK : FAILED;
}

}
