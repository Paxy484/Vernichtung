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
#include <strings.h>
#include "untergangtype.h"

/* =========================================================================
 * ASSET CACHING STORAGE AND SHARED GLOBAL LINKING
 * ========================================================================= */

/* Allocation caches managed locally inside the archive system */
static untergang_flat_t *g_flats = NULL;
static int g_num_flats = 0;

static untergang_wall_texture_t *g_wall_textures = NULL;
static int g_num_wall_textures = 0;

/* External global variables referenced from main.c */
extern SDL_Color g_palette[256];

/* =========================================================================
 * ARCHIVE PARSING ENGINE IMPLEMENTATION
 * ========================================================================= */

/*
 * load_playpal
 * ------------
 * Reads the global color mapping lump (PLAYPAL) to populate the system RGBA 
 * hardware color table. This table translates 8-bit color indexes into modern colors.
 */
int load_playpal(FILE *file, filelump_t *lumps, int num_lumps) {
    /* Linearly scan the WAD directory to find the primary game color palette */
    for (int i = 0; i < num_lumps; i++) {
        if (strncmp(lumps[i].name, "PLAYPAL", 7) == 0) {
            /* Seek straight to the raw binary RGB spectrum bytes on disk */
            fseek(file, lumps[i].filepos, SEEK_SET);
            uint8_t raw_rgb[768]; /* 256 colors * 3 bytes (R, G, B) */
            if (fread(raw_rgb, 1, 768, file) != 768) return 0;

            /* Unpack the raw 8-bit channels into hardware-compatible SDL structures */
            for (int p = 0; p < 256; p++) {
                g_palette[p].r = raw_rgb[p * 3 + 0];
                g_palette[p].g = raw_rgb[p * 3 + 1];
                g_palette[p].b = raw_rgb[p * 3 + 2];
                g_palette[p].a = 255; /* Enforce full alpha opacity */
            }
            return 1;
        }
    }
    return 0;
}

/*
 * load_flats
 * ----------
 * Loads all raw 64x64 flat textures (floors/ceilings) trapped between design 
 * memory stream boundary markers (F_START/F_END or FF_START/FF_END).
 */
int load_flats(FILE *file, filelump_t *lumps, int num_lumps) {
    int f_start = -1, f_end = -1;
    
    /* Locate the beginning and ending structural tracking nodes for flats */
    for (int i = 0; i < num_lumps; i++) {
        if (strncmp(lumps[i].name, "F_START", 7) == 0 || strncmp(lumps[i].name, "FF_START", 8) == 0) {
            f_start = i;
        } else if (strncmp(lumps[i].name, "F_END", 5) == 0 || strncmp(lumps[i].name, "FF_END", 6) == 0) {
            f_end = i;
            break;
        }
    }

    if (f_start == -1 || f_end == -1) return 0;

    /* Count exactly how many lumps meet the valid 4096-byte (64x64) buffer requirement */
    for (int i = f_start + 1; i < f_end; i++) {
        if (lumps[i].size == 4096) g_num_flats++;
    }

    /* Allocate static heap arrays matching the discovered total texture counts */
    g_flats = (untergang_flat_t*)malloc(sizeof(untergang_flat_t) * g_num_flats);
    int idx = 0;

    /* Pull the raw pixel data directly out of the WAD into the static cache memory */
    for (int i = f_start + 1; i < f_end; i++) {
        if (lumps[i].size == 4096) {
            memset(g_flats[idx].name, 0, 9);
            strncpy(g_flats[idx].name, lumps[i].name, 8);
            
            fseek(file, lumps[i].filepos, SEEK_SET);
            if (fread(g_flats[idx].pixels, 1, 4096, file) != 4096) {
                // Keep moving if a single texture block read hits a limit stub
            }
            idx++;
        }
    }
    printf("[VERNICHTUNG] Loaded %d raw flat square textures!\n", g_num_flats);
    return 1;
}

