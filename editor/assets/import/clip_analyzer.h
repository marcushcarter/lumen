#pragma once
#include <core/assets/lanim.h>
#include <string>
#include <string_view>
#include <vector>

namespace lumen {

std::vector<std::string> clip_name_tokens(std::string_view p_name);
void clip_name_tags(std::string_view p_name, ClipMeta& r_meta);
void clip_analyze(LAnimation& r_clip, const LSkeleton* p_skeleton, std::string_view p_name);

}
