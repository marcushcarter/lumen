#pragma once
#include <glm/glm.hpp>
#include <vector>

namespace lumen {

using namespace glm;

struct BuoyancySample
{
    vec3 local = vec3(0.0f);
    float volume = 0.0f;
    float radius = 0.0f;
    vec3 world = vec3(0.0f);
    float submerged = -1.0f;
};

struct BuoyancyBody
{
    std::vector<BuoyancySample> samples;
    float total_volume = 0.0f;
    float submerged = 0.0f;
    float splash_timer = 0.0f;
};

struct WaterQuery
{
    virtual ~WaterQuery() = default;
    virtual bool sample(float p_x, float p_z, float& r_surface_y, vec2& r_flow) const = 0;
    virtual void push(const vec3& p_point, float p_submerged, float p_volume, const vec3& p_velocity, const vec3& p_relative) { (void)p_point; (void)p_submerged; (void)p_volume; (void)p_velocity; (void)p_relative; }
    virtual void splash(const vec3& p_point, float p_strength, float p_radius) { (void)p_point; (void)p_strength; (void)p_radius; }
};

struct NoWater : WaterQuery
{
    bool sample(float, float, float&, vec2&) const override { return false; }
};

struct FlatWater : WaterQuery
{
    float level = 0.0f;

    bool sample(float, float, float& r_surface_y, vec2& r_flow) const override
    {
        r_surface_y = level;
        r_flow = vec2(0.0f);
        return true;
    }
};

}
