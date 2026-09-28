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
#include <math.h>
#include <SDL2/SDL.h>
#include "untergangtype.h"

/* 
 * Engine State Tracking Variables
 * Managed locally by the video layer but read externally by the main loop.
 */
extern game_state_t g_state;
screen_melt_t g_melt;

/* 
 * Hardware-Accelerated Output Targets
 * These stream our raw software pixels onto a modern, hardware-accelerated GPU window context.
 */
SDL_Texture *g_title_texture = NULL;        /* Cached title screen texture */
SDL_Texture *g_framebuffer_texture = NULL;  /* Hardware texture target for streaming game frames */

/* 
 * Software Render Architecture Buffers
 * We draw column-by-column to an array of raw integers, then upload that block to the GPU.
 */
uint32_t *g_pixel_buffer = NULL;       /* Raw ARGB8888 32-bit canvas array */
int       g_buffer_w = 0;              /* Dynamic internal screen canvas columns */
int       g_buffer_h = 0;              /* Dynamic internal screen canvas rows */

/* 
 * Hardware Color Lookup Table
 * Populated directly from the archive loader to translate 8-bit palette indices to modern colors.
 */
SDL_Color g_palette[256];

/*
 * init_framebuffer
 * ----------------
 * Allocates and stretches our internal software drawing array whenever the game engine starts 
 * or the user changes their operating system window size.
 *
 * Parameters:
 *   ren - Pointer to our active, hardware-accelerated SDL Renderer context.
 *   w   - The new target width in pixels.
 *   h   - The new target height in pixels.
 */
void init_framebuffer(SDL_Renderer *ren, int w, int h) {
    /* If memory was previously allocated for another size, wipe it cleanly first */
    if (g_pixel_buffer) free(g_pixel_buffer);
    if (g_framebuffer_texture) SDL_DestroyTexture(g_framebuffer_texture);

    g_buffer_w = w;
    g_buffer_h = h;

    /* Allocate enough sequential memory blocks to store a full screen of 32-bit integers */
    g_pixel_buffer = (uint32_t*)malloc(sizeof(uint32_t) * w * h);
    
    /* Create a streaming hardware texture wrapper that accepts our raw pixel injections */
    g_framebuffer_texture = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, 
                                               SDL_TEXTUREACCESS_STREAMING, w, h);
}

/*
 * clear_pixel_buffer
 * ------------------
 * Resets the entire software color canvas block to a single uniform solid state color value 
 * (typically black) to clean up old stray pixels before a new 32-frame drawing sweep begins.
 */
void clear_pixel_buffer(uint32_t color) {
    int total = g_buffer_w * g_buffer_h;
    for (int i = 0; i < total; i++) {
        g_pixel_buffer[i] = color;
    }
}

/*
 * load_titlepic
 * -------------
 * Clean-room patch parser that extracts the binary title image lump from disk, maps its native 
 * 8-bit palette channels to standard RGBA, and uploads it cleanly as an accelerated hardware texture.
 *
 * Returns: A pointer to a compiled SDL_Texture if successful, or NULL if asset bounds check fails.
 */
SDL_Texture* load_titlepic(SDL_Renderer *ren, FILE *file, filelump_t *lumps, int num_lumps) {
    filelump_t *title_lump = NULL;
    
    /* Search the asset table array linearly for the exact title screen identifier match */
    for (int i = 0; i < num_lumps; i++) {
        if (strncmp(lumps[i].name, "TITLEPIC", 8) == 0) {
            title_lump = &lumps[i];
            break;
        }
    }
    if (!title_lump) return NULL;

    /* Allocate a staging frame array buffer to pull raw bytes directly out of disk storage */
    uint8_t *patch_data = (uint8_t*)malloc(title_lump->size);
    fseek(file, title_lump->filepos, SEEK_SET);
    if (fread(patch_data, 1, title_lump->size, file) != (size_t)title_lump->size) {
        free(patch_data);
        return NULL;
    }

    /* Standard map asset structure overlay tracking dimensions and screen draw shifts */
    untergang_patch_header_t *header = (untergang_patch_header_t*)patch_data;
    uint32_t *col_offsets = (uint32_t*)(patch_data + sizeof(untergang_patch_header_t));

    /* Create an intermediate software memory surface mapping exactly to our target boundaries */
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, header->width, header->height, 32, SDL_PIXELFORMAT_RGBA8888);
    uint32_t *pixels = (uint32_t*)surf->pixels;
    memset(pixels, 0, surf->pitch * header->height);

    /* Unpack column posts out of the raw stream to sidestep historical 2.5D column transparency blocks */
    for (int x = 0; x < header->width; x++) {
        uint8_t *col = patch_data + col_offsets[x];
        while (1) {
            uint8_t topdelta = col[0];
            if (topdelta == 0xFF) break; /* 0xFF marks the absolute termination of a data drawing column loop */

            uint8_t length = col[1];
            uint8_t *data = &col[3];

            for (int y = 0; y < length; y++) {
                int draw_y = topdelta + y;
                if (draw_y < header->height) {
                    uint8_t color_idx = data[y];
                    SDL_Color c = g_palette[color_idx]; /* Map the pixel through our system color chart */
                    
                    /* Pack separate channels cleanly into modern 32-bit ARGB architecture storage spots */
                    pixels[draw_y * header->width + x] = (c.r << 24) | (c.g << 16) | (c.b << 8) | 255;
                }
            }
            col += 4 + length; /* Advance pointers forward past headers and padding bounds blocks */
        }
    }

    /* Convert our software buffer directly into optimal accelerated GPU video formats */
    g_title_texture = SDL_CreateTextureFromSurface(ren, surf);
    SDL_FreeSurface(surf);
    free(patch_data);
    return g_title_texture;
}

