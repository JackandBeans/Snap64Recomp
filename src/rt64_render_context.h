/**
 * @file rt64_render_context.h
 * @brief RT64 RendererContext factory for Snap64 Recomp.
 *
 * Provides a factory function that creates an ultramodern::renderer::RendererContext
 * subclass backed by the RT64 rendering library.
 */

#ifndef SNAP_RT64_RENDER_CONTEXT_H
#define SNAP_RT64_RENDER_CONTEXT_H

#include <memory>
#include <cstdint>
#include "ultramodern/renderer_context.hpp"

namespace snap {

/**
 * Factory function matching ultramodern::renderer::callbacks_t::create_render_context_t.
 *
 * Creates and returns a RendererContext backed by RT64::Application.
 *
 * @param rdram          Pointer to the emulated RDRAM buffer.
 * @param window_handle  Platform window handle (SDL_Window* on Linux).
 * @param developer_mode If true, enable RT64 developer/debug features.
 * @return A unique_ptr to the created RendererContext, or nullptr on failure.
 */
std::unique_ptr<ultramodern::renderer::RendererContext> create_render_context(
    uint8_t* rdram,
    ultramodern::renderer::WindowHandle window_handle,
    bool developer_mode
);

} // namespace snap

#endif // SNAP_RT64_RENDER_CONTEXT_H
