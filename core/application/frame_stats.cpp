// core/application/frame_stats.cpp
#include <core/application/frame_stats.h>
#include <imgui.h>
#include <cmath>

namespace lumen {

void FrameStats::initialize(HWND p_hwnd)
{
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    ticks_to_ms = 1000.0 / (double)freq.QuadPart;

    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (GetMonitorInfoW(MonitorFromWindow(p_hwnd, MONITOR_DEFAULTTONEAREST), &mi) && EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm))
        refresh_hz = dm.dmDisplayFrequency;
}

void FrameStats::begin_frame()
{
    const int64_t now = _now();
    if (frame_start != 0) {
        current.frame_ms = (float)((double)(now - frame_start) * ticks_to_ms);
        samples[sample_head] = current;
        sample_head = (sample_head + 1) % HISTORY;
        if (sample_count < HISTORY) sample_count++;

        resolve_timer += current.frame_ms * 0.001f;
        if (resolve_timer >= RESOLVE_INTERVAL_S) {
            resolve_timer = 0.0f;
            _resolve();
        }
    }
    current = Sample{};
    frame_start = now;
    lap_start = now;
}

void FrameStats::lap(Zone p_zone)
{
    const int64_t now = _now();
    current.zone_ms[(uint32_t)p_zone] += (float)((double)(now - lap_start) * ticks_to_ms);
    lap_start = now;
}

void FrameStats::_resolve()
{
    avg = Sample{};
    peak = Sample{};
    for (uint32_t i = 0; i < sample_count; i++) {
        const Sample& s = samples[i];
        avg.frame_ms += s.frame_ms;
        if (s.frame_ms > peak.frame_ms) peak.frame_ms = s.frame_ms;
        for (uint32_t z = 0; z < ZONE_COUNT; z++) {
            avg.zone_ms[z] += s.zone_ms[z];
            if (s.zone_ms[z] > peak.zone_ms[z]) peak.zone_ms[z] = s.zone_ms[z];
        }
    }
    if (!sample_count) return;
    const float inv = 1.0f / (float)sample_count;
    avg.frame_ms *= inv;
    for (uint32_t z = 0; z < ZONE_COUNT; z++) avg.zone_ms[z] *= inv;
}

void FrameStats::draw()
{
    if (ImGui::IsKeyPressed(ImGuiKey_F3, false)) visible = !visible;
    if (!visible || avg.frame_ms <= 0.0f) return;

    float tracked = 0.0f;
    for (uint32_t z = 0; z < ZONE_COUNT; z++) tracked += avg.zone_ms[z];
    const float wait = avg.zone_ms[(uint32_t)Zone::GpuWait];
    const float refresh_ms = refresh_hz ? 1000.0f / (float)refresh_hz : 0.0f;
    const bool refresh_locked = refresh_ms > 0.0f && std::fabs(avg.frame_ms - refresh_ms) < refresh_ms * 0.03f;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 10.0f, vp->WorkPos.y + 10.0f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.8f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoDocking;
    if (!ImGui::Begin("##frame_stats", nullptr, flags)) { ImGui::End(); return; }

    ImGui::Text("frame %.2f ms (%.0f fps)  max %.2f", avg.frame_ms, 1000.0f / avg.frame_ms, peak.frame_ms);
    // ImGui::Text("cpu busy %.2f ms  (frame - fence wait)", avg.frame_ms - wait);
    ImGui::Text("cpu busy %.2f ms  (frame - gpu/acquire wait)", avg.frame_ms - wait);
    if (refresh_hz) ImGui::TextDisabled("display %u Hz = %.2f ms", refresh_hz, refresh_ms);
    if (refresh_locked) ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "frame time == refresh interval: present-bound, not cpu");

    if (ImGui::BeginTable("##zones", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("zone");
        ImGui::TableSetupColumn("avg", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn("max", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableHeadersRow();
        for (uint32_t z = 0; z < ZONE_COUNT; z++) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (z == (uint32_t)Zone::GpuWait) ImGui::TextDisabled("%s", ZONE_NAMES[z]);
            else ImGui::TextUnformatted(ZONE_NAMES[z]);
            ImGui::TableSetColumnIndex(1); ImGui::Text("%.3f", avg.zone_ms[z]);
            ImGui::TableSetColumnIndex(2); ImGui::Text("%.3f", peak.zone_ms[z]);
        }
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("untracked");
        ImGui::TableSetColumnIndex(1); ImGui::Text("%.3f", avg.frame_ms - tracked);
        ImGui::EndTable();
    }

    ImGui::End();
}

}