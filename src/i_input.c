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

/* =========================================================================
 * ENGINE MATHEMATICAL UTILITIES
 * ========================================================================= */

/*
 * deg_to_rad
 * ----------
 * Simple scalar function converting standard degrees into circular radians.
 * Required because modern C standard math library functions (sinf, cosf) 
 * expect angular input values to be expressed in pure radians.
 */
float deg_to_rad(float deg) { 
    return deg * (PI / 180.0f); 
}

/*
 * normalize_angle
 * ---------------
 * Enforces a strict circular boundary constraint, locking our runtime angular 
 * tracking loops perfectly inside the standard [0.0, 360.0) degree spectrum.
 * This effectively prevents floating-point precision loss over long play sessions.
 */
float normalize_angle(float a) {
    while (a < 0.0f)   a += 360.0f;
    while (a >= 360.0f) a -= 360.0f;
    return a;
}

/*
 * clamp_int
 * ---------
 * Standard integer boundary limiter. Keeps array lookups or column indexing loops 
 * safely locked between a designated floor and ceiling threshold to prevent memory 
 * corruption or out-of-bounds array faults.
 */
int clamp_int(int val, int min, int max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

/*
 * clamp_float
 * -----------
 * Standard floating-point boundary limiter. Primarily used in our lighting routines 
 * and physics code to enforce strict caps on scaling values, friction decay, or depth shadows.
 */
float clamp_float(float val, float min, float max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

/* =========================================================================
 * PLAYER INTERACTION & SYSTEMS VECTOR TRIGGERS
 * ========================================================================= */

/*
 * trigger_use_key
 * ---------------
 * Projects a directional activation ray straight forward out of the player's 
 * camera coordinates based on their current viewing angle when the Use key ('E') is hit.
 *
 * How it works:
 *   Using standard trigonometry, we cast out a 64-unit vector string. Any structural line 
 *   or interactive switch intersecting this forward line can then be flipped or executed.
 */
void trigger_use_key(void) {
    float rad = deg_to_rad(g_player_angle);
    float reach = 64.0f; 
    
    /* Project our forward use vectors using authentic id Tech 1 geometry */
    float target_x = g_player_x + cosf(rad) * reach;
    float target_y = g_player_y + sinf(rad) * reach;

    printf("[VERNICHTUNG] USE Key ('E') activated! Casting ray: (%.1f, %.1f) -> Target vector: (%.1f, %.1f)\n",
           g_player_x, g_player_y, target_x, target_y);
}

/*
 * process_mouse_input
 * -------------------
 * Intercepts raw relative hardware movement deltas directly from the mouse sensor 
 * and recalculates our player's rotation speed based on a baseline sensitivity multiplier.
 *
 * Parameters:
 *   xrel - Relative pixel motion distance along the horizontal plane since the last clock tick.
 */
void process_mouse_input(int xrel) {
    /* Modify our global angle orientation directly, applying mouse look direction shifts */
    g_player_angle -= xrel * MOUSE_SENSITIVITY;
    
    /* Ensure the new resultant float wraps back neatly into safe degree boundaries */
    g_player_angle = normalize_angle(g_player_angle);
}

/*
 * calculate_movement_accel
 * ------------------------
 * Scans the active physical keyboard state and translates WASD key holds into clean, 
 * directional acceleration vectors based on the player's current view orientation angle.
 *
 * Parameters:
 *   keys           - Snapshot pointer of the current SDL keyboard state array.
 *   mouse_captured - Boolean check ensuring inputs are only read while the game screen is active.
 *   out_accel_x    - Pointer returning the resulting computed horizontal physics acceleration delta.
 *   out_accel_y    - Pointer returning the resulting computed vertical physics acceleration delta.
 */
void calculate_movement_accel(const uint8_t *keys, int mouse_captured, float *out_accel_x, float *out_accel_y) {
    *out_accel_x = 0.0f;
    *out_accel_y = 0.0f;

    /* Only process directional movement inputs if the mouse cursor is trapped inside our game viewport */
    if (mouse_captured) {
        float rad = deg_to_rad(g_player_angle);
        
        /* Authentic id Tech 1 coordinate projections */
        /* 0 degrees = East (Positive X), 90 degrees = North (Positive Y) */
        float forward_x =  cosf(rad);
        float forward_y =  sinf(rad);
        
        /* Perpendicular strafing vectors tracking the orientation flip */
        float right_x   =  sinf(rad);
        float right_y   = -cosf(rad);

        /* W Key: Move Forward */
        if (keys[SDL_SCANCODE_W]) { 
            *out_accel_x += forward_x * UNTERGANG_ACCEL; 
            *out_accel_y += forward_y * UNTERGANG_ACCEL; 
        }
        /* S Key: Move Backward */
        if (keys[SDL_SCANCODE_S]) { 
            *out_accel_x -= forward_x * UNTERGANG_ACCEL; 
            *out_accel_y -= forward_y * UNTERGANG_ACCEL; 
        }
        /* D Key: Strafe Right */
        if (keys[SDL_SCANCODE_D]) { 
            *out_accel_x += right_x   * UNTERGANG_ACCEL; 
            *out_accel_y += right_y   * UNTERGANG_ACCEL; 
        }
        /* A Key: Strafe Left */
        if (keys[SDL_SCANCODE_A]) { 
            *out_accel_x -= right_x   * UNTERGANG_ACCEL; 
            *out_accel_y -= right_y   * UNTERGANG_ACCEL; 
        }
    }
}