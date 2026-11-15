// FemboyEngine2D - tween progress, pause, dilation and loops.
#include "Engine/Animation/Tween.hpp"

#include <cstdio>

namespace
{
int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

void TestBasics()
{
    using namespace Fby::Animation;
    Tween ScopedTween;
    TweenDesc ScopedDesc;
    ScopedDesc.durationSeconds = 1.0f;
    ScopedDesc.to = 10.0f;
    Check(ScopedTween.Start(ScopedDesc), "tween starts");
    ScopedTween.Update(0.5f, false, 1.0f);
    Check(ScopedTween.Progress() > 0.49f && ScopedTween.Progress() < 0.51f, "half progress");
    Check(ScopedTween.Value() > 4.9f && ScopedTween.Value() < 5.1f, "half value");
    ScopedTween.Update(0.5f, false, 1.0f);
    Check(ScopedTween.Finished(), "tween finishes");
    Check(ScopedTween.Value() > 9.9f, "tween clamps to end");
}

void TestPauseAndDilation()
{
    using namespace Fby::Animation;
    Tween ScopedTween;
    TweenDesc ScopedDesc;
    ScopedDesc.durationSeconds = 1.0f;
    ScopedDesc.to = 10.0f;
    Check(ScopedTween.Start(ScopedDesc), "pause tween starts");
    ScopedTween.Update(0.25f, true, 1.0f);
    Check(ScopedTween.Progress() < 0.01f, "pause stops progress");
    ScopedTween.SetAffectByGamePause(false);
    ScopedTween.Update(0.25f, true, 1.0f);
    Check(ScopedTween.Progress() > 0.24f, "non-paused tween advances");
    ScopedTween.SetAffectByTimeDilation(false);
    const float ScopedBefore = ScopedTween.Progress();
    ScopedTween.Update(0.25f, false, 0.1f);
    Check(ScopedTween.Progress() > ScopedBefore + 0.2f, "raw time ignores dilation");
}

void TestLoopAndPingPong()
{
    using namespace Fby::Animation;
    Tween ScopedTween;
    TweenDesc ScopedDesc;
    ScopedDesc.durationSeconds = 1.0f;
    ScopedDesc.loop = true;
    ScopedDesc.pingPong = true;
    Check(ScopedTween.Start(ScopedDesc), "loop tween starts");
    ScopedTween.Update(1.5f, false, 1.0f);
    Check(ScopedTween.Progress() < 0.51f && ScopedTween.Progress() > 0.49f, "ping-pong reverse");
    Check(ScopedTween.Running(), "looping tween stays running");
    ScopedTween.Update(2.0f, false, 1.0f);
    Check(ScopedTween.Progress() < 0.51f, "ping-pong cycles");
}

void TestGroup()
{
    using namespace Fby::Animation;
    TweenGroup ScopedGroup;
    TweenDesc ScopedDesc;
    ScopedDesc.durationSeconds = 1.0f;
    Check(ScopedGroup.Start(ScopedDesc) != nullptr, "group starts tween");
    Check(ScopedGroup.RunningCount() == 1, "group counts running tween");
    ScopedGroup.Update(1.0f, false, 1.0f);
    Check(ScopedGroup.RunningCount() == 0, "group retires finished tween");
}
}

int main()
{
    TestBasics();
    TestPauseAndDilation();
    TestLoopAndPingPong();
    TestGroup();
    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d tween test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("tween tests: ok\n");
    return 0;
}
