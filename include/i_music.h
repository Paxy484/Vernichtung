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

#ifndef I_MUSIC_H
#define I_MUSIC_H

#include <stdint.h>
#include <stddef.h>

/**
 * init_music_subsystem
 * --------------------
 * Runtime probe that dynamically maps fluid_synth symbols into memory via dlopen/LoadLibrary.
 * Initializes the synthesis core and loads the user-specified SoundFont patch passed via parameters.
 *
 * Parameters:
 *   sf_path - File path to a valid .sf2 or .sf3 SoundFont bank (passed via -soundfont).
 *
 * Returns: 1 if FluidSynth loaded successfully and is ready, 0 for graceful silent fallback.
 */
int init_music_subsystem(const char *sf_path);

/**
 * play_music_lump
 * ----------------
 * Automatically checks raw lump data format headers via mus2mid conversion gates, 
 * performs on-the-fly limit-removed translation if needed, and streams the sequenced 
 * output directly to FluidSynth.
 *
 * Parameters:
 *   lump_data - Pointer to raw data array extracted from the active IWAD/PWAD file.
 *   lump_size - Absolute size byte length of the target data buffer.
 */
void play_music_lump(const uint8_t *lump_data, size_t lump_size);

/**
 * stop_music
 * ----------
 * Immediately halts active MIDI sequencing threads and resets synth channel states.
 */
void stop_music(void);

/**
 * destroy_music_subsystem
 * -----------------------
 * Tears down hardware device bindings, unloads structures, and closes the dynamic link handle.
 */
void destroy_music_subsystem(void);

#endif /* I_MUSIC_H */