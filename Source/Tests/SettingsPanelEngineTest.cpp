// FemboyEngine2D - ImGui Test Engine interaction test for the settings panel.
#include "Editor/Panels/SettingsRegistryPanel.hpp"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_te_context.h"
#include "imgui_te_engine.h"

#include <array>
#include <cstdio>

namespace
{
Fby::Settings::Registry* GRegistry = nullptr;

void RegisterTests(ImGuiTestEngine* InEngine)
{
    ImGuiTest* ScopedTest = IM_REGISTER_TEST(InEngine, "Settings", "EnumClick");
    ScopedTest->TestFunc = [](ImGuiTestContext* InContext)
    {
        InContext->SetRef("Generated settings");
        InContext->ComboClick("Mode/b");
        IM_CHECK_STR_EQ(GRegistry->Effective("test.mode").EnumValue.c_str(), "b");
    };
}
}

int main()
{
    ImGui::CreateContext();
    ImGuiIO& ScopedIo = ImGui::GetIO();
    ScopedIo.DisplaySize = ImVec2(1280.0f, 720.0f);
    ScopedIo.DeltaTime = 1.0f / 60.0f;
    ScopedIo.Fonts->AddFontDefault();
    ScopedIo.Fonts->Build();

    const std::array<std::string_view, 3> ScopedModes{"a", "b", "c"};
    Fby::Settings::Registry ScopedRegistry;
    ScopedRegistry.Enum("test.mode")
        .Category("Test")
        .Label("Mode")
        .Enum(ScopedModes)
        .Default(Fby::Settings::Value::MakeEnum("a"))
        .Scope(Fby::Settings::Scope::User);
    (void)ScopedRegistry.Freeze();
    Fby::Editor::SettingsRegistryPanel ScopedPanel;
    ScopedPanel.SetRegistry(&ScopedRegistry, Fby::Settings::Scope::User);
    bool ScopedOpen = true;
    GRegistry = &ScopedRegistry;

    ImGuiTestEngine* ScopedEngine = ImGuiTestEngine_CreateContext();
    ImGuiTestEngineIO& ScopedEngineIo = ImGuiTestEngine_GetIO(ScopedEngine);
    ScopedEngineIo.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
    ScopedEngineIo.ConfigLogToTTY = true;
    ImGuiTestEngine_Start(ScopedEngine, ImGui::GetCurrentContext());
    GImGui->TestEngineHookItems = true;
    RegisterTests(ScopedEngine);
    ImGuiTestEngine_QueueTests(ScopedEngine, ImGuiTestGroup_Tests, "Settings");

    while (!ImGuiTestEngine_IsTestQueueEmpty(ScopedEngine))
    {
        ImGui::NewFrame();
        ScopedPanel.Draw("Generated settings", &ScopedOpen);
        ImGui::Render();
        ImGuiTestEngine_PreSwap(ScopedEngine);
        ImGuiTestEngine_PostSwap(ScopedEngine);
    }

    int ScopedTested = 0;
    int ScopedSucceeded = 0;
    ImGuiTestEngine_GetResult(ScopedEngine, ScopedTested, ScopedSucceeded);
    ImGuiTestEngine_Stop(ScopedEngine);
    ImGui::DestroyContext();
    ImGuiTestEngine_DestroyContext(ScopedEngine);
    std::printf("settings_panel_engine: %d/%d\n", ScopedSucceeded, ScopedTested);
    return ScopedTested > 0 && ScopedSucceeded == ScopedTested ? 0 : 1;
}
