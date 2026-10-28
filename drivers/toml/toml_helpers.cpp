#include <drivers/toml/toml_helpers.h>

namespace lumen {

toml::array to_toml(const ImVec4& v) { return toml::array{v.x, v.y, v.z, v.w}; }

ImVec4 from_toml(const toml::node_view<const toml::node>& n, ImVec4 fallback)
{
    const toml::array* arr = n.as_array();
    if (!arr) return fallback;

    const size_t count = arr->size();
    if (count != 3 && count != 4) return fallback;

    ImVec4 out = fallback;
    out.x = static_cast<float>((*arr)[0].value_or(0.0));
    out.y = static_cast<float>((*arr)[1].value_or(0.0));
    out.z = static_cast<float>((*arr)[2].value_or(0.0));
    if (count == 4) out.w = static_cast<float>((*arr)[3].value_or(1.0));
    return out;
}

std::string to_toml(std::string_view s) { return std::string(s); }

std::string from_toml(const toml::node_view<const toml::node>& n, std::string_view fallback)
{
    if (auto v = n.value<std::string>()) return std::move(*v);
    return std::string(fallback);
}

}
