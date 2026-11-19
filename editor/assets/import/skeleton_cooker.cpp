#include <editor/assets/import/skeleton_cooker.h>
#include <core/assets/asset_common.h>
#include <core/base/error.h>
#include <core/io/file_io.h>
#include <core/project/project.h>
#include <assimp/scene.h>
#include <glm/gtc/matrix_transform.hpp>
#include <toml++/toml.hpp>
#include <algorithm>
#include <cstdio>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace lumen {

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

static void _decompose(const mat4& p_m, JointTransform& r_t)
{
    r_t.translation = vec3(p_m[3]);
    r_t.scale = vec3(length(vec3(p_m[0])), length(vec3(p_m[1])), length(vec3(p_m[2])));
    if (determinant(mat3(p_m)) < 0.0f) r_t.scale.x = -r_t.scale.x;
    mat3 rot(1.0f);
    for (int c = 0; c < 3; c++) {
        if (r_t.scale[c] != 0.0f) rot[c] = vec3(p_m[c]) / r_t.scale[c];
    }
    r_t.rotation = normalize(quat_cast(rot));
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

bool SkeletonCooker::build(const aiScene* p_scene, const MeshCooker::CookSettings& p_settings, LSkeleton& r_skeleton)
{
    r_skeleton = {};
    if (!p_scene || !p_scene->mRootNode) return false;

    std::vector<const aiNode*> bones;
    for (uint32_t m = 0; m < p_scene->mNumMeshes; m++) {
        const aiMesh* mesh = p_scene->mMeshes[m];
        for (uint32_t b = 0; b < mesh->mNumBones; b++) {
            const aiNode* node = p_scene->mRootNode->FindNode(mesh->mBones[b]->mName);
            if (!node) {
                log_write("SkeletonCooker: bone '%s' has no node in the scene.", mesh->mBones[b]->mName.C_Str());
                return false;
            }
            if (std::find(bones.begin(), bones.end(), node) == bones.end()) bones.push_back(node);
        }
    }
    if (bones.empty()) return false;

    const aiNode* root = nullptr;
    for (const aiNode* n = bones[0]; n && !root; n = n->mParent) {
        bool all = true;
        for (const aiNode* b : bones) all = all && _is_ancestor(n, b);
        if (all) root = n;
    }
    std::unordered_set<const aiNode*> marked;
    for (const aiNode* b : bones) {
        for (const aiNode* n = b; n; n = n->mParent) {
            marked.insert(n);
            if (n == root) break;
        }
    }

    std::vector<const aiNode*> nodes;
    _order_joints(root, SKELETON_NO_JOINT, marked, nodes, r_skeleton.parents);
    if (nodes.size() > SKELETON_MAX_JOINTS) {
        log_write("SkeletonCooker: %zu joints is over the limit of %u.", nodes.size(), SKELETON_MAX_JOINTS);
        return false;
    }

    const mat4 import = translate(mat4(1.0f), p_settings.translation) * mat4_cast(quat(radians(p_settings.rotation))) * glm::scale(mat4(1.0f), vec3(p_settings.scale));
    std::vector<mat4> global(nodes.size());
    r_skeleton.names.resize(nodes.size());
    r_skeleton.rest.resize(nodes.size());
    for (size_t i = 0; i < nodes.size(); i++) {
        r_skeleton.names[i] = nodes[i]->mName.C_Str();
        global[i] = import * _node_world(nodes[i]);
        const int32_t parent = r_skeleton.parents[i];
        _decompose(parent == SKELETON_NO_JOINT ? global[i] : inverse(global[parent]) * global[i], r_skeleton.rest[i]);
    }
    return true;
}

}
