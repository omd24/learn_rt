#pragma once

#include "vec3.h"
#include "ray.h"

typedef struct {
    point3 origin;
    point3 lower_left_corner;
    vec3f horizontal;
    vec3f vertical;
} camera;

inline void
camera_init (camera * cam, float viewport_w, float viewport_h, float focal_len) {
    cam->origin = (point3) {0, 0, 0};
    cam->horizontal = (vec3f) {viewport_w, 0, 0};
    cam->vertical = (vec3f) {0, viewport_h, 0};
    vec3f half_horz = vec3_scale(cam->horizontal, 0.5f);
    vec3f half_verz = vec3_scale(cam->vertical, 0.5f);
    vec3f depth = {0.f, 0.f, focal_len};
    cam->lower_left_corner = vec3_subvarg(4, cam->origin, half_horz, half_verz, depth);
}
inline ray
camera_cast_ray (camera * cam, float u, float v) {
    vec3f horz_u = vec3_scale(cam->horizontal, u);
    vec3f vert_v = vec3_scale(cam->vertical, v);
    vec3f dir = vec3_addvarg(3, cam->lower_left_corner, horz_u, vert_v);
    dir = vec3_sub(dir, cam->origin);
    ray r = {.dir = dir, .origin = cam->origin};
    return r;
}

