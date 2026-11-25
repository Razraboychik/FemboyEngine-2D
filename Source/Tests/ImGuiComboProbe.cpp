// FemboyEngine2D - minimal ImGui Test Engine combo probe.
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_te_context.h"
#include "imgui_te_engine.h"

#include <cstdio>

namespace
{
int GValue = 0;

void RegisterTests(ImGuiTestEngine* InEngine)
{
    ImGuiTest* ScopedTest = IM_REGISTER_TEST(InEngine, "Probe", "Combo");
    ScopedTest->TestFunc = [](ImGuiTestContext* InContext)
    {
        InContext->SetRef("ComboProbe");
        InContext->ComboClick("Mode/b");
        IM_CHECK(GValue == 1);
    };
}
}

int main()
{
    ImGui::CreateContext();
    ImGuiIO& ScopedIo = ImGui::GetIO();
    ScopedIo.DisplaySize = ImVec2(800.0f, 600.0f);
    ScopedIo.DeltaTime = 1.0f / 60.0f;
    ScopedIo.Fonts->AddFontDefault();
    ScopedIo.Fonts->Build();

    ImGuiTestEngine* ScopedEngine = ImGuiTestEngine_CreateContext();
    ImGuiTestEngineIO& ScopedEngineIo = ImGuiTestEngine_GetIO(ScopedEngine);
    ScopedEngineIo.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
    ScopedEngineIo.ConfigLogToTTY = true;
    ImGuiTestEngine_Start(ScopedEngine, ImGui::GetCurrentContext());
    GImGui->TestEngineHookItems = true;
    RegisterTests(ScopedEngine);
    ImGuiTestEngine_QueueTests(ScopedEngine, ImGuiTestGroup_Tests, "Probe");

    const char* ScopedItems[] = {"a", "b", "c"};
    while (!ImGuiTestEngine_IsTestQueueEmpty(ScopedEngine))
    {
        ImGui::NewFrame();
        ImGui::Begin("ComboProbe");
        ImGui::Combo("Mode", &GValue, ScopedItems, 3);
        ImGui::End();
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
    std::printf("imgui_combo_probe: %d/%d value=%d\n", ScopedSucceeded, ScopedTested, GValue);
    return ScopedTested > 0 && ScopedSucceeded == ScopedTested ? 0 : 1;
}
