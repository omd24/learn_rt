#pragma once

#include "vec3.h"
#include "ray.h"

typedef struct {
    point3 origin;
    point3 lower_left_corner;
    vec3f horizontal;
    vec3f vertical;
    vec3f u, v, w;      /* camera space bases */
    float lens_radius;
} camera;

inline void
camera_init (
    camera * cam,
    point3 lookfrom,
    point3 lookat,
    vec3f vup,
    float vfov /* vertical fov in degrees*/,
    float aspect_ratio,
    float aperture,
    float focus_dist
) {
    float theta = degrees_to_radians(vfov);
    float h = tanf(theta / 2.0f);
    float viewport_h = 2.0f * h;
    float viewport_w = aspect_ratio * viewport_h;

    cam->w = vec3_normalize(vec3_sub(lookfrom, lookat));
    cam->u = vec3_normalize(vec3_mul_cross(vup, cam->w));
    cam->v = vec3_mul_cross(cam->w, cam->u);

    cam->origin = lookfrom;
    cam->horizontal = vec3_scale(cam->u, viewport_w * focus_dist);
    cam->vertical = vec3_scale(cam->v, viewport_h * focus_dist);
    vec3f half_horz = vec3_scale(cam->horizontal, 0.5f);
    vec3f half_verz = vec3_scale(cam->vertical, 0.5f);
    vec3f depth = vec3_scale(cam->w, focus_dist);
    cam->lower_left_corner = vec3_subvarg(4, cam->origin, half_horz, half_verz, depth);

    cam->lens_radius = aperture / 2.0f;
}
inline ray
camera_cast_ray (camera * cam, float s, float t) {
    ray ret;
    vec3f rd = vec3_scale(random_in_unit_disk(), cam->lens_radius);
    vec3f offset = vec3_add(vec3_scale(cam->u, rd.x), vec3_scale(cam->v, rd.y));

    vec3f horz_s = vec3_scale(cam->horizontal, s);
    vec3f vert_t = vec3_scale(cam->vertical, t);
    vec3f dir = vec3_addvarg(5, cam->lower_left_corner, horz_s, vert_t, vec3_negate(cam->origin), vec3_negate(offset));

    ret.origin = vec3_add(cam->origin, offset);
    ret.dir = dir;
    return ret;
}

