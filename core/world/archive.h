#pragma once
#include <core/assets/guid.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstddef>
#include <cstdint>
#include <string>

namespace lumen {

using namespace glm;

struct Archive
{
    bool loading = false;

    virtual ~Archive() = default;
    virtual void field(const char* p_key, bool& r_value) = 0;
    virtual void field(const char* p_key, int32_t& r_value) = 0;
    virtual void field(const char* p_key, uint32_t& r_value) = 0;
    virtual void field(const char* p_key, float& r_value) = 0;
    virtual void field(const char* p_key, vec2& r_value) = 0;
    virtual void field(const char* p_key, vec3& r_value) = 0;
    virtual void field(const char* p_key, vec4& r_value) = 0;
    virtual void field(const char* p_key, uvec3& r_value) = 0;
    virtual void field(const char* p_key, quat& r_value) = 0;
    virtual void field(const char* p_key, Guid& r_value) = 0;
    virtual void field(const char* p_key, std::string& r_value) = 0;
    virtual void field(const char* p_key, char* r_text, size_t p_capacity) = 0;

    template <size_t N>
    void field(const char* p_key, char (&r_text)[N]) { field(p_key, r_text, N); }
};

}
