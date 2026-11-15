// FemboyEngine2D - video stream inspector and playback controls.
#pragma once

#include "Engine/Video/VideoStream.hpp"

#include <array>
#include <string>

namespace Fby::Ecs
{
struct SystemContext;
}

namespace Fby::Editor
{

class VideoPanel
{
public:
    void Draw(Ecs::SystemContext& InContext, bool* InOpen);

private:
    void OpenStream(Ecs::SystemContext& InContext);
    void CloseStream();

    Fby::Video::VideoSystem MSystem;
    Fby::Ecs::Registry MRegistry;
    Fby::Entity MStreamEntity = Fby::kNullEntity;
    std::array<char, 512> MPath{};
    std::string MStatus;
    bool MInitialized = false;
};

}
