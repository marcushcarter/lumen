#pragma once
#include <core/scene/archive.h>
#include <core/scene/components.h>
#include <core/scene/scene_settings.h>

namespace lumen {

void serialize(Archive& a, NameComponent& r_c);
void serialize(Archive& a, TransformComponent& r_c);
void serialize(Archive& a, MeshComponent& r_c);
void serialize(Archive& a, CollisionComponent& r_c);
void serialize(Archive& a, MovementComponent& r_c);
void serialize(Archive& a, CameraComponent& r_c);
void serialize(Archive& a, CharacterComponent& r_c);
void serialize(Archive& a, LookAtComponent& r_c);
void serialize(Archive& a, AttentionPointComponent& r_c);
void serialize(Archive& a, WaterSourceComponent& r_c);
void serialize(Archive& a, TerrainSettings& r_c);
void serialize(Archive& a, WaterSettings& r_c);
void serialize(Archive& a, ParentComponent& r_c);
void serialize(Archive& a, TemplateInstanceComponent& r_c);
void serialize(Archive& a, EnvironmentSettings& r_c);
void serialize(Archive& a, ShadowSettings& r_c);
void serialize(Archive& a, PhysicsSettings& r_c);

}
