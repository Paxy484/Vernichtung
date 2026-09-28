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

#ifndef UNTERGANGTYPE_H
#define UNTERGANGTYPE_H

#include <stdint.h>
#include <stdio.h>
#include <SDL2/SDL.h>
#include "untergangdef.h"

#pragma pack(push, 1)

typedef struct {
    char identification[4];
    int32_t numlumps;
    int32_t infotableofs;
} wadheader_t;

typedef struct {
    int32_t filepos;
    int32_t size;
    char name[8];
} filelump_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    int16_t  left_offset;
    int16_t  top_offset;
} untergang_patch_header_t;

typedef struct {
    int16_t x, y, angle, type, flags;
} untergang_thing_t;

typedef struct { int16_t x, y; } untergang_vertex_t;

typedef struct {
    uint16_t v1, v2;
    int16_t angle;
    uint16_t linedef;
    int16_t side, offset;
} untergang_seg_t;

typedef struct {
    int16_t floor_h, ceiling_h;
    char floor_tex[8], ceil_tex[8];
    int16_t light_level, type, tag;
} untergang_sector_t;

typedef struct {
    int16_t x_offset, y_offset;
    char top_tex[8], bottom_tex[8], mid_tex[8];
    uint16_t sector;
} untergang_sidedef_t;

typedef struct {
    uint16_t v1;
    uint16_t v2;
    uint16_t flags;
    uint16_t special;
    uint16_t tag;
    uint16_t sidenum[2];  /* [0] = Front Sidedef, [1] = Back Sidedef */
} untergang_linedef_t;

typedef struct { uint16_t num_segs, first_seg; } untergang_subsector_t;

typedef struct {
    int16_t x, y, dx, dy, bbox[2][4];
    uint16_t children[2];
} untergang_node_t;

#pragma pack(pop)

typedef struct {
    char name[9];
    uint8_t pixels[64 * 64];
} untergang_flat_t;

typedef struct {
    char name[9];
    uint16_t width, height;
    uint8_t *pixels;
} untergang_wall_texture_t;

typedef struct {
    int y_offsets[MELT_WIDTH];
    int completed;
} screen_melt_t;

/* Global Engine Cross-References */
extern untergang_vertex_t    *g_vertices;
extern untergang_seg_t       *g_segs;
extern untergang_sector_t    *g_sectors;
extern untergang_sidedef_t   *g_sidedefs;
extern untergang_linedef_t   *g_linedefs;
extern untergang_subsector_t *g_ssectors;
extern untergang_node_t      *g_nodes;

extern int g_num_nodes, g_root_node, g_num_linedefs;
extern float g_player_x, g_player_y, g_player_z, g_player_target_z, g_player_angle;
extern float g_player_momx, g_player_momy, g_player_momz;
extern int   g_is_grounded;
extern float g_bob_phase;

extern int *g_upper_clip, *g_lower_clip;

/* Function utility hooks mapped to i_input.c */
float deg_to_rad(float deg);
float normalize_angle(float a);
int clamp_int(int val, int min, int max);
float clamp_float(float val, float min, float max);

/* Asset loader hooks mapped to u_archive.c */
untergang_wall_texture_t* get_wall_texture(const char *name);
uint8_t* get_flat_data(const char *name);

#endif