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
#include <math.h>
#include <SDL2/SDL.h>
#include "untergangtype.h"

/* Global Occlusion Arrays declared in header, instantiated here for compiler linking */
extern int *g_upper_clip;
extern int *g_lower_clip;

/* External global variables referenced from i_video.c and main.c */
extern uint32_t *g_pixel_buffer;
extern int       g_buffer_w;
extern int       g_buffer_h;
extern SDL_Color g_palette[256]; /* array sizing bounds descriptor */

/* =========================================================================
 * CORE 2.5D COLUMN RASTERIZATION ENGINE
 * ========================================================================= */

/*
 * render_seg
 * ----------
 * Transforms a single 2D world line segment into camera space, performs frustum 
 * clipping against the near Z plane, projects wall heights onto screen columns, 
 * and loops horizontally to cast texturing vectors for floors, walls, and ceilings.
 */
void render_seg(SDL_Renderer *ren, untergang_seg_t *seg, float player_rad, float focal_length, int screen_w, int screen_h) {
    (void)ren; /* Silence unused renderer warning as we draw directly to a software pixel array */

    /* Fetch structural geometry references from parent data blocks */
    untergang_linedef_t line = g_linedefs[seg->linedef];
    uint16_t front_side_idx = line.sidenum[0]; /* Index 0 designates Front Sidedef */
    if (front_side_idx == 0xFFFF) return;

    untergang_sidedef_t front_side = g_sidedefs[front_side_idx];
    untergang_sector_t front_sec   = g_sectors[front_side.sector];
    
    /* Identify portals. If a line has a valid back sidedef index, it acts as an open portal */
    uint16_t back_side_idx = line.sidenum[1]; /* Index 1 designates Back Sidedef */
    int is_two_sided = (line.flags & 0x0004) || (back_side_idx != 0xFFFF);  

    untergang_vertex_t v1 = g_vertices[seg->v1];
    untergang_vertex_t v2 = g_vertices[seg->v2];

    /* Translate world position vectors relative to player camera origin coordinates */
    float dx1 = (float)v1.x - g_player_x;
    float dy1 = (float)v1.y - g_player_y;
    float dx2 = (float)v2.x - g_player_x;
    float dy2 = (float)v2.y - g_player_y;

    float cos_a = cosf(player_rad);
    float sin_a = sinf(player_rad);

    /* Transform coordinates into 3D view camera space (X=Horizontal, Z=Depth Forward) */
    float rx1 = -dx1 * sin_a + dy1 * cos_a;
    float tz1 =  dx1 * cos_a + dy1 * sin_a;
    float rx2 = -dx2 * sin_a + dy2 * cos_a;
    float tz2 =  dx2 * cos_a + dy2 * sin_a;

    /* Frustum Clipping Boundary: If the wall segment sits completely behind the camera plane, discard it */
    float near_clip = 1.0f;
    if (tz1 < near_clip && tz2 < near_clip) return;

    float orig_rx1 = rx1, orig_tz1 = tz1;
    float orig_rx2 = rx2, orig_tz2 = tz2;

    /* Clip partial segment lines cutting through the near Z clip boundary to prevent divide-by-zero errors */
    if (tz1 < near_clip) {
        float t = (near_clip - tz1) / (tz2 - tz1);
        rx1 = rx1 + t * (rx2 - rx1);
        tz1 = near_clip;
    } else if (tz2 < near_clip) {
        float t = (near_clip - tz2) / (tz1 - tz2);
        rx2 = rx2 + t * (rx1 - rx2);
        tz2 = near_clip;
    }

    /* Run 3D perspective math projections to map X coordinates onto screen columns */
    int sx1 = (int)(screen_w / 2.0f - (rx1 * focal_length) / tz1);
    int sx2 = (int)(screen_w / 2.0f - (rx2 * focal_length) / tz2);

    /* Filter invalid geometry layouts or columns bleeding past visibility bounds thresholds */
    if (sx1 >= sx2 || sx2 < 0 || sx1 >= screen_w) return;

    int render_x1 = clamp_int(sx1, 0, screen_w - 1);
    int render_x2 = clamp_int(sx2, 0, screen_w - 1);

    float iz1 = 1.0f / tz1;
    float iz2 = 1.0f / tz2;
    float half_h = screen_h / 2.0f;

    /* Fetch asset pointers cached inside our u_archive storage blocks */
    uint8_t *ceil_flat  = get_flat_data("CEIL1_1");  
    uint8_t *floor_flat = get_flat_data("FLOOR0_1");

    untergang_wall_texture_t *mid_tex  = get_wall_texture("STARTAN3");
    untergang_wall_texture_t *top_tex  = get_wall_texture("STARTAN3");
    untergang_wall_texture_t *bot_tex  = get_wall_texture("STARTAN3");

    float seg_len = sqrtf((v2.x - v1.x) * (v2.x - v1.x) + (v2.y - v1.y) * (v2.y - v1.y));

    /* =====================================================================
     * HORIZONTAL COLUMN SWEEP LOOP
     * ===================================================================== */
    for (int x = render_x1; x <= render_x2; x++) {
        /* Read 1D occlusion values to skip drawing behind already established solid walls */
        int u_clip = g_upper_clip[x];
        int l_clip = g_lower_clip[x];
        if (u_clip >= l_clip) continue;

        /* Linear interpolation factor for calculating exact camera depth cross-sections */
        float factor = (sx2 == sx1) ? 0.0f : (float)(x - sx1) / (float)(sx2 - sx1);
        float iz = iz1 + factor * (iz2 - iz1);
        float tz = 1.0f / iz;

        /* Project vertical sector boundary height deltas directly onto viewport pixel rows */
        int y_ceil  = (int)(half_h - ((front_sec.ceiling_h - g_player_z) * focal_length) * iz);
        int y_floor = (int)(half_h - ((front_sec.floor_h   - g_player_z) * focal_length) * iz);

        /* Project an explicit directional viewing ray outwards for visplane row-casting */
        float col_angle = player_rad + atanf((x - screen_w / 2.0f) / focal_length);
        float cos_col = cosf(col_angle);
        float sin_col = sinf(col_angle);

        /* -----------------------------------------------------------------
         * 1. CEILING VISPLANE RASTERIZATION
         * ----------------------------------------------------------------- */
        int ceil_stop = clamp_int(y_ceil, u_clip, l_clip);
        float ceil_height_diff = (front_sec.ceiling_h - g_player_z) * focal_length;

        for (int y = u_clip; y < ceil_stop; y++) {
            float dy = half_h - (float)y;
            if (fabsf(dy) < 0.001f) continue;

            /* Raycast intersection tracking back onto flat world dimensions map grids */
            float row_dist = ceil_height_diff / dy;
            float wx = g_player_x + row_dist * cos_col;
            float wy = g_player_y + row_dist * sin_col;

            /* Mask coordinate bits through an unsigned bitwise AND to force looping on 64x64 grids */
            int fx = ((unsigned int)(int)floorf(wx)) & 63;
            int fy = ((unsigned int)(int)floorf(wy)) & 63;

            if (ceil_flat) {
                uint8_t pal_idx = ceil_flat[fy * 64 + fx];
                SDL_Color c = g_palette[pal_idx];
                float shadow = clamp_float(1.0f - (row_dist / 1500.0f), 0.1f, 1.0f); 

                g_pixel_buffer[y * screen_w + x] = (255 << 24) | 
                                                   ((uint8_t)(c.r * shadow) << 16) | 
                                                   ((uint8_t)(c.g * shadow) << 8)  | 
                                                   ((uint8_t)(c.b * shadow));
            }
        }

        /* -----------------------------------------------------------------
         * 2. FLOOR VISPLANE RASTERIZATION
         * ----------------------------------------------------------------- */
        int floor_start = clamp_int(y_floor, u_clip, l_clip);
        float floor_height_diff = (g_player_z - front_sec.floor_h) * focal_length;

        for (int y = floor_start; y < l_clip; y++) {
            float dy = (float)y - half_h;
            if (fabsf(dy) < 0.001f) continue;

            float row_dist = floor_height_diff / dy;
            float wx = g_player_x + row_dist * cos_col;
            float wy = g_player_y + row_dist * sin_col;

            int fx = ((unsigned int)(int)floorf(wx)) & 63;
            int fy = ((unsigned int)(int)floorf(wy)) & 63;

            if (floor_flat) {
                uint8_t pal_idx = floor_flat[fy * 64 + fx];
                SDL_Color c = g_palette[pal_idx];
                float shadow = clamp_float(1.0f - (row_dist / 1500.0f), 0.1f, 1.0f);

                g_pixel_buffer[y * screen_w + x] = (255 << 24) | 
                                                   ((uint8_t)(c.r * shadow) << 16) | 
                                                   ((uint8_t)(c.g * shadow) << 8)  | 
                                                   ((uint8_t)(c.b * shadow));
            }
        }

        /* Ambient depth shadow scalar for vertical wall panels */
        float shadow_val = clamp_float(1.0f - (tz / 2000.0f), 0.1f, 1.0f);

        /* Map exact horizontal texture column indices (U coordinates) along our line length bounds */
        float orig_sx1 = (screen_w / 2.0f - (orig_rx1 * focal_length) / orig_tz1);
        float orig_sx2 = (screen_w / 2.0f - (orig_rx2 * focal_length) / orig_tz2);
        float seg_u = seg->offset + front_side.x_offset + ((x - orig_sx1) / (orig_sx2 - orig_sx1)) * seg_len;

        /* -----------------------------------------------------------------
         * 3. WALL TEXTURE PANEL DRAWING
         * ----------------------------------------------------------------- */
        if (!is_two_sided) {
            /* SOLID WALL: Draws top-to-bottom and completely occludes anything behind it */
            int draw_top = clamp_int(y_ceil, u_clip, l_clip);
            int draw_bot = clamp_int(y_floor, u_clip, l_clip);

            if (mid_tex && mid_tex->pixels && (y_floor > y_ceil)) {
                int u_tex = ((unsigned int)(int)floorf(seg_u)) % mid_tex->width;

                for (int y = draw_top; y < draw_bot; y++) {
                    float v_scale = (float)mid_tex->height / (float)(y_floor - y_ceil);
                    int v_tex = ((int)((y - y_ceil) * v_scale) + front_side.y_offset) % mid_tex->height;
                    if (v_tex < 0) v_tex += mid_tex->height;

                    uint8_t pal_idx = mid_tex->pixels[v_tex * mid_tex->width + u_tex];
                    SDL_Color c = g_palette[pal_idx];

                    g_pixel_buffer[y * screen_w + x] = (255 << 24) |
                                                       ((uint8_t)(c.r * shadow_val) << 16) |
                                                       ((uint8_t)(c.g * shadow_val) << 8)  |
                                                       ((uint8_t)(c.b * shadow_val));
                }
            } else {
                /* Uniform colored canvas fallback if asset cache block reports missing items */
                uint32_t wall_color = (255 << 24) |
                                       ((uint8_t)(210 * shadow_val) << 16) |
                                       ((uint8_t)(120 * shadow_val) << 8)  |
                                       ((uint8_t)(50 * shadow_val));

                for (int y = draw_top; y < draw_bot; y++) {
                    g_pixel_buffer[y * screen_w + x] = wall_color;
                }
            }

            /* Occlude the tracking column array: Force upper clipping bounds to the bottom edge of viewport */
            g_upper_clip[x] = screen_h;
        } else {
            /* PORTAL WALL: Securely fetch adjoining side profile structures - BUG FIXED */
            if (back_side_idx == 0xFFFF) continue;
            untergang_sidedef_t back_side = g_sidedefs[back_side_idx];
            untergang_sector_t back_sec   = g_sectors[back_side.sector];

            int y_bceil  = (int)(half_h - ((back_sec.ceiling_h - g_player_z) * focal_length) * iz);
            int y_bfloor = (int)(half_h - ((back_sec.floor_h   - g_player_z) * focal_length) * iz);

            /* Upper Step: Render bleeding geometry if connecting room has a lower ceiling height */
            if (front_sec.ceiling_h > back_sec.ceiling_h) {
                int top_draw = clamp_int(y_ceil, u_clip, l_clip);
                int bot_draw = clamp_int(y_bceil, u_clip, l_clip);

                if (top_tex && top_tex->pixels && (y_bceil > y_ceil)) {
                    int u_tex = ((unsigned int)(int)floorf(seg_u)) % top_tex->width;

                    for (int y = top_draw; y < bot_draw; y++) {
                        float v_scale = (float)top_tex->height / (float)(y_bceil - y_ceil);
                        int v_tex = ((int)((y - y_ceil) * v_scale) + front_side.y_offset) % top_tex->height;
                        if (v_tex < 0) v_tex += top_tex->height;

                        uint8_t pal_idx = top_tex->pixels[v_tex * top_tex->width + u_tex];
                        SDL_Color c = g_palette[pal_idx];

                        g_pixel_buffer[y * screen_w + x] = (255 << 24) |
                                                           ((uint8_t)(c.r * shadow_val) << 16) |
                                                           ((uint8_t)(c.g * shadow_val) << 8)  |
                                                           ((uint8_t)(c.b * shadow_val));
                    }
                }

                /* Update the tracking array: push visibility ceiling down to upper step edge boundary */
                g_upper_clip[x] = clamp_int(bot_draw, u_clip, l_clip);
            }

            /* Lower Step: Render step panels if connecting room has a higher floor height */
            if (front_sec.floor_h < back_sec.floor_h) {
                int top_draw = clamp_int(y_bfloor, u_clip, l_clip);
                int bot_draw = clamp_int(y_floor, u_clip, l_clip);

                if (bot_tex && bot_tex->pixels && (y_floor > y_bfloor)) {
                    int u_tex = ((unsigned int)(int)floorf(seg_u)) % bot_tex->width;

                    for (int y = top_draw; y < bot_draw; y++) {
                        float v_scale = (float)bot_tex->height / (float)(y_floor - y_bfloor);
                        int v_tex = ((int)((y - y_bfloor) * v_scale) + front_side.y_offset) % bot_tex->height;
                        if (v_tex < 0) v_tex += bot_tex->height;

                        uint8_t pal_idx = bot_tex->pixels[v_tex * bot_tex->width + u_tex];
                        SDL_Color c = g_palette[pal_idx];

                        g_pixel_buffer[y * screen_w + x] = (255 << 24) |
                                                           ((uint8_t)(c.r * shadow_val) << 16) |
                                                           ((uint8_t)(c.g * shadow_val) << 8)  |
                                                           ((uint8_t)(c.b * shadow_val));
                    }
                }

                /* Update the tracking array: push visibility floor up to lower step edge boundary */
                g_lower_clip[x] = clamp_int(top_draw, u_clip, l_clip);
            }
        }
    }
}

