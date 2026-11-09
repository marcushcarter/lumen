#pragma once
#include <core/base/hash.h>
#include <core/world/archive.h>
#include <toml++/toml.hpp>
#include <bit>
#include <cstdio>
#include <cstring>

namespace lumen {

inline bool toml_has_string(const toml::array* p_list, const char* p_value)
{
    if (!p_list) return false;
    for (const toml::node& n : *p_list) {
        if (n.value<std::string>() == p_value) return true;
    }
    return false;
}

/*****************/
/**** WRITING ****/
/*****************/

struct TomlWriter : Archive
{
    toml::table& table;

    explicit TomlWriter(toml::table& r_table) : table(r_table) {}

    void field(const char* p_key, bool& r_value) override { table.insert_or_assign(p_key, r_value); }
    void field(const char* p_key, int32_t& r_value) override { table.insert_or_assign(p_key, (int64_t)r_value); }
    void field(const char* p_key, uint32_t& r_value) override { table.insert_or_assign(p_key, (int64_t)r_value); }
    void field(const char* p_key, float& r_value) override { table.insert_or_assign(p_key, (double)r_value); }
    void field(const char* p_key, vec2& r_value) override { table.insert_or_assign(p_key, toml::array{ (double)r_value.x, (double)r_value.y }); }
    void field(const char* p_key, vec3& r_value) override { table.insert_or_assign(p_key, toml::array{ (double)r_value.x, (double)r_value.y, (double)r_value.z }); }
    void field(const char* p_key, vec4& r_value) override { table.insert_or_assign(p_key, toml::array{ (double)r_value.x, (double)r_value.y, (double)r_value.z, (double)r_value.w }); }
    void field(const char* p_key, uvec3& r_value) override { table.insert_or_assign(p_key, toml::array{ (int64_t)r_value.x, (int64_t)r_value.y, (int64_t)r_value.z }); }
    void field(const char* p_key, quat& r_value) override { table.insert_or_assign(p_key, toml::array{ (double)r_value.x, (double)r_value.y, (double)r_value.z, (double)r_value.w }); }
    void field(const char* p_key, Guid& r_value) override { table.insert_or_assign(p_key, r_value.valid() ? r_value.to_string() : std::string{}); }
    void field(const char* p_key, std::string& r_value) override { table.insert_or_assign(p_key, r_value); }
    void field(const char* p_key, char* r_text, size_t p_capacity) override { table.insert_or_assign(p_key, std::string(r_text, strnlen(r_text, p_capacity))); }
};

/*****************/
/**** READING ****/
/*****************/

struct TomlReader : Archive
{
    const toml::table& table;

    explicit TomlReader(const toml::table& p_table) : table(p_table) { loading = true; }

    template <typename T>
    void _number(const char* p_key, T& r_value)
    {
        if (const toml::node* n = table.get(p_key)) {
            if (n->is_integer()) r_value = (T)n->as_integer()->get();
            else if (n->is_floating_point()) r_value = (T)n->as_floating_point()->get();
        }
    }

    template <int N>
    bool _floats(const char* p_key, float (&r_out)[N])
    {
        const toml::array* list = table.get_as<toml::array>(p_key);
        if (!list || list->size() != N) return false;
        for (int i = 0; i < N; i++) {
            const toml::node& n = (*list)[i];
            if (n.is_floating_point()) r_out[i] = (float)n.as_floating_point()->get();
            else if (n.is_integer()) r_out[i] = (float)n.as_integer()->get();
            else return false;
        }
        return true;
    }

    void field(const char* p_key, bool& r_value) override { if (const toml::value<bool>* v = table.get_as<bool>(p_key)) r_value = v->get(); }
    void field(const char* p_key, int32_t& r_value) override { _number(p_key, r_value); }
    void field(const char* p_key, uint32_t& r_value) override { _number(p_key, r_value); }
    void field(const char* p_key, float& r_value) override { _number(p_key, r_value); }
    void field(const char* p_key, vec2& r_value) override { float f[2]; if (_floats(p_key, f)) r_value = vec2(f[0], f[1]); }
    void field(const char* p_key, vec3& r_value) override { float f[3]; if (_floats(p_key, f)) r_value = vec3(f[0], f[1], f[2]); }
    void field(const char* p_key, vec4& r_value) override { float f[4]; if (_floats(p_key, f)) r_value = vec4(f[0], f[1], f[2], f[3]); }
    void field(const char* p_key, uvec3& r_value) override { float f[3]; if (_floats(p_key, f)) r_value = uvec3((uint32_t)f[0], (uint32_t)f[1], (uint32_t)f[2]); }
    void field(const char* p_key, quat& r_value) override { float f[4]; if (_floats(p_key, f)) r_value = normalize(quat(f[3], f[0], f[1], f[2])); }
    void field(const char* p_key, Guid& r_value) override { if (const toml::value<std::string>* v = table.get_as<std::string>(p_key)) r_value = Guid::from_string(v->get()); }
    void field(const char* p_key, std::string& r_value) override { if (const toml::value<std::string>* v = table.get_as<std::string>(p_key)) r_value = v->get(); }
    void field(const char* p_key, char* r_text, size_t p_capacity) override
    {
        if (const toml::value<std::string>* v = table.get_as<std::string>(p_key)) std::snprintf(r_text, p_capacity, "%s", v->get().c_str());
    }
};

/*****************/
/**** HASHING ****/
/*****************/

struct HashWriter : Archive
{
    uint64_t hash = HASH_FNV_OFFSET;

    void _key(const char* p_key) { hash = hash_fnv(p_key, hash); }
    void _value(uint64_t p_value) { hash = hash_fnv_step(hash, p_value); }
    void _float(float p_value) { _value(std::bit_cast<uint32_t>(p_value)); }

    void field(const char* p_key, bool& r_value) override { _key(p_key); _value(r_value); }
    void field(const char* p_key, int32_t& r_value) override { _key(p_key); _value((uint32_t)r_value); }
    void field(const char* p_key, uint32_t& r_value) override { _key(p_key); _value(r_value); }
    void field(const char* p_key, float& r_value) override { _key(p_key); _float(r_value); }
    void field(const char* p_key, vec2& r_value) override { _key(p_key); _float(r_value.x); _float(r_value.y); }
    void field(const char* p_key, vec3& r_value) override { _key(p_key); _float(r_value.x); _float(r_value.y); _float(r_value.z); }
    void field(const char* p_key, vec4& r_value) override { _key(p_key); _float(r_value.x); _float(r_value.y); _float(r_value.z); _float(r_value.w); }
    void field(const char* p_key, uvec3& r_value) override { _key(p_key); _value(r_value.x); _value(r_value.y); _value(r_value.z); }
    void field(const char* p_key, quat& r_value) override { _key(p_key); _float(r_value.x); _float(r_value.y); _float(r_value.z); _float(r_value.w); }
    void field(const char* p_key, Guid& r_value) override { _key(p_key); _value(r_value.value); }
    void field(const char* p_key, std::string& r_value) override { _key(p_key); hash = hash_fnv(r_value, hash); }
    void field(const char* p_key, char* r_text, size_t p_capacity) override { _key(p_key); hash = hash_fnv(std::string_view(r_text, strnlen(r_text, p_capacity)), hash); }
};

}
