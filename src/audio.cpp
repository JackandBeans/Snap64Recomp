/**
 * @file audio.cpp
 * @brief SDL2-based audio callback implementation for WaveRace64-Recomp.
 *
 * Uses SDL2's audio queue API for low-latency audio playback.
 * The N64 audio thread submits buffers via audio_queue_samples(),
 * and SDL2 drains them through its audio device callback.
 */

#include "audio.h"

#include <cstdio>
#include <cstring>
#include <atomic>
#include <mutex>
#include <SDL2/SDL.h>

namespace snap {

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

static SDL_AudioDeviceID audio_device = 0;
static uint32_t current_frequency = 32000;
static std::atomic<size_t> queued_samples{0};
// Frequency of the last FAILED open attempt, or 0 if none. Without this,
// audio_queue_samples() retried a full (and failing) WASAPI device open every
// few milliseconds when no audio endpoint is available, starving the audio
// thread and stalling game logic. A failed open is only retried when a
// different frequency is requested; the port otherwise runs as a silent sink
// (samples dropped, zero backlog reported).
static uint32_t failed_open_frequency = 0;

// ---------------------------------------------------------------------------
// SDL audio device management
// ---------------------------------------------------------------------------

static void ensure_audio_device(uint32_t freq) {
    if (audio_device != 0 && current_frequency == freq) {
        return; // Already open at the correct frequency.
    }
    if (audio_device == 0 && failed_open_frequency == freq) {
        return; // This frequency already failed to open; stay a silent sink.
    }

    // Close existing device if frequency changed.
    if (audio_device != 0) {
        SDL_CloseAudioDevice(audio_device);
        audio_device = 0;
    }

    SDL_AudioSpec desired{};
    desired.freq     = static_cast<int>(freq);
    desired.format   = AUDIO_S16SYS;
    desired.channels = 2;       // Stereo
    desired.samples  = 512;     // Buffer size in samples per channel
    desired.callback = nullptr; // Use SDL_QueueAudio instead of callback

    SDL_AudioSpec obtained{};
    audio_device = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (audio_device == 0) {
        fprintf(stderr, "[SNAP-Audio] SDL_OpenAudioDevice failed (audio disabled until next rate change): %s\n", SDL_GetError());
        failed_open_frequency = freq;
        // Adopt the requested rate even though the open failed. Leaving the
        // old rate here would make the next audio_queue_samples() call ask for
        // it again, miss the guard above, and reopen the device at a rate the
        // game is no longer producing -- the pitch bug this file documents.
        current_frequency = freq;
        return;
    }

    failed_open_frequency = 0;
    current_frequency = freq;
    queued_samples.store(0);

    // Unpause the device to start playback.
    SDL_PauseAudioDevice(audio_device, 0);

fprintf(stderr, "[SNAP-Audio] requested freq=%u -> obtained freq=%d channels=%d samples=%d format=0x%X\n",
            freq, obtained.freq, obtained.channels, obtained.samples, obtained.format);
    fflush(stderr);
}

// ---------------------------------------------------------------------------
// Public API (matches ultramodern::audio_callbacks_t)
// ---------------------------------------------------------------------------

void audio_queue_samples(int16_t* samples, size_t count) {
    ensure_audio_device(current_frequency);

    if (audio_device == 0 || samples == nullptr || count == 0) {
        return;
    }

    // count is the number of int16_t values (so byte count = count * 2).
    size_t byte_count = count * sizeof(int16_t);
    if (SDL_QueueAudio(audio_device, samples, static_cast<uint32_t>(byte_count)) != 0) {
        fprintf(stderr, "[SNAP-Audio] SDL_QueueAudio failed: %s\n", SDL_GetError());
        return;
    }

    queued_samples.fetch_add(count);
}

size_t audio_get_frames_remaining() {
    if (audio_device == 0) {
        return 0;
    }

    // SDL_GetQueuedAudioSize returns bytes. ultramodern multiplies this result
    // by 2 * sizeof(int16_t) to recover bytes, so it must be FRAMES (stereo
    // pairs) -- 4 bytes each. Returning int16 count here reported double the
    // real backlog.
    uint32_t queued_bytes = SDL_GetQueuedAudioSize(audio_device);
    return static_cast<size_t>(queued_bytes / (2 * sizeof(int16_t)));
}

size_t audio_queued_bytes() {
    if (audio_device == 0) {
        return 0;
    }
    // Stereo signed-16 => 4 bytes per frame, which is exactly the unit the N64's
    // AI_LEN register reports. The game shifts this right by 2 to get frames.
    return static_cast<size_t>(SDL_GetQueuedAudioSize(audio_device));
}

void audio_set_frequency(uint32_t freq) {
    if (freq == 0) {
        fprintf(stderr, "[SNAP-Audio] Ignoring zero frequency\n");
        return;
    }
    // NOTE: do NOT assign current_frequency here. ensure_audio_device() early-returns
    // when current_frequency already equals the requested rate, so assigning first
    // made it a no-op -- the device stayed at ultramodern's 48kHz startup placeholder
    // while the game produced 32kHz audio, playing everything 1.5x too fast.
    // ensure_audio_device() sets current_frequency itself once the device is open.
    ensure_audio_device(freq);
}

} // namespace snap




