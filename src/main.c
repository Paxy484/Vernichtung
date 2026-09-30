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
#include <time.h>
#include <math.h>
#include <SDL2/SDL.h>
#include "untergangtype.h"

/* =========================================================================
 * GLOBAL VARIABLE ENGINE INSTANTIATIONS
 * ========================================================================= */

/* Level Geometry Buffers */
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

/* Global Occlusion Clipping Columns Instantiated */
int *g_upper_clip = NULL;
int *g_lower_clip = NULL;

/* Player State Space Vectors */
float g_player_x        = 0.0f;
float g_player_y        = 0.0f;
float g_player_z        = EYE_HEIGHT;
float g_player_target_z = EYE_HEIGHT;
float g_player_angle    = 0.0f;

float g_player_momx = 0.0f;
float g_player_momy = 0.0f;
float g_player_momz = 0.0f;
int   g_is_grounded = 1;
float g_bob_phase   = 0.0f;

/* Central Engine Runtime Unified State Flag - Shared Globals Context */
game_state_t g_state = STATE_TITLE_SCREEN;

/* =========================================================================
 * EXTERNAL MODULE CROSS-LINK DECLARATIONS
 * ========================================================================= */

/* Video Layer Hooks (src/i_video.c) */
void init_framebuffer(SDL_Renderer *ren, int w, int h);
void clear_pixel_buffer(uint32_t color);
SDL_Texture* load_titlepic(SDL_Renderer *ren, FILE *file, filelump_t *lumps, int num_lumps);
void init_screen_melt(void);
void render_screen_melt(SDL_Renderer *ren, int screen_w, int screen_h);
void destroy_video_subsystem(void);

extern uint32_t *g_pixel_buffer;
extern int       g_buffer_w;
extern int       g_buffer_h;
extern SDL_Texture *g_framebuffer_texture;

/* Input Layer Hooks (src/i_input.c) */
void process_mouse_input(int xrel);
void trigger_use_key(void);

/* Physics Layer Hooks (src/p_physics.c) */
void update_player_physics(const uint8_t *keys, int mouse_captured);
untergang_sector_t* get_sector_at_pos(float px, float py);

/* Renderer Layer Hooks (src/r_main.c) */
void render_bsp_node(SDL_Renderer *ren, uint16_t node_id, float player_rad, float focal_length, int screen_w, int screen_h);

/* Archive Layer Hooks (src/u_archive.c) */
int load_playpal(FILE *file, filelump_t *lumps, int num_lumps);
int load_flats(FILE *file, filelump_t *lumps, int num_lumps);
int load_wall_textures(FILE *file, filelump_t *lumps, int num_lumps);
void free_archive_cache(void);

/* Internal Fingerprint Scanner Prototype */
const char* determine_iwad_fingerprint(filelump_t *directory, int num_lumps);

/* =========================================================================
 * CASE-SENSITIVE UPPERCASE LUMP FINGERPRINT SCANNER
 * ========================================================================= */
