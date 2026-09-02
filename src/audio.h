/**
 * @file audio.h
 * @brief Audio callbacks for Snap64 Recomp.
 *
 * Provides SDL2-based audio output callbacks that match the
 * ultramodern::audio_callbacks_t interface.
 */

#ifndef SNAP_AUDIO_H
#define SNAP_AUDIO_H

#include <cstdint>
#include <cstddef>

namespace snap {

/**
 * Queue audio samples for playback.
 *
 * @param samples  Pointer to interleaved stereo 16-bit PCM samples.
 * @param count    Number of samples (not bytes — each sample is one int16_t).
 */
void audio_queue_samples(int16_t* samples, size_t count);

/**
 * Get the number of audio frames remaining in the playback buffer.
 *
 * @return Number of samples still queued for playback.
 */
size_t audio_get_frames_remaining();

// Live queue depth in bytes, published to RDRAM as the game's AI_LEN value.
size_t audio_queued_bytes();

// The SOUND page's host-side controls: master volume (0..100), the window
// focus state fed by the SDL event pump, and whether losing focus mutes.
void set_master_volume(int percent);
void set_window_focused(bool focused);
void set_mute_unfocused(bool mute);

/**
 * Set the audio playback frequency.
 *
 * @param freq  Sample rate in Hz (e.g., 32000 for N64 audio).
 */
void audio_set_frequency(uint32_t freq);

} // namespace snap

#endif // SNAP_AUDIO_H
