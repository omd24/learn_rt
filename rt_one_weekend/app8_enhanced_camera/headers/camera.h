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
camera_init (
    camera * cam,
    point3 lookfrom,
    point3 lookat,
    vec3f vup,
    float vfov /* vertical fov in degrees*/,
    float aspect_ratio
) {
    float theta = degrees_to_radians(vfov);
    float h = tanf(theta / 2.0f);
    float viewport_h = 2.0f * h;
    float viewport_w = aspect_ratio * viewport_h;

    vec3f w = vec3_normalize(vec3_sub(lookfrom, lookat));
    vec3f u = vec3_normalize(vec3_mul_cross(vup, w));
    vec3f v = vec3_mul_cross(w, u);

    cam->origin = lookfrom;
    cam->horizontal = vec3_scale(u, viewport_w);
    cam->vertical = vec3_scale(v, viewport_h);
    vec3f half_horz = vec3_scale(cam->horizontal, 0.5f);
    vec3f half_verz = vec3_scale(cam->vertical, 0.5f);
    cam->lower_left_corner = vec3_subvarg(4, cam->origin, half_horz, half_verz, w);
}
inline ray
camera_cast_ray (camera * cam, float s, float t) {
    vec3f horz_s = vec3_scale(cam->horizontal, s);
    vec3f vert_t = vec3_scale(cam->vertical, t);
    vec3f dir = vec3_addvarg(3, cam->lower_left_corner, horz_s, vert_t);
    dir = vec3_sub(dir, cam->origin);
    ray r = {.origin = cam->origin, .dir = dir};
    return r;
}

