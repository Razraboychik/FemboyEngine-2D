// FemboyEngine2D - video stream inspector and playback controls.
#include "Editor/Panels/VideoPanel.hpp"

#include "Editor/Infrastructure/EditorTypography.hpp"

#include "Engine/ECS/SystemContext.hpp"

#include <imgui.h>

namespace Fby::Editor
{

void VideoPanel::Draw(Ecs::SystemContext& InContext, bool* InOpen)
{
    if (InOpen != nullptr && !*InOpen)
    {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(560.0f, 380.0f), ImGuiCond_FirstUseEver);
    if (!BeginEditorPanel("Video", InOpen))
    {
        EndEditorPanel();
        return;
    }
    if (!MInitialized)
    {
        std::string ScopedError;
        MInitialized = MSystem.Initialize(InContext.renderer, InContext.assets, nullptr, &ScopedError);
        if (!MInitialized)
        {
            MStatus = "video system unavailable: " + ScopedError;
        }
    }
    ImGui::TextDisabled("Streams are decoded into the RHI texture array; this panel exposes metadata and controls.");
    ImGui::SetNextItemWidth(360.0f);
    (void)ImGui::InputText("WebM path", MPath.data(), MPath.size());
    ImGui::SameLine();
    if (ImGui::Button("Open"))
    {
        OpenStream(InContext);
    }
    ImGui::SameLine();
    if (ImGui::Button("Close"))
    {
        CloseStream();
    }
    if (!MStatus.empty())
    {
        ImGui::TextWrapped("%s", MStatus.c_str());
    }

    if (MInitialized && MStreamEntity != Fby::kNullEntity)
    {
        MSystem.Update(MRegistry, InContext.dt);
        const Fby::Video::VideoStreamComponent* ScopedStream =
            MRegistry.try_get<Fby::Video::VideoStreamComponent>(MStreamEntity);
        if (ScopedStream != nullptr && ScopedStream->Player != Fby::Video::kInvalidVideoPlayer)
        {
            const Fby::Video::VideoPlayer* ScopedPlayer = MSystem.Player(ScopedStream->Player);
            if (ScopedPlayer != nullptr)
            {
                const Fby::Video::WebmTrackInfo& ScopedTrack = ScopedPlayer->Track();
                const double ScopedFps = ScopedTrack.defaultDurationNs != 0
                                             ? 1.0e9 / static_cast<double>(ScopedTrack.defaultDurationNs)
                                             : 0.0;
                ImGui::Separator();
                ImGui::Text("size %ux%u   fps %.2f   audio %s", ScopedTrack.width, ScopedTrack.height, ScopedFps,
                            ScopedPlayer->HasAudio() ? "yes" : "no");
                ImGui::Text("texture %u   layer %u", ScopedPlayer->TextureId(), ScopedPlayer->CurrentLayer());
                bool ScopedPaused = ScopedStream->Paused;
                if (ImGui::Checkbox("Paused", &ScopedPaused))
                {
                    MRegistry.get<Fby::Video::VideoStreamComponent>(MStreamEntity).Paused = ScopedPaused;
                }
                float ScopedSpeed = ScopedStream->Speed;
                if (ImGui::SliderFloat("Speed", &ScopedSpeed, 0.25f, 2.0f, "%.2f"))
                {
                    MRegistry.get<Fby::Video::VideoStreamComponent>(MStreamEntity).Speed = ScopedSpeed;
                }
                ImGui::Text("player %u   stream %u", ScopedStream->Player, ScopedStream->StreamId);
            }
        }
    }
    EndEditorPanel();
}

void VideoPanel::OpenStream(Ecs::SystemContext& InContext)
{
    CloseStream();
    MStatus.clear();
    if (!MInitialized)
    {
        MStatus = "video system is not initialized";
        return;
    }
    if (MPath[0] == '\0')
    {
        MStatus = "Enter a .webm path first.";
        return;
    }
    (void)InContext;
    const Fby::Entity ScopedEntity = MRegistry.create();
    Fby::Video::VideoStreamComponent ScopedStream;
    std::string ScopedError;
    if (!MSystem.Open(ScopedStream, MPath.data(), &ScopedError))
    {
        MRegistry.destroy(ScopedEntity);
        MStatus = "open failed: " + ScopedError;
        return;
    }
    MRegistry.emplace<Fby::Video::VideoStreamComponent>(ScopedEntity, ScopedStream);
    MStreamEntity = ScopedEntity;
    MStatus = "opened " + std::string(MPath.data());
}

void VideoPanel::CloseStream()
{
    if (MStreamEntity != Fby::kNullEntity)
    {
        if (auto* ScopedStream = MRegistry.try_get<Fby::Video::VideoStreamComponent>(MStreamEntity);
            ScopedStream != nullptr)
        {
            (void)MSystem.Close(*ScopedStream);
        }
        MRegistry.destroy(MStreamEntity);
        MStreamEntity = Fby::kNullEntity;
    }
}

}
