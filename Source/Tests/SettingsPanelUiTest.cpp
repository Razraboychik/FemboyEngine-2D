// FemboyEngine2D - generated settings panel render smoke test.
#include "Editor/Panels/SettingsRegistryPanel.hpp"

#include <imgui.h>

#include <array>
#include <cstdio>

int main()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& ScopedIo = ImGui::GetIO();
    ScopedIo.DisplaySize = ImVec2(1280.0f, 720.0f);
    ScopedIo.DeltaTime = 1.0f / 60.0f;
    ScopedIo.Fonts->AddFontDefault();
    ScopedIo.Fonts->Build();
    ImGui::StyleColorsDark();

    const std::array<std::string_view, 4> ScopedPolicies{"full", "no_world", "disabled", "headless"};
    Fby::Settings::Registry ScopedRegistry;
    ScopedRegistry.Bool("accessibility.deaf").Category("Accessibility").Label("Deaf mode").Default(Fby::Settings::Value::MakeBool(false));
    ScopedRegistry.Enum("render.policy")
        .Category("Accessibility")
        .Label("Render output")
        .Enum(ScopedPolicies)
        .Default(Fby::Settings::Value::MakeEnum("full"))
        .Safe(Fby::Settings::Value::MakeEnum("full"))
        .Scope(Fby::Settings::Scope::User)
        .Dangerous("Changing render output can hide the interface.", "Ctrl+Alt+F12");
    if (!ScopedRegistry.Freeze())
    {
        std::fprintf(stderr, "FAIL: ui schema freeze\n");
        ImGui::DestroyContext();
        return 1;
    }

    Fby::Editor::SettingsRegistryPanel ScopedPanel;
    ScopedPanel.SetRegistry(&ScopedRegistry, Fby::Settings::Scope::User);
    bool ScopedOpen = true;
    for (int ScopedFrame = 0; ScopedFrame < 3; ++ScopedFrame)
    {
        ImGui::NewFrame();
        ScopedPanel.Draw("Generated settings", &ScopedOpen);
        ImGui::Render();
    }
    ImGui::DestroyContext();
    std::printf("settings_panel_ui: OK\n");
    return 0;
}
