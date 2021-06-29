/* ===========================================================
   #File: ray_sphere.c #
   #Date: 29 June 2021 #
   #Revision: 1.0 #
   #Creator: Omid Miresmaeili #
   #Description: ray sphere interesection #
   # First raytraced image #
   #Notice: (C) Copyright 2021 by Omid. All Rights Reserved. #
   =========================================================== */

#include "headers/ray.h"
#include "headers/vec3.h"

#include <stdio.h>
#include <stdbool.h>

// NOTE(omid): To output the result of the program to .ppm instead of console: 
// app2_ray_sphere.exe > image.ppm

//
// solves the eq: (t*t)B.B + 2tB.(A-C) + (A-C).(A-C) - r2 = 0
// ray: A + tB
// sphere: C (center), r (radius)
// discriminant: b*b - 
static bool
hit_sphere (point3 * center, float radius, ray * r) {
    vec3f oc = vec3_sub(r->origin, *center);
    float a = vec3_mul_dot(r->dir, r->dir);
    float b = 2.0f * vec3_mul_dot(oc, r->dir);
    float c = vec3_mul_dot(oc, oc) - radius * radius;
    float discriminant = b * b - 4 * a * c;
    return (discriminant > 0);
}
static color
ray_color (ray * r) {
    //
    // color red to mark ray-sphere intersection
    // center = (0,0,1), radius = 0.5
    if (hit_sphere(&(point3) { 0, 0, 1 }, .5f, r))
        return (color) { 1, 0, 0 };

    // 
    // bg: linearly blends white and blue depending on ray.y value (height)
    vec3f unit_dir = vec3_normalize(r->dir);
    float t = 0.5f * (unit_dir.y + 1.0f);

    color white = {1.0f, 1.0f, 1.0f};
    white = vec3_scale(white, (1.0f - t));
    color blue = {.5f, .7f, 1.0f};
    blue = vec3_scale(blue, t);

    return vec3_add(white, blue);
}
// translate [0.f, 1.f] to [0, 255]
static void
write_color (int out_color[3], color pixel_color) {
    for (int i = 0; i < 3; ++i)
        out_color[i] = (int)(255 * pixel_color.E[i]);
}
int main () {

    // -- image setup
    float aspect_ratio = 16.f / 9.f;
    int width = 400;
    int height = (int)(width / aspect_ratio);

    // -- camera setup
    float viewport_h = 2.f;
    float viewport_w = aspect_ratio * viewport_h;
    float focal_length = 1.f;   // camera distance from proj plane
    // NOTE(omid): Not to be confused with "focus distance" which is distance at which objects appear in perfect focus

    point3 origin = {0.f, 0.f, 0.f};
    vec3f horizontal = {viewport_w, 0.f, 0.f};
    vec3f half_horz = vec3_scale(horizontal, .5f);
    vec3f vertical = {0.f, viewport_h, 0.f};
    vec3f half_vert = vec3_scale(vertical, .5f);
    vec3f depth = {0.f, 0.f, focal_length};
    point3 lower_left_corner = vec3_subvarg(4, origin, half_horz, half_vert, depth);

    // -- render
    printf("P3\n%d %d\n255\n", width, height);
    for (int j = height - 1; j >= 0; --j) {
        fprintf(stderr, "\rScanlines remaining: %d", j);
        //fflush(stdout); // Will now print everything in the stdout buffer
        for (int i = 0; i < width; ++i) {
            float u = (float)i / (width - 1);
            float v = (float)j / (height - 1);
            vec3f horz_u = vec3_scale(horizontal, u);
            vec3f vert_v = vec3_scale(vertical, v);
            vec3f dir = vec3_addvarg(3, lower_left_corner, horz_u, vert_v);
            dir = vec3_sub(dir, origin);
            ray r = {
                .dir = dir,
                .origin = origin
            };
            color pixel_color = ray_color(&r);
            int icolor[3] = {0};
            write_color(icolor, pixel_color);
            printf("%d %d %d\n", icolor[0], icolor[1], icolor[2]);
        }
    }

    return(0);
}