/*
 * init_screen_melt
 * ----------------
 * Pre-calculates random vertical column drop arrays to construct our unique screen melt transition 
 * effect when the user breaks past the initial title sequence.
 */
void init_screen_melt(void) {
    g_melt.completed = 0;
    
    /* Seed the primary edge column baseline with a slight random upward offset push */
    g_melt.y_offsets[0] = -(rand() % 16);
    
    /* Loop across our canvas width to generate randomized jagged cascading column steps */
    for (int i = 1; i < MELT_WIDTH; i++) {
        int delta = (rand() % 5) - 2; /* Columns step slightly up or down relative to neighbors */
        g_melt.y_offsets[i] = g_melt.y_offsets[i - 1] + delta;
        
        /* Enforce absolute bounds limits so the melt stays contained cleanly to the screen */
        if (g_melt.y_offsets[i] > 0)   g_melt.y_offsets[i] = 0;
        if (g_melt.y_offsets[i] < -32) g_melt.y_offsets[i] = -32;
    }

    g_state = STATE_MELTING;
}

/*
 * render_screen_melt
 * ------------------
 * Animates the screen melt effect in real time by shifting column slices down the canvas.
 * It tracks column completion and shifts the game state seamlessly once all columns clear.
 */
void render_screen_melt(SDL_Renderer *ren, int screen_w, int screen_h) {
    int all_done = 1;
    float col_width = (float)screen_w / (float)MELT_WIDTH;
    int melt_speed = 20; /* Defines the rate of decay acceleration per frame sweep */

    for (int i = 0; i < MELT_WIDTH; i++) {
        /* If a column hasn't hit active sliding status yet, advance its trigger delay loop */
        if (g_melt.y_offsets[i] <= 0) {
            g_melt.y_offsets[i] += 2; 
            all_done = 0;
        } else {
            /* Accelerate gravity drops down the viewport */
            g_melt.y_offsets[i] += melt_speed;
            if (g_melt.y_offsets[i] < screen_h) {
                all_done = 0; /* Keep drawing because columns are still traveling down screen coordinates */
            }
        }

        int y_shift = g_melt.y_offsets[i];
        if (y_shift < 0) y_shift = 0;
        if (y_shift >= screen_h) continue;

        /* Calculate exact source and destination geometric rect boundaries for rendering blocks */
        SDL_Rect src_col = { (int)(i * (320.0f / MELT_WIDTH)), 0, 1, MELT_HEIGHT };
        SDL_Rect dst_col = { (int)(i * col_width), y_shift, (int)ceilf(col_width), screen_h };

        /* Splat the column slice image asset state directly down onto the primary rendering device */
        SDL_RenderCopy(ren, g_title_texture, &src_col, &dst_col);
    }

    /* Once all pixel columns exit off screen bounds limits, drop safely directly into interactive game blocks */
    if (all_done) {
        g_state = STATE_IN_GAME;
    }
}

/*
 * destroy_video_subsystem
 * -----------------------
 * Safe memory teardown de-allocator loop cleaning up references during full hardware exits.
 */
void destroy_video_subsystem(void) {
    if (g_pixel_buffer) { free(g_pixel_buffer); g_pixel_buffer = NULL; }
    if (g_framebuffer_texture) { SDL_DestroyTexture(g_framebuffer_texture); g_framebuffer_texture = NULL; }
    if (g_title_texture) { SDL_DestroyTexture(g_title_texture); g_title_texture = NULL; }
}