#ifndef __THREADS_HPP__
#define __THREADS_HPP__

#include <functional>
#include <string>
#include <thread>

#include "ultra64.h"

namespace ultramodern {
    namespace threads {
        struct callbacks_t {
            using get_game_thread_name_t = std::string(const OSThread* t);

            /**
             * Allows to specifying a custom name for each thread. Mainly for debugging purposes.
             *
             * For maximum cross-platform compatibility the returned name should be at most 15 bytes long (16 bytes including the null terminator).
             *
             * If this function is not provided then the thread id will be used as the name of the thread.
             */
            get_game_thread_name_t *get_game_thread_name;
        };

        void set_callbacks(const callbacks_t& callbacks);

        std::string get_game_thread_name(const OSThread* t);

        // Snap64 Recomp: the host threads that run the game's recompiled
        // code get the stack that code is known to need, 8 MB. Windows gives
        // every thread the image's /STACK (the port links 8 MB) and Linux
        // 8 MB, but macOS gives every thread but the main one 512 KB, a
        // sixteenth of what the game has ever run on. On macOS the function
        // runs on a pthread with the full stack (the joinable form's
        // std::thread only waits for it); elsewhere it is a plain
        // std::thread, as before.
        constexpr size_t game_host_stack_bytes = 8u * 1024u * 1024u;
        std::thread make_game_host_thread(std::function<void()> func);
        void start_detached_game_host_thread(std::function<void()> func);
    }
}

#endif
