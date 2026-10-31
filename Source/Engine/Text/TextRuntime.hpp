// FemboyEngine2D - host-owned strict text runtime.
#pragma once

#include "Engine/Text/TextService.hpp"
#include "Engine/Text/TextShapeCache.hpp"
#include "Engine/Text/TextShaper.hpp"

#include <memory>

namespace Fby::Text
{

// Owns the two large text stores once per host: the 1 MiB TextService store and
// the fixed-capacity TextShapeCache. Both are heap-owned because their arrays
// are far too large for an app frame stack. The shaper is borrowed: a host may
// point it at a TextFontStack face or at a CodepointShaper fallback.
class TextRuntime
{
public:
    TextRuntime()
        : MService(std::make_unique<TextService>()), MShapes(std::make_unique<TextShapeCache>())
    {
    }
    ~TextRuntime() = default;
    TextRuntime(const TextRuntime&) = delete;
    TextRuntime& operator=(const TextRuntime&) = delete;

    [[nodiscard]] TextService& Service() noexcept
    {
        return *MService;
    }
    [[nodiscard]] const TextService& Service() const noexcept
    {
        return *MService;
    }
    [[nodiscard]] TextShapeCache& Shapes() noexcept
    {
        return *MShapes;
    }
    [[nodiscard]] const TextShapeCache& Shapes() const noexcept
    {
        return *MShapes;
    }
    void SetShaper(ITextShaper* InShaper) noexcept
    {
        MShaper = InShaper;
    }
    [[nodiscard]] ITextShaper* Shaper() const noexcept
    {
        return MShaper;
    }

private:
    std::unique_ptr<TextService> MService;
    std::unique_ptr<TextShapeCache> MShapes;
    ITextShaper* MShaper = nullptr;
};
}
