/*
 * Copyright 2026 Rafael Peixoto
 *
 * Licensed under the Pax Public License, Version 5.0 (the "License");
 * you may not use this file except in compliance with the License.
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: LicenseRef-Pax-5.0
 *
 * You may obtain a copy of the full License text within the Source or Object 
 * (or Binary) form materials provided with this distribution (typically in a 
 * "LICENSE" file, or "LICENSE-PAX" if there is more than one file named "LICENSE").
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <time.h>
#include <SDL2/SDL.h>

// I'm sorry

/* =========================================================================
 * ENGINE CONSTANTS & PHYSICS TWEAKS
 * ========================================================================= */
#define PI 3.14159f
#define FOV_BASE 90.0f             /* Default Field of View in degrees */
#define EYE_HEIGHT 41.0f           /* Player camera height above sector floor */
#define MAX_STEP_HEIGHT 24.0f      /* Maximum vertical step player can walk up */
#define GAME_FPS 35                /* Classic framerate target */
#define FRAME_TIME_MS (1000 / GAME_FPS)
#define PLAYER_RADIUS 16.0f        /* Bounding cylinder radius for line collision */
#define MOUSE_SENSITIVITY 0.15f

#define UNTERGANG_FRICTION 0.90625f/* Ground friction decay per frame */
#define UNTERGANG_ACCEL    1.8f    /* Acceleration rate per movement frame */
#define GRAVITY            1.2f    /* Gravity applied per frame when airborne */

#define NF_SUBSECTOR 0x8000        /* Node flag bitmask indicating a leaf subsector */
#define MELT_WIDTH   640           /* Resolution columns for screen melt transition */
#define MELT_HEIGHT  400           /* Resolution rows for screen melt transition */

/* State machine for game flow */
typedef enum {
    STATE_TITLE_SCREEN,
    STATE_MELTING,
    STATE_IN_GAME
} game_state_t;

/* =========================================================================
 * WAD & BSP DATA STRUCTURES (Packed to match binary disk layout)
 * ========================================================================= */
#pragma pack(push, 1)

/* Primary WAD file header */
typedef struct {
    char identification[4];        /* "IWAD" or "PWAD" */
    int32_t numlumps;              /* Total number of lump directory entries */
    int32_t infotableofs;          /* File offset to directory table */
} wadheader_t;

/* Directory entry pointing to a lump data block */
typedef struct {
    int32_t filepos;               /* Offset in bytes from start of file */
    int32_t size;                  /* Size of the lump in bytes */
    char name[8];                  /* Lump ASCII name (padded with nulls if < 8) */
} filelump_t;

/* Header format for picture patches inside WADs */
typedef struct {
    uint16_t width;
    uint16_t height;
    int16_t  left_offset;          /* Origin alignment offset X */
    int16_t  top_offset;           /* Origin alignment offset Y */
} untergang_patch_header_t;

/* Thing entity definition (Player spawn, monsters, pickups, lights) */
typedef struct {
    int16_t x, y;                  /* World position coordinates */
    int16_t angle;                 /* Facing direction in degrees */
    int16_t type;                  /* Thing type ID */
    int16_t flags;                 /* Spawn flags (e.g., Skill levels, MP) */
} untergang_thing_t;

/* World 2D map vertex */
typedef struct {
    int16_t x, y;
} untergang_vertex_t;

/* Line segment created by BSP compiler splitting Linedefs */
typedef struct {
    uint16_t v1, v2;               /* Start and end vertex indices */
    int16_t angle;                 /* Binary Angle Measurement (BAM) */
    uint16_t linedef;              /* Parent Linedef index */
    int16_t side;                  /* Sidedef side: 0 = front, 1 = back */
    int16_t offset;                /* Texture horizontal offset along segment */
} untergang_seg_t;

/* Sector definition (2.5D ceiling/floor boundaries) */
typedef struct {
    int16_t floor_h, ceiling_h;    /* Vertical heights */
    char floor_tex[8], ceil_tex[8];/* Flat texture lump names */
    int16_t light_level, type, tag;
} untergang_sector_t;

/* Sidedef binding a Linedef face to a Sector with wall textures */
typedef struct {
    int16_t x_offset, y_offset;
    char top_tex[8], bottom_tex[8], mid_tex[8];
    uint16_t sector;               /* Sector index this side faces */
} untergang_sidedef_t;

/* Linedef connecting two vertices and partitioning space */
typedef struct {
    uint16_t v1, v2;               /* Vertex indices */
    uint16_t flags, special, tag;
    uint16_t sidenum[2];           /* Sidedef indices [0]=front, [1]=back (0xFFFF = none) */
} untergang_linedef_t;

/* BSP Leaf Node representing a convex subsector polygon */
typedef struct {
    uint16_t num_segs;             /* Number of Segs contained */
    uint16_t first_seg;            /* Index of first Seg in array */
} untergang_subsector_t;

/* BSP Node structure defining a partitioning line and bounding boxes */
typedef struct {
    int16_t x, y;                  /* Partition line origin */
    int16_t dx, dy;                /* Partition line vector */
    int16_t bbox[2][4];            /* Bounding boxes for child nodes [right/left][top,bot,left,right] */
    uint16_t children[2];          /* Subsector or child node indices */
} untergang_node_t;

#pragma pack(pop)

/* =========================================================================
 * RUNTIME TEXTURE & SCREEN MELT BUFFERS
 * ========================================================================= */

/* Raw 64x64 flat floor/ceiling texture */
typedef struct {
    char name[9];
    uint8_t pixels[64 * 64];       /* Palette indexed pixels */
} untergang_flat_t;

/* Composite multi-patch wall texture synthesized at load time */
typedef struct {
    char name[9];
    uint16_t width;
    uint16_t height;
    uint8_t *pixels;               /* Column-major palette indexed pixels */
} untergang_wall_texture_t;

/* Screen melt transition effect state */
typedef struct {
    int y_offsets[MELT_WIDTH];     /* Per-column vertical drop offset */
    int completed;
} screen_melt_t;

/* Global Asset Caches */
static untergang_flat_t *g_flats = NULL;
static int g_num_flats = 0;

static untergang_wall_texture_t *g_wall_textures = NULL;
static int g_num_wall_textures = 0;

