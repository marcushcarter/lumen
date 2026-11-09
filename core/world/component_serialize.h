#pragma once
#include <core/world/archive.h>
#include <core/world/components.h>
#include <core/world/world_settings.h>

namespace lumen {

void serialize(Archive& a, NameComponent& r_c);
void serialize(Archive& a, TransformComponent& r_c);
void serialize(Archive& a, ModelComponent& r_c);
void serialize(Archive& a, ModelGridComponent& r_c);
void serialize(Archive& a, ParentComponent& r_c);
void serialize(Archive& a, EnvironmentSettings& r_c);

}
