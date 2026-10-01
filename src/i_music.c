/*
 * Copyright 2026 Rafael Peixoto
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, 
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, 
 *    this list of conditions and the following disclaimer in the documentation 
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its 
 *    contributors may be used to endorse or promote products derived from 
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “AS IS” 
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE 
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include "i_music.h"
#include "mus2mid.h"

/* =========================================================================
 * UNIFIED CROSS-PLATFORM RUNTIME DYNAMIC LINKING ABSTRACTION
 * ========================================================================= */
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
    #include <windows.h>
    #define dlopen(libname, flags) (void*)LoadLibraryA(libname)
    #define dlsym(handle, symbol)  (void*)GetProcAddress((HMODULE)handle, symbol)
    #define dlclose(handle)        FreeLibrary((HMODULE)handle)
    
    static const char* g_fluidsynth_libs[] = { 
        "fluidsynth.dll", 
        "libfluidsynth-3.dll", 
        "libfluidsynth-2.dll" 
    };
#else
    #include <dlfcn.h>
    static const char* g_fluidsynth_libs[] = { 
        "libfluidsynth.so.3", 
        "libfluidsynth.so.2", 
        "libfluidsynth.so" 
    };
#endif

/* Opaque Handle definitions matching public FluidSynth API configurations */
typedef struct _fluid_settings_t fluid_settings_t;
typedef struct _fluid_synth_t    fluid_synth_t;
typedef struct _fluid_player_t   fluid_player_t;

/* Dynamic Linking Function Pointer Block */
static void*            g_fluid_handle   = NULL;
static SDL_AudioDeviceID g_audio_device   = 0;

static fluid_settings_t* (*fn_new_fluid_settings)(void) = NULL;
static fluid_synth_t*    (*fn_new_fluid_synth)(fluid_settings_t*) = NULL;
static fluid_player_t*   (*fn_new_fluid_player)(fluid_synth_t*) = NULL;
static int               (*fn_fluid_synth_sfload)(fluid_synth_t*, const char*, int) = NULL;
static int               (*fn_fluid_player_add_mem)(fluid_player_t*, const void*, size_t) = NULL;
static int               (*fn_fluid_player_play)(fluid_player_t*) = NULL;
static int               (*fn_fluid_player_stop)(fluid_player_t*) = NULL;
static int               (*fn_fluid_synth_write_s16)(fluid_synth_t*, int, void*, int, int, void*, int, int) = NULL;
static void              (*fn_delete_fluid_player)(fluid_player_t*) = NULL;
static void              (*fn_delete_fluid_synth)(fluid_synth_t*) = NULL;
static void              (*fn_delete_fluid_settings)(fluid_settings_t*) = NULL;

/* Sub-system Core Memory Trackers */
static fluid_settings_t *g_settings        = NULL;
static fluid_synth_t    *g_synth           = NULL;
static fluid_player_t   *g_player          = NULL;
static uint8_t          *g_active_midi_buf = NULL;

/* Real-Time Audio Mixer Processing Thread Callback */
static void fluid_audio_callback(void *userdata, uint8_t *stream, int len) {
    (void)userdata;
    if (!g_synth || !fn_fluid_synth_write_s16) {
        memset(stream, 0, len);
        return;
    }
    /* Render 16-bit signed stereo frames out of FluidSynth's internal synthesis pipe */
    int sample_frames = len / 4; /* 2 channels * 2 bytes per sample = 4 bytes per frame */
    fn_fluid_synth_write_s16(g_synth, sample_frames, stream, 0, 2, stream, 1, 2);
}