/* Global Loaded Level Data */
untergang_vertex_t    *g_vertices   = NULL;
untergang_seg_t       *g_segs       = NULL;
untergang_sector_t    *g_sectors    = NULL;
untergang_sidedef_t   *g_sidedefs   = NULL;
untergang_linedef_t   *g_linedefs   = NULL;
untergang_subsector_t *g_ssectors   = NULL;
untergang_node_t      *g_nodes      = NULL;

int g_num_nodes    = 0;
int g_root_node    = 0;
int g_num_linedefs = 0;

/* Global Player State */
float g_player_x = 0.0f;
float g_player_y = 0.0f;
float g_player_z = EYE_HEIGHT;
float g_player_target_z = EYE_HEIGHT;
float g_player_angle = 0.0f;

float g_player_momx = 0.0f;
float g_player_momy = 0.0f;
float g_player_momz = 0.0f;
int   g_is_grounded = 1;
float g_bob_phase   = 0.0f;

/* Engine Rendering & Display State */
static game_state_t g_state = STATE_TITLE_SCREEN;
static SDL_Color    g_palette[256]; /* 256-color palette from PLAYPAL */
static SDL_Texture *g_title_texture = NULL;
static screen_melt_t g_melt;

static uint32_t    *g_pixel_buffer = NULL;       /* Software ARGB8888 frame buffer */
static SDL_Texture *g_framebuffer_texture = NULL;/* Hardware texture target for streaming */
static int g_buffer_w = 0;
static int g_buffer_h = 0;

/* Occlusion clipping arrays for 1D occlusion tracking per screen column */
int *g_upper_clip = NULL;
int *g_lower_clip = NULL;

/* =========================================================================
 * MATH & UTILITY FUNCTIONS
 * ========================================================================= */

float deg_to_rad(float deg) { return deg * (PI / 180.0f); }

/* Wraps angle into [0.0, 360.0) range */
float normalize_angle(float a) {
    while (a < 0.0f) a += 360.0f;
    while (a >= 360.0f) a -= 360.0f;
    return a;
}

int clamp_int(int val, int min, int max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

float clamp_float(float val, float min, float max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

/* Reallocates software pixel buffer when window is resized */
void init_framebuffer(SDL_Renderer *ren, int w, int h) {
    if (g_pixel_buffer) free(g_pixel_buffer);
    if (g_framebuffer_texture) SDL_DestroyTexture(g_framebuffer_texture);

    g_buffer_w = w;
    g_buffer_h = h;
    g_pixel_buffer = (uint32_t*)malloc(sizeof(uint32_t) * w * h);
    g_framebuffer_texture = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, 
                                               SDL_TEXTUREACCESS_STREAMING, w, h);
}

void clear_pixel_buffer(uint32_t color) {
    int total = g_buffer_w * g_buffer_h;
    for (int i = 0; i < total; i++) {
        g_pixel_buffer[i] = color;
    }
}

/* =========================================================================
 * WAD RESOURCE LOADERS
 * ========================================================================= */

/* Reads PLAYPAL lump to build RGBA lookup table for color index mapping */
int load_playpal(FILE *wad, filelump_t *lumps, int num_lumps) {
    for (int i = 0; i < num_lumps; i++) {
        if (strncmp(lumps[i].name, "PLAYPAL", 7) == 0) {
            fseek(wad, lumps[i].filepos, SEEK_SET);
            uint8_t raw_rgb[768];
            if (fread(raw_rgb, 1, 768, wad) != 768) return 0;

            for (int p = 0; p < 256; p++) {
                g_palette[p].r = raw_rgb[p * 3 + 0];
                g_palette[p].g = raw_rgb[p * 3 + 1];
                g_palette[p].b = raw_rgb[p * 3 + 2];
                g_palette[p].a = 255;
            }
            return 1;
        }
    }
    return 0;
}

/* Loads all raw 64x64 flat textures located between F_START and F_END markers */
int load_flats(FILE *wad, filelump_t *lumps, int num_lumps) {
    int f_start = -1, f_end = -1;
    for (int i = 0; i < num_lumps; i++) {
        if (strncmp(lumps[i].name, "F_START", 7) == 0 || strncmp(lumps[i].name, "FF_START", 8) == 0) {
            f_start = i;
        } else if (strncmp(lumps[i].name, "F_END", 5) == 0 || strncmp(lumps[i].name, "FF_END", 6) == 0) {
            f_end = i;
            break;
        }
    }

    if (f_start == -1 || f_end == -1) return 0;

    for (int i = f_start + 1; i < f_end; i++) {
        if (lumps[i].size == 4096) g_num_flats++;
    }

    g_flats = (untergang_flat_t*)malloc(sizeof(untergang_flat_t) * g_num_flats);
    int idx = 0;

    for (int i = f_start + 1; i < f_end; i++) {
        if (lumps[i].size == 4096) {
            memset(g_flats[idx].name, 0, 9);
            strncpy(g_flats[idx].name, lumps[i].name, 8);
            fseek(wad, lumps[i].filepos, SEEK_SET);
            fread(g_flats[idx].pixels, 1, 4096, wad);
            idx++;
        }
    }
    printf("[VERNICHTUNG] Loaded %d flat textures!\n", g_num_flats);
    return 1;
}

/* Returns memory address of raw flat texture pixels by name */
uint8_t* get_flat_data(const char *name) {
    if (!g_flats || g_num_flats == 0) return NULL;
    for (int i = 0; i < g_num_flats; i++) {
        if (strncasecmp(g_flats[i].name, name, 8) == 0) {
            return g_flats[i].pixels;
        }
    }
    return g_flats[0].pixels;
}

/* Parses PNAMES and TEXTURE1/TEXTURE2 lumps to compile multi-patch composite wall textures */
int load_wall_textures(FILE *wad, filelump_t *lumps, int num_lumps) {
    int pnames_idx = -1;
    for (int i = 0; i < num_lumps; i++) {
        if (strncmp(lumps[i].name, "PNAMES", 6) == 0) {
            pnames_idx = i;
            break;
        }
    }
    if (pnames_idx == -1) return 0;

    /* Parse PNAMES list (map of patch names) */
    fseek(wad, lumps[pnames_idx].filepos, SEEK_SET);
    int32_t num_pnames = 0;
    fread(&num_pnames, 4, 1, wad);

    char (*pnames)[8] = malloc(num_pnames * 8);
    fread(pnames, 8, num_pnames, wad);

    /* Process both TEXTURE1 and TEXTURE2 lumps */
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

        uint8_t *tdata = malloc(lumps[tex_lump_idx].size);
        fseek(wad, lumps[tex_lump_idx].filepos, SEEK_SET);
        fread(tdata, 1, lumps[tex_lump_idx].size, wad);

        int32_t num_textures = *(int32_t*)tdata;
        int32_t *offsets = (int32_t*)(tdata + 4);

        int base_idx = g_num_wall_textures;
        g_num_wall_textures += num_textures;
        g_wall_textures = realloc(g_wall_textures, sizeof(untergang_wall_texture_t) * g_num_wall_textures);

        for (int i = 0; i < num_textures; i++) {
            uint8_t *entry = tdata + offsets[i];
            untergang_wall_texture_t *wt = &g_wall_textures[base_idx + i];
            memset(wt->name, 0, 9);
            strncpy(wt->name, (char*)entry, 8);

            wt->width  = *(uint16_t*)(entry + 12);
            wt->height = *(uint16_t*)(entry + 14);
            uint16_t num_patches = *(uint16_t*)(entry + 20);

            wt->pixels = malloc(wt->width * wt->height);
            memset(wt->pixels, 0, wt->width * wt->height);

            /* Composite patch data into final pixel array */
            uint8_t *patch_structs = entry + 22;
            for (int p = 0; p < num_patches; p++) {
                int16_t origin_x = *(int16_t*)(patch_structs + p * 10 + 0);
                int16_t origin_y = *(int16_t*)(patch_structs + p * 10 + 2);
                uint16_t pnum   = *(uint16_t*)(patch_structs + p * 10 + 4);

                if (pnum >= num_pnames) continue;

                int p_lump_idx = -1;
                for (int k = 0; k < num_lumps; k++) {
                    if (strncasecmp(lumps[k].name, pnames[pnum], 8) == 0) {
                        p_lump_idx = k;
                        break;
                    }
                }
                if (p_lump_idx == -1) continue;

                uint8_t *patch_data = malloc(lumps[p_lump_idx].size);
                fseek(wad, lumps[p_lump_idx].filepos, SEEK_SET);
                fread(patch_data, 1, lumps[p_lump_idx].size, wad);

                untergang_patch_header_t *ph = (untergang_patch_header_t*)patch_data;
                uint32_t *col_offsets = (uint32_t*)(patch_data + sizeof(untergang_patch_header_t));

                /* Unpack column posts into linear composite texture */
                for (int px = 0; px < ph->width; px++) {
                    int dest_x = origin_x + px;
                    if (dest_x < 0 || dest_x >= wt->width) continue;

                    uint8_t *col = patch_data + col_offsets[px];
                    while (1) {
                        uint8_t topdelta = col[0];
                        if (topdelta == 0xFF) break;

                        uint8_t length = col[1];
                        uint8_t *data = &col[3];

                        for (int py = 0; py < length; py++) {
                            int dest_y = origin_y + topdelta + py;
                            if (dest_y >= 0 && dest_y < wt->height) {
                                wt->pixels[dest_x * wt->height + dest_y] = data[py];
                            }
                        }
                        col += 4 + length;
                    }
                }
                free(patch_data);
            }
        }
        free(tdata);
    }
    free(pnames);
    printf("[VERNICHTUNG] Loaded %d composite wall textures!\n", g_num_wall_textures);
    return 1;
}

