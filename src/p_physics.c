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
#include <math.h>
#include <SDL2/SDL.h>
#include "untergangtype.h"

/* 
 * External Function Hook from i_input.c
 * Used to fetch keyboard acceleration deltas without cluttering this module with SDL code.
 */
void calculate_movement_accel(const uint8_t *keys, int mouse_captured, float *out_accel_x, float *out_accel_y);

/* =========================================================================
 * BSP MAP SPATIAL QUERY FUNCTIONS
 * ========================================================================= */

/*
 * get_subsector_at_pos
 * ---------------------
 * Recursively walks the map's Binary Space Partitioning (BSP) tree down to a leaf 
 * subsector node to find out exactly which convex polygon contains the point (px, py).
 *
 * Parameters:
 *   px, py  - The 2D world coordinates to evaluate.
 *   node_id - The current tree node index. Start with the root node.
 */
uint16_t get_subsector_at_pos(float px, float py, uint16_t node_id) {
    /* If the highest bit is set, we have hit a leaf node (a subsector) instead of a divider node */
    if (node_id & NF_SUBSECTOR) {
        return node_id & ~NF_SUBSECTOR; /* Clear the flag bit to return the true raw index array position */
    }

    untergang_node_t *node = &g_nodes[node_id];

    /* 
     * Perform a 2D cross-product point-side evaluation relative to the partition line.
     * This mathematical test determines if our coordinate sits on the front or back side of the node line.
     */
    float check = (float)node->dx * (py - (float)node->y) - (float)node->dy * (px - (float)node->x);

    /* Index 0 is the front child, Index 1 is the back child */
    return get_subsector_at_pos(px, py, node->children[(check <= 0.0f) ? 0 : 1]);
}

/*
 * get_sector_at_pos
 * -----------------
 * Returns the structural 2.5D ceiling and floor sector properties underlying the 2D point (px, py).
 * It chains down to the BSP tree walker, pulls a segment inside that subsector, and grabs its parent sector.
 * Includes defensive boundaries to handle void space gracefully without crashing.
 */
untergang_sector_t* get_sector_at_pos(float px, float py) {
    uint16_t ssec_id = get_subsector_at_pos(px, py, (uint16_t)g_root_node);
    
    /* 1. Protect subsector lookup array index space using the global nodes allocation boundary counts */
    if (ssec_id >= (uint16_t)g_num_nodes * 2) return &g_sectors[0];

    untergang_subsector_t ssec = g_ssectors[ssec_id];
    
    /* 2. Check that the segment pointer indexing is within absolute safe geometric memory layout limits */
    if (ssec.first_seg == 0xFFFF) return &g_sectors[0];

    untergang_seg_t seg = g_segs[ssec.first_seg];
    if (seg.linedef >= g_num_linedefs) return &g_sectors[0];

    untergang_linedef_t line = g_linedefs[seg.linedef];
    uint16_t side_idx = line.sidenum[seg.side];
    
    /* 3. Ensure the sidedef index doesn't overshoot or clip to a missing portal structure */
    if (side_idx == 0xFFFF) return &g_sectors[0];
    
    return &g_sectors[g_sidedefs[side_idx].sector];
}

/* =========================================================================
 * PHYSICS & SLIDING WALL COLLISION LOGIC
 * ========================================================================= */

/*
 * collide_with_line
 * -----------------
 * Treats the player as a 2D bounding circle and a map wall as a line segment. 
 * If the circle intersects the line closer than its radius, this function mathematically 
 * pushes the target position outward along the line's normal vector, creating a smooth slide.
 */
void collide_with_line(float *target_x, float *target_y, float v1x, float v1y, float v2x, float v2y, float radius) {
    float line_dx = v2x - v1x;
    float line_dy = v2y - v1y;
    float len_sq = line_dx * line_dx + line_dy * line_dy;
    if (len_sq == 0.0f) return;

    /* Project the player's 2D position vector directly onto the infinite line segment */
    float t = ((*target_x - v1x) * line_dx + (*target_y - v1y) * line_dy) / len_sq;
    t = clamp_float(t, 0.0f, 1.0f); /* Clamp value to keep calculations bounded strictly to the segment limits */

    /* Find the closest point coordinate on the segment line to the player */
    float cx = v1x + t * line_dx;
    float cy = v1y + t * line_dy;
    
    /* Calculate distance vector from closest point to the player circle center */
    float dx = *target_x - cx;
    float dy = *target_y - cy;
    float dist = sqrtf(dx * dx + dy * dy);

    /* If distance is smaller than player size radius boundary, an intersection collision has occurred */
    if (dist < radius) {
        if (dist == 0.0f) dist = 0.001f; /* Prevent dividing by zero error splits */
        float overlap = radius - dist;
        
        /* Displace coordinates outward by the overlapping distance value amount */
        *target_x += (dx / dist) * overlap;
        *target_y += (dy / dist) * overlap;
    }
}

/*
 * check_wall_collisions
 * ---------------------
 * Iterates through all map linedef elements to run collision response passes. 
 * It filters for solid walls or 2-sided structural steps that are too steep to walk up.
 */
