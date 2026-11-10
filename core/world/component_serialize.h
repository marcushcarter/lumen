#pragma once
#include <core/world/archive.h>
#include <core/world/components.h>
#include <core/world/world_settings.h>

namespace lumen {

const char* camera_state_key(CameraState p_state);
void serialize(Archive& a, NameComponent& r_c);
void serialize(Archive& a, TransformComponent& r_c);
void serialize(Archive& a, ModelComponent& r_c);
void serialize(Archive& a, ModelGridComponent& r_c);
void serialize(Archive& a, AnimationComponent& r_c);
void serialize(Archive& a, PlayerSettings& r_c);
void serialize(Archive& a, CollisionComponent& r_c);
void serialize(Archive& a, CharacterComponent& r_c);
void serialize(Archive& a, ParentComponent& r_c);
void serialize(Archive& a, PrefabInstanceComponent& r_c);
void serialize(Archive& a, EnvironmentSettings& r_c);

}