int init_music_subsystem(const char *sf_path) {
    /* Multi-filename runtime scanner targeting host system dynamic frameworks */
    size_t lib_count = sizeof(g_fluidsynth_libs) / sizeof(g_fluidsynth_libs[0]);
    for (size_t i = 0; i < lib_count; i++) {
        g_fluid_handle = dlopen(g_fluidsynth_libs[i], RTLD_LAZY);
        if (g_fluid_handle) break;
    }

    if (!g_fluid_handle) {
        printf("[VERNICHTUNG] FluidSynth library not found. Operating music in SILENT mode.\n");
        return 0;
    }

    /* Map binary interface functions cleanly */
    fn_new_fluid_settings    = (fluid_settings_t* (*)(void))dlsym(g_fluid_handle, "new_fluid_settings");
    fn_new_fluid_synth       = (fluid_synth_t* (*)(fluid_settings_t*))dlsym(g_fluid_handle, "new_fluid_synth");
    fn_new_fluid_player      = (fluid_player_t* (*)(fluid_synth_t*))dlsym(g_fluid_handle, "new_fluid_player");
    fn_fluid_synth_sfload    = (int (*)(fluid_synth_t*, const char*, int))dlsym(g_fluid_handle, "fluid_synth_sfload");
    fn_fluid_player_add_mem  = (int (*)(fluid_player_t*, const void*, size_t))dlsym(g_fluid_handle, "fluid_player_add_mem");
    fn_fluid_player_play     = (int (*)(fluid_player_t*))dlsym(g_fluid_handle, "fluid_player_play");
    fn_fluid_player_stop     = (int (*)(fluid_player_t*))dlsym(g_fluid_handle, "fluid_player_stop");
    fn_fluid_synth_write_s16 = (int (*)(fluid_synth_t*, int, void*, int, int, void*, int, int))dlsym(g_fluid_handle, "fluid_synth_write_s16");
    fn_delete_fluid_player   = (void (*)(fluid_player_t*))dlsym(g_fluid_handle, "delete_fluid_player");
    fn_delete_fluid_synth    = (void (*)(fluid_synth_t*))dlsym(g_fluid_handle, "delete_fluid_synth");
    fn_delete_fluid_settings = (void (*)(fluid_settings_t*))dlsym(g_fluid_handle, "delete_fluid_settings");

    if (!fn_new_fluid_settings || !fn_new_fluid_synth || !fn_new_fluid_player || 
        !fn_fluid_synth_sfload || !fn_fluid_synth_write_s16 || !fn_delete_fluid_player ||
        !fn_delete_fluid_synth || !fn_delete_fluid_settings) {
        printf("[ERROR] Function symbol extraction failed inside the loaded library module.\n");
        dlclose(g_fluid_handle);
        g_fluid_handle = NULL;
        return 0;
    }

    /* Instantiate synthesis structures */
    g_settings = fn_new_fluid_settings();
    g_synth    = fn_new_fluid_synth(g_settings);

    /* =========================================================================
     * EXPLICIT VARIABLE COMMAND-LINE SOUNDFONT VALIDATION GATE
     * ========================================================================= */
    int sf_id = -1;
    if (sf_path) {
        sf_id = fn_fluid_synth_sfload(g_synth, sf_path, 1);
        if (sf_id != -1) {
            printf("[VERNICHTUNG] Loaded custom SoundFont asset: %s\n", sf_path);
        } else {
            printf("[VERNICHTUNG] [WARNING] Failed to load requested SoundFont path: %s\n", sf_path);
        }
    } else {
        printf("[VERNICHTUNG] No SoundFont passed via -soundfont. Operating audio in SILENT mode.\n");
    }

    /* Gracefully discard engine handles and cascade backward if loading fails */
    if (sf_id == -1) {
        fn_delete_fluid_synth(g_synth); g_synth = NULL;
        fn_delete_fluid_settings(g_settings); g_settings = NULL;
        dlclose(g_fluid_handle); g_fluid_handle = NULL;
        return 0;
    }

    /* Request standard low-latency hardware stream constraints from SDL2 */
    SDL_AudioSpec desired, obtained;
    SDL_zero(desired);
    desired.freq     = 44100;
    desired.format   = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples  = 1024;
    desired.callback = fluid_audio_callback;

    g_audio_device = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0);
    if (g_audio_device == 0) {
        printf("[ERROR] Failed to establish a functional SDL2 hardware mixer pipeline device thread.\n");
        
        /* Do NOT call destroy_music_subsystem() or dlclose() here! 
         * By simply returning 0, we keep the FluidSynth library handle warm.
         * This allows GNU OpenMP's background worker threads to sleep safely 
         * without referencing an unmapped memory graveyard, completely 
         * preventing the SIGSEGV core dump! */
        return 0; 
    }

    /* Fire up hardware audio processing channels */
    SDL_PauseAudioDevice(g_audio_device, 0);
    return 1;
}

void play_music_lump(const uint8_t *lump_data, size_t lump_size) {
    if (!g_fluid_handle || !g_synth || !g_audio_device) return;

    stop_music();

    /* Automated safety data format translation routing pass (Bypasses MIDI, Unpacks MUS) */
    size_t midi_size = 0;
    g_active_midi_buf = translate_mus_to_midi(lump_data, lump_size, &midi_size);
    if (!g_active_midi_buf) return;

    /* Protect structural background sequencer additions from racing audio hardware threads */
    SDL_LockAudioDevice(g_audio_device);

    g_player = fn_new_fluid_player(g_synth);
    if (g_player) {
        fn_fluid_player_add_mem(g_player, g_active_midi_buf, midi_size);
        fn_fluid_player_play(g_player);
    }

    SDL_UnlockAudioDevice(g_audio_device);
}

void stop_music(void) {
    if (!g_fluid_handle || !g_audio_device) return;

    SDL_LockAudioDevice(g_audio_device);

    if (g_player) {
        fn_fluid_player_stop(g_player);
        fn_delete_fluid_player(g_player);
        g_player = NULL;
    }

    if (g_active_midi_buf) {
        free(g_active_midi_buf);
        g_active_midi_buf = NULL;
    }

    SDL_UnlockAudioDevice(g_audio_device);
}

void destroy_music_subsystem(void) {
    stop_music();

    if (g_audio_device) {
        SDL_CloseAudioDevice(g_audio_device);
        g_audio_device = 0;
    }

    if (g_fluid_handle) {
        if (g_synth)    fn_delete_fluid_synth(g_synth);
        if (g_settings) fn_delete_fluid_settings(g_settings);
        dlclose(g_fluid_handle);
        g_synth = NULL;
        g_settings = NULL;
        g_fluid_handle = NULL;
    }
}