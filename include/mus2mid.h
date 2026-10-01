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

#ifndef MUS2MID_H
#define MUS2MID_H

#include <stdint.h>
#include <stddef.h>

#pragma pack(push, 1)

/* 
 * Public Specification Binary Header for DMX MUS Format
 * Securely packed to guarantee 1:1 binary alignment across compilers.
 */
typedef struct {
    char     magic[4];     /* Must match "MUS\x1A" */
    uint16_t score_len;    /* Length of the score music data data bytes block */
    uint16_t score_start;  /* Absolute file offset tracking structural track starts */
    uint16_t channels;     /* Primary sound reproduction channel matrix numbers */
    uint16_t sec_channels; /* Secondary tracking column channel indicators */
    uint16_t instr_cnt;    /* Count tracker bounding active patches mapping list data arrays */
    uint16_t dummy;        /* Architectural alignment spacing padding word placeholder */
} mus_header_t;

#pragma pack(pop)

/**
 * translate_mus_to_midi
 * --------------------
 * Inspects an incoming lump data stream. If it is already a standard MIDI file,
 * it returns a clean duplicate block. If it is a compressed legacy MUS stream,
 * it unpacks it on-the-fly into a clean standard Type 0 MIDI buffer.
 *
 * Parameters:
 *   mus_data     - Pointer to the raw lump memory block pulled from the WAD.
 *   mus_size     - Total size of the input lump data buffer.
 *   out_mid_size - Out pointer returning the precise byte length of the resulting MIDI block.
 *
 * Returns: Dynamically allocated heap memory containing ready-to-stream MIDI data, 
 *          or NULL if a hardened boundary check detects corruption.
 */
uint8_t* translate_mus_to_midi(const uint8_t *mus_data, size_t mus_size, size_t *out_mid_size);

#endif /* MUS2MID_H */