#pragma once
#include <core/assets/asset_common.h>
#include <core/assets/guid.h>
#include <editor/application/editor_context.h>
#include <imgui.h>

namespace lumen {

ImU32 asset_type_color(AssetType p_type);
const char* asset_type_label(AssetType p_type);
void asset_tooltip(EditorContext& ctx, Guid p_guid, AssetType p_type);
bool asset_picker(EditorContext& ctx, const char* p_popup_id, Guid& r_guid, AssetType p_type);
bool asset_field(EditorContext& ctx, const char* p_label, const char* p_id, Guid& r_guid, AssetType p_type);

}