/*
 * get_flat_data
 * -------------
 * System query hook that fetches flat floor/ceiling pixel blocks by their string names.
 * Falls back to the index 0 texture if an asset match fails to prevent crashes.
 */
uint8_t* get_flat_data(const char *name) {
    if (!g_flats || g_num_flats == 0) return NULL;
    for (int i = 0; i < g_num_flats; i++) {
        if (strncasecmp(g_flats[i].name, name, 8) == 0) {
            return g_flats[i].pixels;
        }
    }
    return g_flats[0].pixels; /* Secure fallback engine mechanism */
}

/*
 * load_wall_textures
 * ------------------
 * Resolves multiple compound patch structures out of the composite map tables 
 * (PNAMES + TEXTURE1/TEXTURE2) to assemble fully expanded linear wall textures into memory.
 */
int load_wall_textures(FILE *file, filelump_t *lumps, int num_lumps) {
    int pnames_idx = -1;
    
    /* 1. Extract the primary master directory index mapping patch string names */
    for (int i = 0; i < num_lumps; i++) {
        if (strncmp(lumps[i].name, "PNAMES", 6) == 0) {
            pnames_idx = i;
            break;
        }
    }
    if (pnames_idx == -1) return 0;

    fseek(file, lumps[pnames_idx].filepos, SEEK_SET);
    int32_t num_pnames = 0;
    if (fread(&num_pnames, 4, 1, file) != 1) return 0;

    /* Pull the full array of raw patch name blocks into a dynamic staging buffer */
    char (*pnames)[8] = malloc(num_pnames * 8);
    if (fread(pnames, 8, num_pnames, file) != (size_t)num_pnames) {
        free(pnames);
        return 0;
    }

    /* 2. Run two distinct compilation loops to sweep through TEXTURE1 and TEXTURE2 lumps */
    for (int lump_pass = 0; lump_pass < 2; lump_pass++) {
        const char *tname = (lump_pass == 0) ? "TEXTURE1" : "TEXTURE2";
        int tex_lump_idx = -1;
        for (int i = 0; i < num_lumps; i++) {
            if (strncmp(lumps[i].name, tname, 8) == 0) {
                tex_lump_idx = i;
                break;
            }
        }
        if (tex_lump_idx == -1) continue;

        /* Extract the full raw mapping block table into RAM for quick layout processing */
        uint8_t *tdata = malloc(lumps[tex_lump_idx].size);
        fseek(file, lumps[tex_lump_idx].filepos, SEEK_SET);
        if (fread(tdata, 1, lumps[tex_lump_idx].size, file) != (size_t)lumps[tex_lump_idx].size) {
            free(tdata);
            continue;
        }

        /* Read the texture tracking dimensions and relative address offset jumps */
        int32_t num_textures = *(int32_t*)tdata;
        int32_t *offsets = (int32_t*)(tdata + 4);

        int base_idx = g_num_wall_textures;
        g_num_wall_textures += num_textures;
        g_wall_textures = realloc(g_wall_textures, sizeof(untergang_wall_texture_t) * g_num_wall_textures);

        /* 3. Loop across every composite texture blueprint entry to generate linear arrays */
        for (int i = 0; i < num_textures; i++) {
            uint8_t *entry = tdata + offsets[i];
            untergang_wall_texture_t *wt = &g_wall_textures[base_idx + i];
            memset(wt->name, 0, 9);
            strncpy(wt->name, (char*)entry, 8);

            /* Read dimensions and total individual sub-patches used by this texture layout */
            wt->width  = *(uint16_t*)(entry + 12);
            wt->height = *(uint16_t*)(entry + 14);
            uint16_t num_patches = *(uint16_t*)(entry + 20);

            /* Allocate a clean linear canvas block to flatten out the stacked patches */
            wt->pixels = malloc(wt->width * wt->height);
            memset(wt->pixels, 0, wt->width * wt->height);

            uint8_t *patch_structs = entry + 22;
            
            /* 4. Traverse every internal patch to slice and stamp its data into the canvas */
            for (int p = 0; p < num_patches; p++) {
                int16_t origin_x = *(int16_t*)(patch_structs + p * 10 + 0);
                int16_t origin_y = *(int16_t*)(patch_structs + p * 10 + 2);
                uint16_t pnum   = *(uint16_t*)(patch_structs + p * 10 + 4);

                if (pnum >= (uint16_t)num_pnames) continue; /* Defensive boundary check */

                /* Correlate the patch name string from PNAMES to its actual data lump index */
                int p_lump_idx = -1;
                for (int k = 0; k < num_lumps; k++) {
                    if (strncasecmp(lumps[k].name, pnames[pnum], 8) == 0) {
                        p_lump_idx = k;
                        break;
                    }
                }
                if (p_lump_idx == -1) continue;

                /* Pull the individual picture patch into an isolated buffer array */
                uint8_t *patch_data = malloc(lumps[p_lump_idx].size);
                fseek(file, lumps[p_lump_idx].filepos, SEEK_SET);
                if (fread(patch_data, 1, lumps[p_lump_idx].size, file) != (size_t)lumps[p_lump_idx].size) {
                    free(patch_data);
                    continue;
                }

                untergang_patch_header_t *ph = (untergang_patch_header_t*)patch_data;
                uint32_t *col_offsets = (uint32_t*)(patch_data + sizeof(untergang_patch_header_t));

                /* 5. Secure vertical column parsing loop extracting pixel posts */
                for (int px = 0; px < ph->width; px++) {
                    int dest_x = origin_x + px;
                    if (dest_x < 0 || dest_x >= wt->width) continue; /* Trim overflowing columns */

                    uint32_t offset = col_offsets[px];
                    if (offset >= (uint32_t)lumps[p_lump_idx].size) continue;

                    uint8_t *col = patch_data + offset;
                    while (1) {
                        if ((col - patch_data) + 2 > lumps[p_lump_idx].size) break; 
                        
                        uint8_t topdelta = col[0];
                        if (topdelta == 0xFF) break; /* 0xFF marks the termination of a vertical column script */

                        uint8_t length = col[1];
                        if ((col - patch_data) + 4 + length > lumps[p_lump_idx].size) break;
                        
                        uint8_t *data = &col[3];

                        /* Draw the post data bytes straight into our flattened texture mapping matrix */
                        for (int py = 0; py < length; py++) {
                            int dest_y = origin_y + topdelta + py;
                            if (dest_y >= 0 && dest_y < wt->height) {
                                wt->pixels[dest_y * wt->width + dest_x] = data[py];
                            }
                        }
                        col += 4 + length; /* Shift pointer forward past metadata and post pixels */
                    }
                }
                free(patch_data);
            }
        }
        free(tdata);
    }
    free(pnames);
    printf("[VERNICHTUNG] Loaded %d compound composite wall textures!\n", g_num_wall_textures);
    return 1;
}

/* System query returning compiled texture layouts to the drawing routines */
untergang_wall_texture_t* get_wall_texture(const char *name) {
    if (!g_wall_textures || g_num_wall_textures == 0) return NULL;
    for (int i = 0; i < g_num_wall_textures; i++) {
        if (strncasecmp(g_wall_textures[i].name, name, 8) == 0) {
            return &g_wall_textures[i];
        }
    }
    return NULL;
}

/* Frees local caching structures during engine de-allocation sequences */
void free_archive_cache(void) {
    if (g_flats) {
        free(g_flats);
        g_flats = NULL;
    }
    if (g_wall_textures) {
        for (int i = 0; i < g_num_wall_textures; i++) {
            if (g_wall_textures[i].pixels) free(g_wall_textures[i].pixels);
        }
        free(g_wall_textures);
        g_wall_textures = NULL;
    }
    g_num_flats = 0;
    g_num_wall_textures = 0;
}