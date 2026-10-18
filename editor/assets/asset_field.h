#pragma once
#include <core/assets/asset_common.h>
#include <core/assets/guid.h>
#include <editor/editor_context.h>

namespace lumen {

bool asset_field(EditorContext& ctx, const char* p_id, Guid& r_guid, AssetType p_type, const char* p_hint);

}