const char* determine_iwad_fingerprint(filelump_t *directory, int num_lumps) {
    int has_e2m1 = 0;
    int has_e4m1 = 0;
    int has_super_shotgun_sound = 0; /* Checked via DSDSHTGN lump */
    
    /* Strict Open Source Freedoom Specific Graphical Indicators */
    int is_freedoom_engine_raw = 0;   /* Checked via FREEDOOM lump signature */
    int has_freedoom_phase1_menu = 0; /* Checked via M_PHAS1 lump */
    int has_freedoom_phase2_menu = 0; /* Checked via M_PHAS2 lump */
    int has_tnt_exclusive_map = 0;    /* Checked via MAP33 lump */

    /* Scan the entire pre-loaded WAD directory for fingerprint lumps */
    for (int i = 0; i < num_lumps; i++) {
        char name[9] = {0}; /* 9 elements ensures a guaranteed null terminator at index 8 */
        memcpy(name, directory[i].name, 8);

        /* Case-Sensitive comparisons since inside WAD directories everything is strictly UPPERCASE */
        if (strcmp(name, "E2M1") == 0)              has_e2m1 = 1;
        else if (strcmp(name, "E4M1") == 0)         has_e4m1 = 1;
        else if (strcmp(name, "DSDSHTGN") == 0)     has_super_shotgun_sound = 1;
        else if (strcmp(name, "MAP33") == 0)        has_tnt_exclusive_map = 1;
        else if (strcmp(name, "FREEDOOM") == 0)     is_freedoom_engine_raw = 1;
        else if (strcmp(name, "M_PHAS1") == 0)      has_freedoom_phase1_menu = 1;
        else if (strcmp(name, "M_PHAS2") == 0)      has_freedoom_phase2_menu = 1;
    }

    /* Unified Directory Identification Routing Engine */
    if (is_freedoom_engine_raw || has_freedoom_phase1_menu || has_freedoom_phase2_menu) {
        if (has_super_shotgun_sound || has_freedoom_phase2_menu) {
            return "Freedoom: Phase 2";
        }
        return "Freedoom: Phase 1";
    }

    if (has_super_shotgun_sound) {
        if (has_tnt_exclusive_map) return "Final DOOM: TNT - Evilution";
        
        /* Check if this WAD contains Final Doom's exclusive menu palette lump */
        int is_final_doom = 0;
        for (int i = 0; i < num_lumps; i++) {
            char name[9] = {0};
            memcpy(name, directory[i].name, 8);
            if (strcmp(name, "DMENUPAL") == 0) {
                is_final_doom = 1;
                break;
            }
        }

        /* If it has the Final Doom menu palette indicator, it's Plutonia! Otherwise, it's vanilla Doom 2! */
        if (is_final_doom) {
            return "Final DOOM: The Plutonia Experiment";
        }
        
        return "DOOM 2: Hell on Earth";
    }

    if (has_e4m1) return "The Ultimate DOOM";
    if (has_e2m1) return "DOOM (Registered)";

    return "DOOM: Shareware";
}

/* =========================================================================
 * ENGINE LIFECYCLE EXECUTION
 * ========================================================================= */

