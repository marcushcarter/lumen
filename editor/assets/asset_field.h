#pragma once
#include <core/assets/asset_common.h>
#include <core/assets/guid.h>
#include <editor/editor_context.h>
#include <functional>

namespace lumen {

static constexpr float ASSET_THUMBNAIL_FIELD_SIZE = 64.0f;

bool asset_field(EditorContext& ctx, const char* p_id, Guid& r_guid, AssetType p_type, const char* p_hint, const std::function<void()>& p_tooltip = {});
bool asset_thumbnail_field(EditorContext& ctx, const char* p_id, Guid& r_guid, AssetType p_type, const char* p_hint, const std::function<void()>& p_tooltip = {});

}