/* =========================================================================
 * BSP RECURSIVE TRAVERSER ENTRY POINT
 * ========================================================================= */

/*
 * render_bsp_node
 * ---------------
 * Performs a recursive front-to-back traversal of the Binary Space Partitioning tree.
 * By evaluating the side of the split line the player camera stands on, it guarantees
 * that the closest polygons render first, maximizing our software column occlusion metrics.
 */
void render_bsp_node(SDL_Renderer *ren, uint16_t node_id, float player_rad, float focal_length, int screen_w, int screen_h) {
    /* Base Case: If higher bit is flagged, we have successfully run down onto a map leaf subsector */
    if (node_id & NF_SUBSECTOR) {
        uint16_t ssec_id = node_id & ~NF_SUBSECTOR;
        
        /* FIXED: Protect subsector tracking bounds safely against the node matrix boundaries */
        if (ssec_id >= (uint16_t)g_num_nodes * 2) return; 

        untergang_subsector_t ssec = g_ssectors[ssec_id];

        /* Render all line segments contained within this convex subsector hull */
        for (int i = 0; i < ssec.num_segs; i++) {
            uint32_t seg_idx = ssec.first_seg + i;
            
            /* Ensure segment sequence checking stays safely within linedef tracking limits */
            if (seg_idx < (uint32_t)g_num_linedefs * 4) {
                render_seg(ren, &g_segs[seg_idx], player_rad, focal_length, screen_w, screen_h);
            }
        }
        return;
    }

    if (node_id >= (uint16_t)g_num_nodes) return; /* FIXED tracking name boundary */
    untergang_node_t *node = &g_nodes[node_id];

    /* Calculate directional orientation offsets from splitting line node nodes */
    float dx = (float)node->dx;
    float dy = (float)node->dy;
    float px = g_player_x - (float)node->x;
    float py = g_player_y - (float)node->y;

    /* Binary cross-product check sorting camera position against node partition planes */
    int side = ((px * dy) - (py * dx) > 0.0f) ? 1 : 0;

    /* Front-to-back sorting rule: Render near sub-tree node elements first, then process far paths */
    render_bsp_node(ren, node->children[side], player_rad, focal_length, screen_w, screen_h);
    render_bsp_node(ren, node->children[side ^ 1], player_rad, focal_length, screen_w, screen_h);
}