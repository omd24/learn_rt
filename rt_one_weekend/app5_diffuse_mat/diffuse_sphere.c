/* ===========================================================
   #File: diffuse_sphere.c #
   #Date: 02 July 2021 #
   #Revision: 1.0 #
   #Creator: Omid Miresmaeili #
   #Description: Rendering of diffuse spheres #
   #Notice: (C) Copyright 2021 by Omid. All Rights Reserved. #
   =========================================================== */

#include "headers/common.h"

// NOTE(omid): To output the result of the program to .ppm instead of console: 
// app3_normals_and_hittables.exe > image.ppm

//
// linearly blend color1 and color2 based on t parameter
static color
blend_lin (color c1, color c2, float t) {
    color ret;

    c1 = vec3_scale(c1, (1.0f - t));
    c2 = vec3_scale(c2, t);

    ret = vec3_add(c1, c2);
    return ret;
}
//
// compute ray color based on hitting an obj or not (bg)
// TODO(omid): make hittable_list inherit from hittable to use a hittable here
static color
ray_color (ray * r, hittable_list * hlist, int depth) {
    color ret = {0,0,0};
    hit_record rec;
    if (depth > 0) {
        if (hlist_hit(hlist, r, 0.001f /*Fixing Shadow Acne*/, g_infinity, &rec)) {
            point3 target = vec3_addvarg(3, rec.p, rec.normal, random_vec3_in_unit_sphere());
            ray child_ray = {.origin = rec.p, .dir = vec3_sub(target, rec.p)};
            color child_ray_color = ray_color(&child_ray, hlist, depth - 1);
            ret = vec3_scale(child_ray_color, 0.5f);
        } else {    // bg: blend white and blue based on ray.y
            vec3f unit_dir = vec3_normalize(r->dir);
            float wt = 0.5f * (unit_dir.y + 1.0f);
            color white = {1.0f, 1.0f, 1.0f};
            color blue = {.5f, .7f, 1.0f};
            ret = blend_lin(white, blue, wt);
        }
    }
    return ret;
}
//
// translate [0.f, 1.f] to [0, 255]
static void
write_color (int out_color[3], color pixel_color, int samples_per_pixel) {
    float scale = 1.0f / samples_per_pixel;
    pixel_color = vec3_scale(pixel_color, scale);
    /* gamma correction */
    // raising the color to the power of 1/gamma
    // (gamma = 2.0f)
    float r = sqrtf(pixel_color.x);
    float g = sqrtf(pixel_color.y);
    float b = sqrtf(pixel_color.z);
    for (int i = 0; i < 3; ++i)
        out_color[i] = (int)(256 * clamp(pixel_color.E[i], 0.0f, 0.999f));
}
int main () {

    // -- image setup
    float aspect_ratio = 16.f / 9.f;
    int width = 400;
    int height = (int)(width / aspect_ratio);
    int samples_per_pixel = 100;
    int max_depth = 50;

    // -- world setup
    int const world_cap = 10;
    byte * world_memory = malloc(hlist_size(world_cap));
    hittable_list * world = hlist_init(world_memory, world_cap);

    sphere s1 = {0};
    sphere s2 = {0};
    sphere_init(&s1, (point3) { 0.0f, 0.0f, -1.0f }, 0.5f);
    sphere_init(&s2, (point3) { 0.0f, -100.5f, -1.0f }, 100.0f);
    hlist_add(world, (hittable *)&s1);
    hlist_add(world, (hittable *)&s2);

    // -- camera setup
    float viewport_h = 2.f;
    float viewport_w = aspect_ratio * viewport_h;
    float focal_length = 1.f;   // camera distance from proj plane
    // NOTE(omid): Not to be confused with "focus distance" which is distance at which objects appear in perfect focus
    camera cam = {0};
    camera_init(&cam, viewport_w, viewport_h, focal_length);

    // -- render
    printf("P3\n%d %d\n255\n", width, height);
    for (int j = height - 1; j >= 0; --j) {
        fprintf(stderr, "\nScanlines remaining: %d", j);
        //fflush(stdout); // Will now print everything in the stdout buffer
        for (int i = 0; i < width; ++i) {
            color pixel_color = {0,0,0};
            for (int s = 0; s < samples_per_pixel; ++s) {
                float u = (float)(i + random_float()) / (width - 1);
                float v = (float)(j + +random_float()) / (height - 1);
                ray r = camera_cast_ray(&cam, u, v);
                pixel_color = vec3_add(pixel_color, ray_color(&r, world, max_depth));
            }
            int icolor[3] = {0};
            write_color(icolor, pixel_color, samples_per_pixel);
            printf("%d %d %d\n", icolor[0], icolor[1], icolor[2]);
        }
    }

    return(0);
}

