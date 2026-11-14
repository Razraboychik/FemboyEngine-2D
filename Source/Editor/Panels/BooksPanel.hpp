// FemboyEngine2D - editor reader for cooked .fbybook assets.
#pragma once

#include "Engine/Text/BookSession.hpp"

#include <array>
#include <string>
#include <vector>

namespace Fby::Ecs
{
struct SystemContext;
}

namespace Fby::Editor
{

class BooksPanel
{
public:
    void Draw(Ecs::SystemContext& InContext, bool* InOpen);

private:
    void Load();

    Text::BookSession MBook;
    Text::TextLayoutParams MLayout{};
    std::vector<std::byte> MBytes;
    std::array<char, 512> MPath{};
    std::string MStatus;
};

}