untergang_wall_texture_t* get_wall_texture(const char *name) {
    if (!g_wall_textures || g_num_wall_textures == 0) return NULL;
    for (int i = 0; i < g_num_wall_textures; i++) {
        if (strncasecmp(g_wall_textures[i].name, name, 8) == 0) {
            return &g_wall_textures[i];
        }
    }
    return NULL;
}

/* Loads TITLEPIC patch image lump and converts it into an RGBA SDL_Texture */
SDL_Texture* load_titlepic(SDL_Renderer *ren, FILE *wad, filelump_t *lumps, int num_lumps) {
    filelump_t *title_lump = NULL;
    for (int i = 0; i < num_lumps; i++) {
        if (strncmp(lumps[i].name, "TITLEPIC", 8) == 0) {
            title_lump = &lumps[i];
            break;
        }
    }
    if (!title_lump) return NULL;

    uint8_t *patch_data = (uint8_t*)malloc(title_lump->size);
    fseek(wad, title_lump->filepos, SEEK_SET);
    fread(patch_data, 1, title_lump->size, wad);

    untergang_patch_header_t *header = (untergang_patch_header_t*)patch_data;
    uint32_t *col_offsets = (uint32_t*)(patch_data + sizeof(untergang_patch_header_t));

    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, header->width, header->height, 32, SDL_PIXELFORMAT_RGBA8888);
    uint32_t *pixels = (uint32_t*)surf->pixels;
    memset(pixels, 0, surf->pitch * header->height);

    for (int x = 0; x < header->width; x++) {
        uint8_t *col = patch_data + col_offsets[x];
        while (1) {
            uint8_t topdelta = col[0];
            if (topdelta == 0xFF) break;

            uint8_t length = col[1];
            uint8_t *data = &col[3];

            for (int y = 0; y < length; y++) {
                int draw_y = topdelta + y;
                if (draw_y < header->height) {
                    uint8_t color_idx = data[y];
                    SDL_Color c = g_palette[color_idx];
                    pixels[draw_y * header->width + x] = (c.r << 24) | (c.g << 16) | (c.b << 8) | 255;
                }
            }
            col += 4 + length;
        }
    }

    SDL_Texture *tex = SDL_CreateTextureFromSurface(ren, surf);
    SDL_FreeSurface(surf);
    free(patch_data);
    return tex;
}

/* =========================================================================
 * SCREEN MELT TRANSITION SYSTEM
 * ========================================================================= */

void init_screen_melt(void) {
    g_melt.completed = 0;
    g_melt.y_offsets[0] = -(rand() % 16);
    
    for (int i = 1; i < MELT_WIDTH; i++) {
        int delta = (rand() % 5) - 2;
        g_melt.y_offsets[i] = g_melt.y_offsets[i - 1] + delta;
        if (g_melt.y_offsets[i] > 0)   g_melt.y_offsets[i] = 0;
        if (g_melt.y_offsets[i] < -32) g_melt.y_offsets[i] = -32;
    }

    g_state = STATE_MELTING;
}

