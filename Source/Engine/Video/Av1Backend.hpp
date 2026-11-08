// FemboyEngine2D - AV1 decoder backend (dav1d or another host plugin).
#pragma once

#include "Engine/Video/Vp9Backend.hpp"

namespace Fby::Video
{
// The AV1 table has the same decoder contract as VP9: create one instance per
// stream, decode one compressed frame into an I420 target, then destroy. The
// engine does not vendor dav1d; a host supplies the table through VideoPlayer.
using Av1Backend = Vp9Backend;

// The vendored dav1d table on Windows; nullptr when the optional dependency was
// not built (other platforms can supply their own Av1Backend).
[[nodiscard]] const Av1Backend* Libdav1dBackend() noexcept;
}
