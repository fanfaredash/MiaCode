#pragma once

namespace miacode::quick_shell {

inline bool shouldUseSeparatePreviewSurface(bool quickShellFrontend, bool hasVideoMedia)
{
    return quickShellFrontend && hasVideoMedia;
}

}  // namespace miacode::quick_shell