void render_screen_melt(SDL_Renderer *ren, int screen_w, int screen_h) {
    int all_done = 1;
    float col_width = (float)screen_w / (float)MELT_WIDTH;
    int melt_speed = 20;

    for (int i = 0; i < MELT_WIDTH; i++) {
        if (g_melt.y_offsets[i] <= 0) {
            g_melt.y_offsets[i] += 2; 
            all_done = 0;
        } else {
            g_melt.y_offsets[i] += melt_speed;
            if (g_melt.y_offsets[i] < screen_h) {
                all_done = 0;
            }
        }

        int y_shift = g_melt.y_offsets[i];
        if (y_shift < 0) y_shift = 0;
        if (y_shift >= screen_h) continue;

        SDL_Rect src_col = { (int)(i * (320.0f / MELT_WIDTH)), 0, 1, MELT_HEIGHT };
        SDL_Rect dst_col = { (int)(i * col_width), y_shift, (int)ceilf(col_width), screen_h };

        SDL_RenderCopy(ren, g_title_texture, &src_col, &dst_col);
    }

    if (all_done) {
        g_state = STATE_IN_GAME;
    }
}

/* Handles interaction vector trigger when 'E' key is pressed */
void trigger_use_key(void) {
    float rad = deg_to_rad(g_player_angle);
    float reach = 64.0f;
    float target_x = g_player_x + sinf(rad) * reach;
    float target_y = g_player_y + cosf(rad) * reach;

    printf("[VERNICHTUNG] USE Key ('E') pressed! Vector: (%.1f, %.1f) -> (%.1f, %.1f)\n",
           g_player_x, g_player_y, target_x, target_y);
}

/* =========================================================================
 * BSP & GEOMETRY SPATIAL QUERY
 * ========================================================================= */

/* Traverses BSP tree recursively to determine which subsector contains (px, py) */
uint16_t get_subsector_at_pos(float px, float py, uint16_t node_id) {
    if (node_id & NF_SUBSECTOR) {
        return node_id & ~NF_SUBSECTOR;
    }
    untergang_node_t *node = &g_nodes[node_id];
    /* Cross-product point side evaluation relative to partition line */
    float check = (float)node->dx * (py - (float)node->y) - (float)node->dy * (px - (float)node->x);
    return get_subsector_at_pos(px, py, node->children[(check <= 0.0f) ? 0 : 1]);
}

/* Returns sector underlying point (px, py) */
untergang_sector_t* get_sector_at_pos(float px, float py) {
    uint16_t ssec_id = get_subsector_at_pos(px, py, (uint16_t)g_root_node);
    untergang_subsector_t ssec = g_ssectors[ssec_id];
    untergang_seg_t seg = g_segs[ssec.first_seg];
    untergang_linedef_t line = g_linedefs[seg.linedef];
    return &g_sectors[g_sidedefs[line.sidenum[seg.side]].sector];
}

/* =========================================================================
 * PHYSICS & COLLISION SYSTEM
 * ========================================================================= */

/* Pushes target position outside of line segment if closer than circle radius */
void collide_with_line(float *target_x, float *target_y, float v1x, float v1y, float v2x, float v2y, float radius) {
    float line_dx = v2x - v1x;
    float line_dy = v2y - v1y;
    float len_sq = line_dx * line_dx + line_dy * line_dy;
    if (len_sq == 0.0f) return;

    /* Project player position onto line segment */
    float t = ((*target_x - v1x) * line_dx + (*target_y - v1y) * line_dy) / len_sq;
    t = clamp_float(t, 0.0f, 1.0f);

    float cx = v1x + t * line_dx;
    float cy = v1y + t * line_dy;
    float dx = *target_x - cx;
    float dy = *target_y - cy;
    float dist = sqrtf(dx * dx + dy * dy);

    if (dist < radius) {
        if (dist == 0.0f) dist = 0.001f;
        float overlap = radius - dist;
        *target_x += (dx / dist) * overlap;
        *target_y += (dy / dist) * overlap;
    }
}

/* Iterates solid linedefs to resolve sliding movement response */
void check_wall_collisions(float *new_x, float *new_y, float current_floor_h) {
    for (int pass = 0; pass < 3; pass++) {
        for (int i = 0; i < g_num_linedefs; i++) {
            untergang_linedef_t *line = &g_linedefs[i];
            int is_solid = (line->sidenum[1] == 0xFFFF) || (line->flags & 0x0001);

            /* Two-sided step obstruction check */
            if (!is_solid && line->sidenum[1] != 0xFFFF) {
                untergang_sector_t *sec_a = &g_sectors[g_sidedefs[line->sidenum[0]].sector];
                untergang_sector_t *sec_b = &g_sectors[g_sidedefs[line->sidenum[1]].sector];
                float target_floor = (sec_a->floor_h == (int16_t)current_floor_h) ? (float)sec_b->floor_h : (float)sec_a->floor_h;
                if ((target_floor - current_floor_h) > MAX_STEP_HEIGHT) is_solid = 1;
            }

            if (!is_solid) continue;
            untergang_vertex_t v1 = g_vertices[line->v1];
            untergang_vertex_t v2 = g_vertices[line->v2];
            collide_with_line(new_x, new_y, (float)v1.x, (float)v1.y, (float)v2.x, (float)v2.y, PLAYER_RADIUS);
        }
    }
}

