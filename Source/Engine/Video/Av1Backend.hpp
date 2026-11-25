// FemboyEngine2D - AV1 decoder backend (dav1d or another host plugin).
#pragma once

#include "Engine/Video/Vp9Backend.hpp"

namespace Fby::Video
{
// AV1 uses the same function-table shape as VP9: create one instance per
// stream, decode one compressed frame into an I420 target, then destroy. The
// AV1 implementation is responsible for consuming the sequence header before
// frame data; a host may supply its own table through VideoPlayer.
using Av1Backend = Vp9Backend;

// The built-in dav1d table when FBY_HAS_DAV1D is defined (vendored on Windows,
// system pkg-config elsewhere); nullptr when the dependency was not built. Hosts
// may override it with their own Av1Backend.
[[nodiscard]] const Av1Backend* Libdav1dBackend() noexcept;
}
