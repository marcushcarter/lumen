#include <core/assets/lmesh.h>

namespace lumen {

const char* mesh_dag_level_name(MeshDagLevel p_level)
{
    switch (p_level) {
        case MeshDagLevel::FLAT: return "flat";
        case MeshDagLevel::CLUSTERED: return "clustered";
        case MeshDagLevel::CLUSTERED_DAG: return "clustered_dag";
    }
    return "clustered_dag";
}

MeshDagLevel mesh_dag_level_from_name(const std::string& p_name, MeshDagLevel p_fallback)
{
    if (p_name == "flat") return MeshDagLevel::FLAT;
    if (p_name == "clustered") return MeshDagLevel::CLUSTERED;
    if (p_name == "clustered_dag") return MeshDagLevel::CLUSTERED_DAG;
    return p_fallback;
}

}