/* Updates velocity momentum, stair stepping, gravity, and weapon bobbing */
void update_player_physics(const uint8_t *keys, int mouse_captured) {
    float rad = deg_to_rad(g_player_angle);
    float forward_x =  sinf(rad);
    float forward_y =  cosf(rad);
    float right_x   =  cosf(rad);
    float right_y   = -sinf(rad);

    float accel_x = 0.0f;
    float accel_y = 0.0f;

    if (mouse_captured) {
        if (keys[SDL_SCANCODE_W]) { accel_x += forward_x * UNTERGANG_ACCEL; accel_y += forward_y * UNTERGANG_ACCEL; }
        if (keys[SDL_SCANCODE_S]) { accel_x -= forward_x * UNTERGANG_ACCEL; accel_y -= forward_y * UNTERGANG_ACCEL; }
        if (keys[SDL_SCANCODE_D]) { accel_x += right_x   * UNTERGANG_ACCEL; accel_y += right_y   * UNTERGANG_ACCEL; }
        if (keys[SDL_SCANCODE_A]) { accel_x -= right_x   * UNTERGANG_ACCEL; accel_y -= right_y   * UNTERGANG_ACCEL; }
    }

    g_player_momx += accel_x;
    g_player_momy += accel_y;

    /* Apply friction depending on air movement */
    if (g_is_grounded) {
        g_player_momx *= UNTERGANG_FRICTION;
        g_player_momy *= UNTERGANG_FRICTION;
    } else {
        g_player_momx *= 0.98f;
        g_player_momy *= 0.98f;
    }

    float target_x = g_player_x + g_player_momx;
    float target_y = g_player_y + g_player_momy;

    untergang_sector_t *cur_sec = get_sector_at_pos(g_player_x, g_player_y);
    check_wall_collisions(&target_x, &target_y, (float)cur_sec->floor_h);

    g_player_x = target_x;
    g_player_y = target_y;

    /* Update vertical position and gravity */
    untergang_sector_t *new_sec = get_sector_at_pos(g_player_x, g_player_y);
    float floor_height = (float)new_sec->floor_h;

    if (!g_is_grounded) {
        g_player_momz -= GRAVITY;
        g_player_target_z += g_player_momz;
        if (g_player_target_z <= floor_height + EYE_HEIGHT) {
            g_player_target_z = floor_height + EYE_HEIGHT;
            g_player_momz = 0.0f;
            g_is_grounded = 1;
        }
    } else {
        if (floor_height + EYE_HEIGHT < g_player_target_z - MAX_STEP_HEIGHT) {
            g_is_grounded = 0;
        } else {
            g_player_target_z = floor_height + EYE_HEIGHT;
        }
    }

    /* Smooth camera interpolation over steps */
    g_player_z += (g_player_target_z - g_player_z) * 0.35f;

    /* View bobbing calculation */
    float speed = sqrtf(g_player_momx * g_player_momx + g_player_momy * g_player_momy);
    if (g_is_grounded && speed > 0.5f) {
        g_bob_phase += speed * 0.05f;
        g_player_z += sinf(g_bob_phase) * 2.0f;
    } else {
        g_bob_phase = 0.0f;
    }
}

/* =========================================================================
 * SOFTWARE RENDERER (BSP Traversal & Column Projection)
 * ========================================================================= */

