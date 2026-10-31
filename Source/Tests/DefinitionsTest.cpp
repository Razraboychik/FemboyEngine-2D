// FemboyEngine2D - Definitions.hpp contract test.
#include "Engine/Core/Definitions.hpp"
#include "Engine/ECS/Components.hpp"

#include <cstdio>

namespace Fby::Test
{

class SampleBase : public TypeObject
{
public:
    [[nodiscard]] TypeId GetTypeId() const noexcept override
    {
        return StableTypeId<SampleBase>();
    }

    int Payload = 7;
};

class SampleDerived : public SampleBase
{
public:
    [[nodiscard]] TypeId GetTypeId() const noexcept override
    {
        return StableTypeId<SampleDerived>();
    }
};

class SampleOther : public TypeObject
{
public:
    [[nodiscard]] TypeId GetTypeId() const noexcept override
    {
        return StableTypeId<SampleOther>();
    }
};
}

FBY_REGISTER_TYPE(Fby::Test::SampleDerived, Fby::Test::SampleBase)

int main()
{
    int ScopedFailures = 0;
    const auto ScopedCheck = [&ScopedFailures](bool InCondition, const char* InName)
    {
        if (!InCondition)
        {
            std::printf("FAIL: %s\n", InName);
            ++ScopedFailures;
        }
    };

    Fby::Test::SampleDerived ScopedDerived;
    Fby::Test::SampleBase ScopedBase;
    Fby::Test::SampleBase* ScopedAsBase = &ScopedDerived;

    ScopedCheck(Fby::Cast<Fby::Test::SampleBase>(ScopedAsBase) == ScopedAsBase, "upcast is free");
    ScopedCheck(Fby::Cast<Fby::Test::SampleDerived>(ScopedAsBase) == &ScopedDerived, "derived downcast succeeds");
    ScopedCheck(Fby::Cast<Fby::Test::SampleDerived>(&ScopedBase) == nullptr, "base downcast is rejected");
    ScopedCheck(Fby::Cast<Fby::Test::SampleBase>(static_cast<Fby::TypeObject*>(ScopedAsBase)) == ScopedAsBase,
                "cast through TypeObject succeeds");

    Fby::Test::SampleOther ScopedOther;
    ScopedCheck(Fby::Cast<Fby::Test::SampleDerived>(static_cast<Fby::TypeObject*>(&ScopedOther)) == nullptr,
                "unrelated object is rejected");

    ScopedCheck(Fby::FemCast<Fby::Test::SampleBase*>(&ScopedDerived) == &ScopedDerived, "FemCast is a static upcast");
    ScopedCheck(Fby::StableTypeId<Fby::Test::SampleDerived>() != Fby::StableTypeId<Fby::Test::SampleBase>(),
                "distinct classes have distinct ids");

    static_assert(Fby::StableTypeId<Fby::Transform2D>() == Fby::TypeDetail::Fnv1a("Transform2D"),
                  "Mirror name drives the component id");
    static_assert(Fby::StableTypeId<int>() != Fby::kInvalidTypeId, "unreflected types still have ids");

    if (ScopedFailures == 0)
    {
        std::printf("definitions_test: all tests passed\n");
    }
    return ScopedFailures == 0 ? 0 : 1;
}
