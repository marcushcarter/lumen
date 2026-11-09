#include <core/world/component_serialize.h>

namespace lumen {

void serialize(Archive& a, NameComponent& r_c)
{
    a.field("name", r_c.name);
}

void serialize(Archive& a, TransformComponent& r_c)
{
    a.field("position", r_c.position);
    a.field("rotation", r_c.rotation);
    a.field("scale", r_c.scale);
}

void serialize(Archive& a, ModelComponent& r_c)
{
    a.field("model", r_c.model);
}

void serialize(Archive& a, ModelGridComponent& r_c)
{
    a.field("model", r_c.model);
    a.field("count", r_c.count);
    a.field("spacing", r_c.spacing);
}

void serialize(Archive& a, ParentComponent& r_c)
{
    a.field("parent", r_c.parent);
}

void serialize(Archive& a, EnvironmentSettings& r_c)
{
    a.field("manual_ev100", r_c.manual_ev100);
}

}