/* Renders a single wall segment, project walls/flats column by column */
void render_seg(SDL_Renderer *ren, untergang_seg_t *seg, float player_rad, float focal_length, int screen_w, int screen_h) {
    untergang_vertex_t v1 = g_vertices[seg->v1];
    untergang_vertex_t v2 = g_vertices[seg->v2];
    untergang_linedef_t line = g_linedefs[seg->linedef];
    uint16_t front_side_idx = line.sidenum[seg->side];
    if (front_side_idx == 0xFFFF) return;

    untergang_sidedef_t front_side = g_sidedefs[front_side_idx];
    untergang_sector_t front_sec   = g_sectors[front_side.sector];
    uint16_t back_side_idx = line.sidenum[seg->side ^ 1];
    int is_two_sided = (back_side_idx != 0xFFFF);

    /* Transform relative world space coordinates to view camera space */
    float dx1 = (float)v1.x - g_player_x;
    float dy1 = (float)v1.y - g_player_y;
    float dx2 = (float)v2.x - g_player_x;
    float dy2 = (float)v2.y - g_player_y;

    float cos_a = cosf(player_rad);
    float sin_a = sinf(player_rad);

    float rx1 = -dx1 * sin_a + dy1 * cos_a;
    float tz1 =  dx1 * cos_a + dy1 * sin_a;
    float rx2 = -dx2 * sin_a + dy2 * cos_a;
    float tz2 =  dx2 * cos_a + dy2 * sin_a;

    float near_clip = 1.0f;
    if (tz1 < near_clip && tz2 < near_clip) return;

    float orig_rx1 = rx1, orig_tz1 = tz1;
    float orig_rx2 = rx2, orig_tz2 = tz2;

    /* Near Z frustum clipping */
    if (tz1 < near_clip) {
        float t = (near_clip - tz1) / (tz2 - tz1);
        rx1 = rx1 + t * (rx2 - rx1);
        tz1 = near_clip;
    } else if (tz2 < near_clip) {
        float t = (near_clip - tz2) / (tz1 - tz2);
        rx2 = rx2 + t * (rx1 - rx2);
        tz2 = near_clip;
    }

    /* Perspective projection onto screen X space */
    int sx1 = (int)(screen_w / 2.0f - (rx1 * focal_length) / tz1);
    int sx2 = (int)(screen_w / 2.0f - (rx2 * focal_length) / tz2);

    if (sx1 >= sx2 || sx2 < 0 || sx1 >= screen_w) return;

    int render_x1 = clamp_int(sx1, 0, screen_w - 1);
    int render_x2 = clamp_int(sx2, 0, screen_w - 1);

    float iz1 = 1.0f / tz1;
    float iz2 = 1.0f / tz2;
    float half_h = screen_h / 2.0f;

    uint8_t *ceil_flat  = get_flat_data(front_sec.ceil_tex);
    uint8_t *floor_flat = get_flat_data(front_sec.floor_tex);

    untergang_wall_texture_t *mid_tex  = get_wall_texture(front_side.mid_tex);
    untergang_wall_texture_t *top_tex  = get_wall_texture(front_side.top_tex);
    untergang_wall_texture_t *bot_tex  = get_wall_texture(front_side.bottom_tex);

    float seg_len = sqrtf((v2.x - v1.x)*(v2.x - v1.x) + (v2.y - v1.y)*(v2.y - v1.y));

    /* Raycasting/Column loop across projected segment width */
    for (int x = render_x1; x <= render_x2; x++) {
        int u_clip = g_upper_clip[x];
        int l_clip = g_lower_clip[x];
        if (u_clip >= l_clip) continue;

        float factor = (sx2 == sx1) ? 0.0f : (float)(x - sx1) / (float)(sx2 - sx1);
        float iz = iz1 + factor * (iz2 - iz1);
        float tz = 1.0f / iz;

        /* Wall vertical height project */
        int y_ceil  = (int)(half_h - ((front_sec.ceiling_h - g_player_z) * focal_length) * iz);
        int y_floor = (int)(half_h - ((front_sec.floor_h   - g_player_z) * focal_length) * iz);

        float col_angle = player_rad + atanf((x - screen_w / 2.0f) / focal_length);
        float cos_col = cosf(col_angle);
        float sin_col = sinf(col_angle);

        /* 1. Render Ceiling Visplane */
        int ceil_stop = clamp_int(y_ceil, u_clip, l_clip);
        float ceil_height_diff = (front_sec.ceiling_h - g_player_z) * focal_length;

        for (int y = u_clip; y < ceil_stop; y++) {
            float dy = half_h - (float)y;
            if (fabsf(dy) < 0.001f) continue;

            float row_dist = ceil_height_diff / dy;
            float wx = g_player_x + row_dist * cos_col;
            float wy = g_player_y + row_dist * sin_col;

            int fx = ((unsigned int)(int)floorf(wx)) & 63;
            int fy = ((unsigned int)(int)floorf(wy)) & 63;

            if (ceil_flat) {
                uint8_t pal_idx = ceil_flat[fy * 64 + fx];
                SDL_Color c = g_palette[pal_idx];
                float shadow = clamp_float(1.0f - (row_dist / 1500.0f), 0.1f, 1.0f);

                uint32_t argb = (255 << 24) | 
                                ((uint8_t)(c.r * shadow) << 16) | 
                                ((uint8_t)(c.g * shadow) << 8)  | 
                                ((uint8_t)(c.b * shadow));
                g_pixel_buffer[y * screen_w + x] = argb;
            }
        }

        /* 2. Render Floor Visplane */
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

                uint32_t argb = (255 << 24) | 
                                ((uint8_t)(c.r * shadow) << 16) | 
                                ((uint8_t)(c.g * shadow) << 8)  | 
                                ((uint8_t)(c.b * shadow));
                g_pixel_buffer[y * screen_w + x] = argb;
            }
        }

        float shadow_val = clamp_float(1.0f - (tz / 2000.0f), 0.1f, 1.0f);

        float orig_sx1 = (screen_w / 2.0f - (orig_rx1 * focal_length) / orig_tz1);
        float orig_sx2 = (screen_w / 2.0f - (orig_rx2 * focal_length) / orig_tz2);
        float seg_u = seg->offset + front_side.x_offset + ((x - orig_sx1) / (orig_sx2 - orig_sx1)) * seg_len;

        /* 3. Render Solid or Two-sided Portal Wall textures */
        if (!is_two_sided) {
            int draw_top = clamp_int(y_ceil, u_clip, l_clip);
            int draw_bot = clamp_int(y_floor, u_clip, l_clip);

            if (mid_tex && mid_tex->pixels && (y_floor > y_ceil)) {
                int u_tex = ((unsigned int)(int)floorf(seg_u)) % mid_tex->width;
                for (int y = draw_top; y < draw_bot; y++) {
                    float v_scale = (float)mid_tex->height / (float)(y_floor - y_ceil);
                    int v_tex = ((int)((y - y_ceil) * v_scale) + front_side.y_offset) % mid_tex->height;
                    if (v_tex < 0) v_tex += mid_tex->height;

                    uint8_t pal_idx = mid_tex->pixels[u_tex * mid_tex->height + v_tex];
                    SDL_Color c = g_palette[pal_idx];
                    g_pixel_buffer[y * screen_w + x] = (255 << 24) | 
                        ((uint8_t)(c.r * shadow_val) << 16) | 
                        ((uint8_t)(c.g * shadow_val) << 8)  | 
                        ((uint8_t)(c.b * shadow_val));
                }
            } else {
                uint32_t wall_color = (255 << 24) | ((uint8_t)(210 * shadow_val) << 16) | ((uint8_t)(120 * shadow_val) << 8) | ((uint8_t)(50 * shadow_val));
                for (int y = draw_top; y < draw_bot; y++) g_pixel_buffer[y * screen_w + x] = wall_color;
            }
            g_upper_clip[x] = screen_h; /* Fully occlude column behind solid wall */
        } else {
            /* Handle Upper/Lower step portals for 2-sided lines */
            untergang_sidedef_t back_side = g_sidedefs[back_side_idx];
            untergang_sector_t back_sec   = g_sectors[back_side.sector];
            int y_bceil  = (int)(half_h - ((back_sec.ceiling_h - g_player_z) * focal_length) * iz);
            int y_bfloor = (int)(half_h - ((back_sec.floor_h   - g_player_z) * focal_length) * iz);

            /* Upper Wall step down */
            if (front_sec.ceiling_h > back_sec.ceiling_h) {
                int top_draw = clamp_int(y_ceil, u_clip, l_clip);
                int bot_draw = clamp_int(y_bceil, u_clip, l_clip);

                if (top_tex && top_tex->pixels && (y_bceil > y_ceil)) {
                    int u_tex = ((unsigned int)(int)floorf(seg_u)) % top_tex->width;
                    for (int y = top_draw; y < bot_draw; y++) {
                        float v_scale = (float)top_tex->height / (float)(y_bceil - y_ceil);
                        int v_tex = ((int)((y - y_ceil) * v_scale) + front_side.y_offset) % top_tex->height;
                        if (v_tex < 0) v_tex += top_tex->height;

                        uint8_t pal_idx = top_tex->pixels[u_tex * top_tex->height + v_tex];
                        SDL_Color c = g_palette[pal_idx];
                        g_pixel_buffer[y * screen_w + x] = (255 << 24) | 
                            ((uint8_t)(c.r * shadow_val) << 16) | 
                            ((uint8_t)(c.g * shadow_val) << 8)  | 
                            ((uint8_t)(c.b * shadow_val));
                    }
                } else {
                    uint32_t top_wall_color = (255 << 24) | ((uint8_t)(180 * shadow_val) << 16) | ((uint8_t)(90 * shadow_val) << 8) | ((uint8_t)(40 * shadow_val));
                    for (int y = top_draw; y < bot_draw; y++) g_pixel_buffer[y * screen_w + x] = top_wall_color;
                }
                g_upper_clip[x] = clamp_int(bot_draw, u_clip, l_clip);
            }

            /* Lower Wall step up */
            if (front_sec.floor_h < back_sec.floor_h) {
                int top_draw = clamp_int(y_bfloor, u_clip, l_clip);
                int bot_draw = clamp_int(y_floor, u_clip, l_clip);

                if (bot_tex && bot_tex->pixels && (y_floor > y_bfloor)) {
                    int u_tex = ((unsigned int)(int)floorf(seg_u)) % bot_tex->width;
                    for (int y = top_draw; y < bot_draw; y++) {
                        float v_scale = (float)bot_tex->height / (float)(y_floor - y_bfloor);
                        int v_tex = ((int)((y - y_bfloor) * v_scale) + front_side.y_offset) % bot_tex->height;
                        if (v_tex < 0) v_tex += bot_tex->height;

                        uint8_t pal_idx = bot_tex->pixels[u_tex * bot_tex->height + v_tex];
                        SDL_Color c = g_palette[pal_idx];
                        g_pixel_buffer[y * screen_w + x] = (255 << 24) | 
                            ((uint8_t)(c.r * shadow_val) << 16) | 
                            ((uint8_t)(c.g * shadow_val) << 8)  | 
                            ((uint8_t)(c.b * shadow_val));
                    }
                } else {
                    uint32_t bot_wall_color = (255 << 24) | ((uint8_t)(150 * shadow_val) << 16) | ((uint8_t)(75 * shadow_val) << 8) | ((uint8_t)(30 * shadow_val));
                    for (int y = top_draw; y < bot_draw; y++) g_pixel_buffer[y * screen_w + x] = bot_wall_color;
                }
                g_lower_clip[x] = clamp_int(top_draw, u_clip, l_clip);
            }
        }
    }
}

