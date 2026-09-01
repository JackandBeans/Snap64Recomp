/**
 * Replaces auSetSoundVolume from src/sys/audio.c -- the fourth writer of the
 * sound-effect volume slots, and the one the SOUND page's Effects slider
 * did not reach.
 *
 * A sound effect's volume is a slot in auSoundVolume[]. The audio thread
 * reads the slot once when it starts the voice, and the global-volume fades
 * (auSetSoundGlobalVolume, auSetCurrentSoundsGlobalVolume) re-read it; no
 * reader feeds the slot back into a writer. Four functions write it while
 * the game runs: auPlaySound, auPlaySoundWithParams and auPlaySoundWithVolume
 * fill it as a sound starts, and auSetSoundVolume rewrites it while the sound
 * plays. graphics_menu_patch.c scales the first three. Anything that changes
 * a sound's volume after it starts went through this one unscaled: every
 * positional sound, every tick, from EnvSound_Update, and every course's own
 * ambience ramps (the surf, the volcano, the river, the cave drips). With the
 * slider at ten percent those sounds still played at whatever this function
 * last stored, which for a positional sound is the full distance-attenuated
 * level -- the slider was heard only on one-shot effects.
 *
 * This is the stock body with the slot scaled as it is written, exactly as
 * the three play functions scale theirs: the slot always holds the scaled
 * value, the push to the voice is computed from the scaled slot with the
 * game's own formula, and each reader sees the scale exactly once. The
 * shutter's sub-slider applies here too, keyed on the id the slot was
 * started with (auPlayingSound), so a take-photo sound keeps its own level
 * if anything reshapes it mid-play.
 *
 * The two helpers are copies of graphics_menu_patch.c's, which are static
 * there; a shared non-replacement function would land in the patch section
 * and be refused by the recompiler. Keep the copies identical.
 */

#include "common.h"
#include "PR/libaudio.h"
#include "sys/audio.h"

/* audio.c keeps the sound player's per-voice state as file-local types; the
 * layout is mirrored here so the bank volume of the voice in a slot can be
 * read the way the original reads it (a 0x30-byte stride, the ALSound
 * pointer at +0x1C -- what the game's own code indexes with). */
typedef struct N_ALVoice_s {
    ALLink node;
    struct N_PVoice_s* pvoice;
    ALWaveTable* table;
    void* clientPrivate;
    s16 state;
    s16 priority;
    s16 fxBus;
    s16 unityPitch;
} N_ALVoice;

typedef struct {
    N_ALVoice voice;
    ALSound* sound;
    s16 priority;
    f32 pitch;
    s32 state;
    s16 vol;
    ALPan pan;
    u8 fxMix;
} N_ALSoundState;

extern s8* auSndpSoundId;
extern s32* auStartingSound;
extern u16* auSoundVolume;
extern u8 auGlobalSoundVolume;
extern ALSndPlayer* auSoundPlayer;

/* The SOUND bank of the settings mailbox, as graphics_menu_patch.c defines
 * it: the port's magic word at the base, percent volumes from +0x28. */
#define SNAP_GFX_MAILBOX 0x80C00000
#define MBOX_MAGIC   (*(volatile u32*) (SNAP_GFX_MAILBOX + 0x0))
#define SND_FIELD(i) (*(volatile u8*) (SNAP_GFX_MAILBOX + 0x28 + (i)))

/* Percent from the SOUND bank; full volume until the port has staged. */
static s32 snap_snd_pct(s32 i) {
    if (MBOX_MAGIC != 0x53474658) {
        return 100;
    }
    return SND_FIELD(i);
}

/* The shutter is its own slider on top of the effects slider: the two
 * take-photo sounds, and nothing else. */
static u16 snap_scaled_sfx(u32 soundID, s32 vol) {
    s32 pct = snap_snd_pct(2);
    if ((soundID == 0) || (soundID == 16)) {   /* SOUND_ID_TAKE_PHOTO(_2) */
        pct = (pct * snap_snd_pct(3)) / 100;
    }
    return (u16) ((vol * pct) / 100);
}

void auSetSoundVolume(s32 handle, u32 vol) {
    if (auSndpSoundId[handle] != -1) {
        OSIntMask mask = osSetIntMask(OS_IM_NONE);
        alSndpSetSound(auSoundPlayer, auSndpSoundId[handle]);
        if (alSndpGetState(auSoundPlayer) == AL_PLAYING || auStartingSound[handle] >= 0) {
            if (vol > 0x7FFF) {
                vol = 0x7FFF;
            }
            vol = snap_scaled_sfx(auPlayingSound[handle], vol);
            auSoundVolume[handle] = vol;
            vol = (vol * ((N_ALSoundState*) auSoundPlayer->sndState)[auSndpSoundId[handle]].sound->sampleVolume * auGlobalSoundVolume) >>
                  14;
            alSndpSetVol(auSoundPlayer, vol);
        }
        osSetIntMask(mask);
    }
}
