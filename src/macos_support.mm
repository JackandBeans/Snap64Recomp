/**
 * @file macos_support.mm
 * @brief What the macOS build needs of Objective-C: the Metal layer's size
 * left to the renderer.
 *
 * SDL's Cocoa Metal view (SDL_cocoametalview, created by SDL_Metal_CreateView
 * in main.cpp) sets its CAMetalLayer's drawableSize itself whenever the view
 * changes size. RT64's Metal swap chain (plume_metal.cpp,
 * MetalSwapChain::resize) sizes the same layer and records the size of the
 * pictures it draws, but only when the layer's size differs from the
 * window's: when SDL has already set it, plume never updates its record, and
 * every frame after a resize or a switch to or from fullscreen is drawn at
 * the old size into pictures of the new one. SDL's update is made to do
 * nothing, so the renderer alone sizes the layer.
 *
 * The method replacement is Zelda64Recomp's (src/main/support_apple.mm, by
 * David Chavez, GPL-3.0 as this port is), which ships with the same RT64
 * Metal backend.
 */

#import <Foundation/Foundation.h>
#import <objc/message.h>
#import <objc/runtime.h>

static void snap_keep_sdl_off_the_metal_layer(void) {
    Class cls = objc_getClass("SDL_cocoametalview");
    if (cls == nil) {
        // Another SDL, or the class is not linked in: nothing to replace.
        return;
    }
    SEL original = sel_registerName("updateDrawableSize");
    Method method = class_getInstanceMethod(cls, original);
    if (method == NULL) {
        return;
    }
    IMP nothing = imp_implementationWithBlock(^void(id self) {
        (void) self;
    });
    method_setImplementation(method, nothing);
}

// Runs as the executable loads, before main() and before SDL makes a view.
__attribute__((constructor)) static void snap_macos_support_init(void) {
    snap_keep_sdl_off_the_metal_layer();
}