/* Recursive front-to-back traversal of the map's Binary Space Partitioning tree */
void render_bsp_node(SDL_Renderer *ren, uint16_t node_id, float player_rad, float focal_length, int screen_w, int screen_h) {
    if (node_id & NF_SUBSECTOR) {
        uint16_t ssec_id = node_id & ~NF_SUBSECTOR;
        untergang_subsector_t ssec = g_ssectors[ssec_id];
        for (int i = 0; i < ssec.num_segs; i++) {
            render_seg(ren, &g_segs[ssec.first_seg + i], player_rad, focal_length, screen_w, screen_h);
        }
        return;
    }

    untergang_node_t *node = &g_nodes[node_id];

    float dx = (float)node->dx;
    float dy = (float)node->dy;
    float px = g_player_x - (float)node->x;
    float py = g_player_y - (float)node->y;

    /* Evaluate which side of splitting line camera sits on */
    int side = ((px * dy) - (py * dx) > 0.0f) ? 1 : 0;

    /* Render near child node first, then far child node */
    render_bsp_node(ren, node->children[side], player_rad, focal_length, screen_w, screen_h);
    render_bsp_node(ren, node->children[side ^ 1], player_rad, focal_length, screen_w, screen_h);
}

/* =========================================================================
 * ENGINE INITIALIZATION & MAIN GAME LOOP
 * ========================================================================= */

