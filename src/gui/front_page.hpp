#pragma once

#include "framebuffer.hpp"
#include "scopes.hpp"
#include "core/mod_loader.hpp"
#include "core/voice_pool.hpp"

namespace paulascape {

class FrontPage {
public:
    FrontPage();

    void draw(Framebuffer& fb, const Module& mod, PlaybackMode mainMode, PlaybackMode subMode, Scopes& scopes);
};

} // namespace paulascape
