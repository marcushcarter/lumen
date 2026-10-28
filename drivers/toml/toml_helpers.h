#pragma once
#include <toml++/toml.hpp>
#include <imgui.h>
#include <string>

namespace lumen {

toml::array to_toml(const ImVec4& v);
ImVec4 from_toml(const toml::node_view<const toml::node>& n, ImVec4 fallback);

std::string to_toml(std::string_view s);
std::string from_toml(const toml::node_view<const toml::node>& n, std::string_view fallback);

}
