#pragma once
#include <core/assets/asset_common.h>
#include <core/assets/guid.h>
#include <editor/application/editor_context.h>
#include <functional>

namespace lumen {

bool asset_field(EditorContext& ctx, const char* p_label, const char* p_id, Guid& r_guid, AssetType p_type, const std::function<void()>& p_tooltip = {});

}
