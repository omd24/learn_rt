#pragma once

#include "headers/vec3.h"

typedef struct {
    point3 origin;
    vec3f dir;
} ray;

// returns: P(t) = A + tB
// A : origin, B: dir
inline point3
ray_at (ray * r, float t) {
    vec3f tB = vec3_scale(r->dir, t);
    point3 P = vec3_add(r->origin, tB);
    return P;
}