int main(int argc, char *argv[]) {
    srand((unsigned int)time(NULL));
    const char *wad_filename = NULL;

    /* 1. Parse command-line flags cleanly for asset layer tracking */
    for (int i = 1; i < argc; i++) {
        if (strcasecmp(argv[i], "-iwad") == 0) {
            if (i + 1 < argc) {
                wad_filename = argv[i + 1];
                i++;
            }
        }
    }

    if (!wad_filename) {
        printf("[ERROR] No unencumbered asset archive target specified!\n");
        printf("Usage: %s -iwad <path/to/game.wad>\n", argv[0]);
        return 1;
    }

    FILE *file = fopen(wad_filename, "rb");
    if (!file) {
        printf("[ERROR] Could not open file stream: '%s'\n", wad_filename);
        return 1;
    }

    /* 2. Read WAD master layout headers and load file directories */
    wadheader_t header;
    if (fread(&header, sizeof(wadheader_t), 1, file) != 1) {
        fclose(file); 
        return 1;
    }

    fseek(file, header.infotableofs, SEEK_SET);
    filelump_t *directory = malloc(sizeof(filelump_t) * header.numlumps);
    if (fread(directory, sizeof(filelump_t), header.numlumps, file) != (size_t)header.numlumps) {
        fclose(file); 
        free(directory); 
        return 1;
    }

    /* Execute Case-Sensitive Lump Fingerprint check right here! */
    const char *detected_game = determine_iwad_fingerprint(directory, header.numlumps);

    /* =========================================================================
     * PRINT VINTAGE STANDALONE CONSOLE SPLASH BANNER
     * ========================================================================= */
    printf("===========================================================================\n");
    printf("                         %s\n", detected_game);
    printf("===========================================================================\n");
    printf(" Vernichtung is free software, covered by the 3-Clause BSD License.\n");
    printf(" There is NO warranty; not even for MERCHANTABILITY or FITNESS FOR A\n");
    printf(" PARTICULAR PURPOSE. You are welcome to change and distribute copies\n");
    printf(" under certain conditions. See the source for more information.\n");
    printf("===========================================================================\n");

    printf("[VERNICHTUNG] Opening asset file stream: %s\n", wad_filename);

    /* 3. Invoke archive managers to parse colors and load graphics metadata */
    load_playpal(file, directory, header.numlumps);
    load_flats(file, directory, header.numlumps);
    load_wall_textures(file, directory, header.numlumps);

    /* 4. Locate initial map data chunk position pointers */
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
        printf("[ERROR] Initial core map indices (E1M1/MAP01) missing from target archive.\n");
        fclose(file); 
        free(directory); 
        return 1;
    }

    /* Assign sequential offset offsets matching standard binary file arrays */
    filelump_t things_l   = directory[map_idx + 1];
    filelump_t linedefs_l = directory[map_idx + 2];
    filelump_t sidedefs_l = directory[map_idx + 3];
    filelump_t vertexes_l = directory[map_idx + 4];
    filelump_t segs_l     = directory[map_idx + 5];
    filelump_t ssectors_l = directory[map_idx + 6];
    filelump_t nodes_l    = directory[map_idx + 7];
    filelump_t sectors_l  = directory[map_idx + 8];

    /* 5. Parse map player start thing coordinate entities */
    int num_things = things_l.size / sizeof(untergang_thing_t);
    untergang_thing_t *things = malloc(things_l.size);
    fseek(file, things_l.filepos, SEEK_SET);
    if (fread(things, sizeof(untergang_thing_t), num_things, file) == (size_t)num_things) {
        for (int i = 0; i < num_things; i++) {
            if (things[i].type == 1) { /* Type 1 designates Player 1 Spawn Position */
                g_player_x = (float)things[i].x;
                g_player_y = (float)things[i].y;
                g_player_angle = (float)things[i].angle;
                break;
            }
        }
    }
    free(things);

    /* 6. Allocate runtime space maps and pull level structures cleanly into RAM */
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

    fseek(file, vertexes_l.filepos, SEEK_SET);  if (fread(g_vertices, 1, vertexes_l.size, file) != (size_t)vertexes_l.size) {}
    fseek(file, segs_l.filepos, SEEK_SET);      if (fread(g_segs, 1, segs_l.size, file) != (size_t)segs_l.size) {}
    fseek(file, sectors_l.filepos, SEEK_SET);   if (fread(g_sectors, 1, sectors_l.size, file) != (size_t)sectors_l.size) {}
    fseek(file, sidedefs_l.filepos, SEEK_SET);  if (fread(g_sidedefs, 1, sidedefs_l.size, file) != (size_t)sidedefs_l.size) {}
    fseek(file, linedefs_l.filepos, SEEK_SET);  if (fread(g_linedefs, 1, linedefs_l.size, file) != (size_t)linedefs_l.size) {}
    fseek(file, ssectors_l.filepos, SEEK_SET); if (fread(g_ssectors, 1, ssectors_l.size, file) != (size_t)ssectors_l.size) {}
    fseek(file, nodes_l.filepos, SEEK_SET);     if (fread(g_nodes, 1, nodes_l.size, file) != (size_t)nodes_l.size) {}

    /* Map player height coordinates cleanly to the physical baseline sector deck */
    untergang_sector_t *start_sec = get_sector_at_pos(g_player_x, g_player_y);
    g_player_z = (float)start_sec->floor_h + EYE_HEIGHT;
    g_player_target_z = g_player_z;

    /* 7. Initialize OS Video Subsystems via SDL2 API layers */
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fclose(file); 
        free(directory); 
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow("VERNICHTUNG ENGINE", 
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 
                                       640, 400, SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);

    /* =========================================================================
     * This is 320x200 to match id tech 1
     * ========================================================================= */
    /* Tell SDL to lock our interactive game window space to a sharp 320x200 grid */
    SDL_RenderSetLogicalSize(ren, 320, 200); 

    /* Extract title splash using our video wrapper module functions */
    SDL_Texture *title_texture = load_titlepic(ren, file, directory, header.numlumps);

    fclose(file); 
    free(directory);

    /* Lock down hardware pointer boundaries */
    int mouse_captured = 1;
    SDL_SetRelativeMouseMode(SDL_TRUE);

    /* Explicitly initialize your frame buffers directly at 320x200 layout bounds */
    init_framebuffer(ren, 320, 200);

    /* Allocate system column occlusion metrics matching our 320 logic columns */
    g_upper_clip = malloc(sizeof(int) * 320);
    g_lower_clip = malloc(sizeof(int) * 320);

    int running = 1;
    SDL_Event event;
    const uint8_t *keys = SDL_GetKeyboardState(NULL);

    /* =====================================================================
     * CENTRAL SIMULATION TICK TIMER HEARTBEAT LOOP
     * ===================================================================== */
    while (running) {
        uint32_t frame_start = SDL_GetTicks();

        /* Intercept standard hardware inputs and pass structural states down */
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            }
            
            if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    mouse_captured = !mouse_captured;
                    SDL_SetRelativeMouseMode(mouse_captured ? SDL_TRUE : SDL_FALSE);
                }

                // Unified state machine progression routing triggers
                if (g_state == STATE_TITLE_SCREEN) {
                    if (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_SPACE || event.key.keysym.sym == SDLK_e) {
                        init_screen_melt();
                        g_state = STATE_MELTING;
                    }
                } else if (g_state == STATE_IN_GAME) {
                    if (event.key.keysym.sym == SDLK_e) {
                        trigger_use_key();
                    }
                }
            }

            // Send horizontal mouse displacement ticks straight into the look calculator module (FIXED NESTING)
            if (event.type == SDL_MOUSEMOTION && mouse_captured && g_state == STATE_IN_GAME) {
                process_mouse_input(event.motion.xrel);
            }
        }

        // Advance game physics environment parameters if simulation is active (FIXED NESTING)
        if (g_state == STATE_IN_GAME) {
            update_player_physics(keys, mouse_captured);
        }

        // Clear hardware screen context devices before redraw sequences
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);

        // Render system graphical arrays based on engine status targets (FIXED NESTING)
        if (g_state == STATE_TITLE_SCREEN) {
            if (title_texture) {
                SDL_RenderCopy(ren, title_texture, NULL, NULL);
            }
        } else if (g_state == STATE_MELTING || g_state == STATE_IN_GAME) {
            clear_pixel_buffer(0xFF000000); // Wipe software array canvas rows black
            
            /* Reset vertical tracking clipping markers across our 320 columns */
            for (int i = 0; i < 320; i++) {
                g_upper_clip[i] = 0;
                g_lower_clip[i] = 200;
            }

            /* Calculate fixed focal distance thresholds for 320x200 Mode 13h dimensions */
            float aspect_ratio = 320.0f / 200.0f;
            float fov = FOV_BASE * (aspect_ratio / (4.0f / 3.0f));
            float focal_length = (320.0f / 2.0f) / tanf(fov * (3.14159f / 180.0f) / 2.0f);

            /* Traverse the map BSP architecture arrays front-to-back to populate raw canvas blocks */
            float rad = g_player_angle * (3.14159f / 180.0f);
            render_bsp_node(ren, (uint16_t)g_root_node, rad, focal_length, 320, 200);

            /* Flush software pixels (320 columns pitch) into our streamable hardware target wrapper */
            SDL_UpdateTexture(g_framebuffer_texture, NULL, g_pixel_buffer, 320 * sizeof(uint32_t));
            SDL_RenderCopy(ren, g_framebuffer_texture, NULL, NULL);

            /* Draw the screen decay drop transition if melting state flags report active status */
            if (g_state == STATE_MELTING) {
                render_screen_melt(ren, 320, 200);
            }
        }

        // Swap double-buffered frame structures to paint the monitor active display panels
        SDL_RenderPresent(ren);

        // Enforce absolute classic 35Hz time clock constraints to separate physics simulation speeds from frame rates
        uint32_t frame_time = SDL_GetTicks() - frame_start;
        if (frame_time < FRAME_TIME_MS) {
            SDL_Delay(FRAME_TIME_MS - frame_time);
        }
    }

    // Tear down memory targets and exit libraries cleanly upon terminal signals
    destroy_video_subsystem();
    free_archive_cache();
    free(g_upper_clip);
    free(g_lower_clip);
    free(g_vertices);
    free(g_segs);
    free(g_sectors);
    free(g_sidedefs);
    free(g_linedefs);
    free(g_ssectors);
    free(g_nodes);
    
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();

    return 0;
}