void check_wall_collisions(float *new_x, float *new_y, float current_floor_h) {
    /* Perform 3 sliding resolution passes to allow smooth sliding into diagonal structural corners */
    for (int pass = 0; pass < 3; pass++) {
        for (int i = 0; i < g_num_linedefs; i++) {
            untergang_linedef_t *line = &g_linedefs[i];
            
            /* Line flag 0x0001 marks a line as completely solid/impassable */
            int is_solid = (line->sidenum[1] == 0xFFFF) || (line->flags & 0x0001);

            /* Two-sided portal check: Evaluate step height variance thresholds */
            if (!is_solid && line->sidenum[1] != 0xFFFF) {
                untergang_sector_t *sec_a = &g_sectors[g_sidedefs[line->sidenum[0]].sector];
                untergang_sector_t *sec_b = &g_sectors[g_sidedefs[line->sidenum[1]].sector];
                
                /* Target height is whichever sector the player is not currently standing in */
                float target_floor = (sec_a->floor_h == (int16_t)current_floor_h) ? (float)sec_b->floor_h : (float)sec_a->floor_h;
                
                /* If height difference breaches maximum climb limit, treat portal step as a solid wall obstacle */
                if ((target_floor - current_floor_h) > MAX_STEP_HEIGHT) is_solid = 1;
            }

            if (!is_solid) continue;
            
            /* Fetch physical coordinate endpoints for the boundary segment */
            untergang_vertex_t v1 = g_vertices[line->v1];
            untergang_vertex_t v2 = g_vertices[line->v2];
            
            /* Resolve sliding vector corrections directly against coordinates */
            collide_with_line(new_x, new_y, (float)v1.x, (float)v1.y, (float)v2.x, (float)v2.y, PLAYER_RADIUS);
        }
    }
}

/* =========================================================================
 * TICK SIMULATION UPDATE LOOP
 * ========================================================================= */

/*
 * update_player_physics
 * ---------------------
 * Primary physics tick engine method. Updates velocity frames, handles momentum decay 
 * friction shifts, resolves gravity constraints, maps stair climbing transitions, 
 * and applies structural camera bob variables.
 */
void update_player_physics(const uint8_t *keys, int mouse_captured) {
    float accel_x = 0.0f;
    float accel_y = 0.0f;

    /* Fetch keyboard acceleration states processed inside i_input.c */
    calculate_movement_accel(keys, mouse_captured, &accel_x, &accel_y);

    /* Apply keyboard velocity push vectors directly into momentum buffers */
    g_player_momx += accel_x;
    g_player_momy += accel_y;

    /* Apply drag friction dampening scalars depending on grounded safety state status */
    if (g_is_grounded) {
        g_player_momx *= UNTERGANG_FRICTION;
        g_player_momy *= UNTERGANG_FRICTION;
    } else {
        g_player_momx *= 0.98f; /* Reduce friction sliding dampening while player drifts mid-air */
        g_player_momy *= 0.98f;
    }

    /* Staging position trackers for tentative next frame layout step evaluations */
    float target_x = g_player_x + g_player_momx;
    float target_y = g_player_y + g_player_momy;

    /* Run collision solvers against staging buffers to trim impassable line motions */
    untergang_sector_t *cur_sec = get_sector_at_pos(g_player_x, g_player_y);
    check_wall_collisions(&target_x, &target_y, (float)cur_sec->floor_h);

    /* Apply final resolved 2D position shifts to global engine variables */
    g_player_x = target_x;
    g_player_y = target_y;

    /* Update vertical position indices and apply environmental downward gravity acceleration drops */
    untergang_sector_t *new_sec = get_sector_at_pos(g_player_x, g_player_y);
    float floor_height = (float)new_sec->floor_h;

    if (!g_is_grounded) {
        g_player_momz -= GRAVITY; /* Accelerate vertical fall rate vectors downwards */
        g_player_target_z += g_player_momz;
        
        /* Floor impact boundary check limit */
        if (g_player_target_z <= floor_height + EYE_HEIGHT) {
            g_player_target_z = floor_height + EYE_HEIGHT;
            g_player_momz = 0.0f;
            g_is_grounded = 1; /* Reset ground status status */
        }
    } else {
        /* If player steps off a cliff ledge drop wider than maximum step values, trigger falling status */
        if (floor_height + EYE_HEIGHT < g_player_target_z - MAX_STEP_HEIGHT) {
            g_is_grounded = 0;
        } else {
            g_player_target_z = floor_height + EYE_HEIGHT; /* Snap directly to new step deck heights */
        }
    }

    /* Apply clean linear interpolation to smooth camera translation shifts when navigating jagged stairs */
    g_player_z += (g_player_target_z - g_player_z) * 0.35f;

    /* View weapon/camera stride bobbing math calculations */
    float speed = sqrtf(g_player_momx * g_player_momx + g_player_momy * g_player_momy);
    if (g_is_grounded && speed > 0.5f) {
        g_bob_phase += speed * 0.05f;
        g_player_z += sinf(g_bob_phase) * 2.0f; /* Alternate camera offset values vertically over a wave function */
    } else {
        g_bob_phase = 0.0f;}}