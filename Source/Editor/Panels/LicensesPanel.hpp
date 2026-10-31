// FemboyEngine2D - in-editor third-party license viewer.
#pragma once

#include <cstddef>

namespace Fby::Editor
{

// Read-only browser over the generated Fby::Notices table. It carries no state
// that affects the engine, so it never allocates on the editor frame path.
class LicensesPanel
{
public:
    LicensesPanel() = default;

    // Draws the window. Passing a null `InOpen` keeps the window always open.
    void Draw(bool* InOpen);

protected:
    std::size_t MSelected = 0;
};
}