int main(int argc, char *argv[]) {
    srand((unsigned int)time(NULL));
    const char *filename = NULL;

    /* Parse mandatory -iwad command line flag */
    for (int i = 1; i < argc; i++) {
        if (strcasecmp(argv[i], "-iwad") == 0) {
            if (i + 1 < argc) {
                filename = argv[i + 1];
                i++;
            }
        }
    }

    if (!filename) {
        printf("[ERROR] No IWAD specified! Usage: %s -iwad <path/to/game.wad>\n", argv[0]);
        return 1;
    }

    FILE *file = fopen(filename, "rb");
    if (!file) {
        printf("[ERROR] Could not open WAD file: '%s'\n", filename);
        return 1;
    }

    printf("[VERNICHTUNG] Successfully opened IWAD file: %s\n", filename);

    /* Read header and lump directory */
    wadheader_t header;
    fread(&header, sizeof(wadheader_t), 1, file);

    fseek(file, header.infotableofs, SEEK_SET);
    filelump_t *directory = malloc(sizeof(filelump_t) * header.numlumps);
    fread(directory, sizeof(filelump_t), header.numlumps, file);

    /* Load system graphics & assets */
    load_playpal(file, directory, header.numlumps);
    load_flats(file, directory, header.numlumps);
    load_wall_textures(file, directory, header.numlumps);

    /* Locate level map marker (E1M1 or MAP01) */
    int map_idx = -1;
    for (int i = 0; i < header.numlumps; i++) {
        char name[9] = {0};
        strncpy(name, directory[i].name, 8);
        if (strcmp(name, "E1M1") == 0 || strcmp(name, "MAP01") == 0) {
            map_idx = i;
            break;
        }
    }

    if (map_idx == -1) {
        printf("[ERROR] E1M1 / MAP01 lump not found in WAD!\n");
        fclose(file); free(directory); return 1;
    }

    /* Assign lump pointers relative to map offset */
    filelump_t things_l   = directory[map_idx + 1];
    filelump_t linedefs_l = directory[map_idx + 2];
    filelump_t sidedefs_l = directory[map_idx + 3];
    filelump_t vertexes_l = directory[map_idx + 4];
    filelump_t segs_l     = directory[map_idx + 5];
    filelump_t ssectors_l = directory[map_idx + 6];
    filelump_t nodes_l    = directory[map_idx + 7];
    filelump_t sectors_l  = directory[map_idx + 8];

    /* Load Player 1 start point */
    int num_things = things_l.size / sizeof(untergang_thing_t);
    untergang_thing_t *things = malloc(things_l.size);
    fseek(file, things_l.filepos, SEEK_SET);
    fread(things, sizeof(untergang_thing_t), num_things, file);

    for (int i = 0; i < num_things; i++) {
        if (things[i].type == 1) { 
            g_player_x = (float)things[i].x;
            g_player_y = (float)things[i].y;
            g_player_angle = (float)things[i].angle;
            break;
        }
    }
    free(things);

    /* Allocate memory and read level geometry into system buffers */
    g_vertices     = malloc(vertexes_l.size);
    g_segs         = malloc(segs_l.size);
    g_sectors      = malloc(sectors_l.size);
    g_sidedefs     = malloc(sidedefs_l.size);
    g_linedefs     = malloc(linedefs_l.size);
    g_ssectors     = malloc(ssectors_l.size);
    g_nodes        = malloc(nodes_l.size);

    g_num_nodes    = nodes_l.size / sizeof(untergang_node_t);
    g_num_linedefs = linedefs_l.size / sizeof(untergang_linedef_t);
    g_root_node    = g_num_nodes - 1;

    fseek(file, vertexes_l.filepos, SEEK_SET); fread(g_vertices, sizeof(untergang_vertex_t), vertexes_l.size / sizeof(untergang_vertex_t), file);
    fseek(file, segs_l.filepos, SEEK_SET);     fread(g_segs, sizeof(untergang_seg_t), segs_l.size / sizeof(untergang_seg_t), file);
    fseek(file, sectors_l.filepos, SEEK_SET);  fread(g_sectors, sizeof(untergang_sector_t), sectors_l.size / sizeof(untergang_sector_t), file);
    fseek(file, sidedefs_l.filepos, SEEK_SET);  fread(g_sidedefs, sizeof(untergang_sidedef_t), sidedefs_l.size / sizeof(untergang_sidedef_t), file);
    fseek(file, linedefs_l.filepos, SEEK_SET);  fread(g_linedefs, sizeof(untergang_linedef_t), g_num_linedefs, file);
    fseek(file, ssectors_l.filepos, SEEK_SET); fread(g_ssectors, sizeof(untergang_subsector_t), ssectors_l.size / sizeof(untergang_subsector_t), file);
    fseek(file, nodes_l.filepos, SEEK_SET);    fread(g_nodes, sizeof(untergang_node_t), g_num_nodes, file);

    /* Position player camera vertical floor level */
    untergang_sector_t *start_sec = get_sector_at_pos(g_player_x, g_player_y);
    g_player_z = (float)start_sec->floor_h + EYE_HEIGHT;
    g_player_target_z = g_player_z;

    /* Initialize SDL2 Video Subsystem */
    SDL_Init(SDL_INIT_VIDEO);

    SDL_Window *win = SDL_CreateWindow("VERNICHTUNG ENGINE - Crisp Core", 
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 
                                       640, 400, SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);

    g_title_texture = load_titlepic(ren, file, directory, header.numlumps);

    fclose(file); free(directory);

    int mouse_captured = 1;
    SDL_SetRelativeMouseMode(SDL_TRUE);

    int cur_w, cur_h;
    SDL_GetWindowSize(win, &cur_w, &cur_h);

    init_framebuffer(ren, cur_w, cur_h);

    g_upper_clip = malloc(sizeof(int) * cur_w);
    g_lower_clip = malloc(sizeof(int) * cur_w);

    int running = 1;
    SDL_Event e;
    const uint8_t *keys = SDL_GetKeyboardState(NULL);

    /* MAIN GAME ENGINE LOOP */
    while (running) {
        uint32_t frame_start = SDL_GetTicks();

        /* Process OS and Input Events */
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) {
                    mouse_captured = !mouse_captured;
                    SDL_SetRelativeMouseMode(mouse_captured ? SDL_TRUE : SDL_FALSE);
                }
                if (g_state == STATE_TITLE_SCREEN) {
                    if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_SPACE || e.key.keysym.sym == SDLK_e) {
                        init_screen_melt();
                    }
                } else if (g_state == STATE_IN_GAME) {
                    if (e.key.keysym.sym == SDLK_e) {
                        trigger_use_key();
                    }
                }
            }
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_RESIZED) {
                cur_w = e.window.data1;
                cur_h = e.window.data2;
                init_framebuffer(ren, cur_w, cur_h);

                int *tmp_u = realloc(g_upper_clip, sizeof(int) * cur_w);
                int *tmp_l = realloc(g_lower_clip, sizeof(int) * cur_w);
                if (tmp_u) g_upper_clip = tmp_u;
                if (tmp_l) g_lower_clip = tmp_l;
            }
            if (e.type == SDL_MOUSEMOTION && mouse_captured && g_state == STATE_IN_GAME) {
                g_player_angle -= e.motion.xrel * MOUSE_SENSITIVITY;
                g_player_angle = normalize_angle(g_player_angle);
            }
        }

        /* Update simulation state */
        if (g_state == STATE_IN_GAME) {
            update_player_physics(keys, mouse_captured);
        }

        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);

        /* Render game screen state */
        if (g_state == STATE_TITLE_SCREEN) {
            if (g_title_texture) {
                SDL_RenderCopy(ren, g_title_texture, NULL, NULL);
            }
        } 
        else if (g_state == STATE_MELTING || g_state == STATE_IN_GAME) {
            clear_pixel_buffer(0xFF000000);

            /* Reset vertical column clip arrays */
            for (int i = 0; i < cur_w; i++) {
                g_upper_clip[i] = 0;
                g_lower_clip[i] = cur_h;
            }

            /* Calculate focal distance and aspect ratio scalar */
            float aspect_ratio = (float)cur_w / (float)cur_h;
            float fov = FOV_BASE * (aspect_ratio / (4.0f / 3.0f));
            float focal_length = (cur_w / 2.0f) / tanf(deg_to_rad(fov / 2.0f));

            float rad = deg_to_rad(g_player_angle);
            render_bsp_node(ren, (uint16_t)g_root_node, rad, focal_length, cur_w, cur_h);

            SDL_UpdateTexture(g_framebuffer_texture, NULL, g_pixel_buffer, cur_w * sizeof(uint32_t));
            SDL_RenderCopy(ren, g_framebuffer_texture, NULL, NULL);

            if (g_state == STATE_MELTING && g_title_texture) {
                render_screen_melt(ren, cur_w, cur_h);
            }
        }

        SDL_RenderPresent(ren);

        /* Fixed framerate capper */
        uint32_t frame_time = SDL_GetTicks() - frame_start;
        if (frame_time < FRAME_TIME_MS) {
            SDL_Delay(FRAME_TIME_MS - frame_time);
        }
    }

    /* Cleanup engine memory & exit */
    if (g_pixel_buffer) free(g_pixel_buffer);
    if (g_framebuffer_texture) SDL_DestroyTexture(g_framebuffer_texture);
    if (g_title_texture) SDL_DestroyTexture(g_title_texture);
    if (g_flats) free(g_flats);
    if (g_wall_textures) {
        for (int i = 0; i < g_num_wall_textures; i++) {
            if (g_wall_textures[i].pixels) free(g_wall_textures[i].pixels);
        }
        free(g_wall_textures);
    }
    free(g_upper_clip); free(g_lower_clip);
    free(g_vertices); free(g_segs); free(g_sectors); free(g_sidedefs); free(g_linedefs); free(g_ssectors); free(g_nodes);
    SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
