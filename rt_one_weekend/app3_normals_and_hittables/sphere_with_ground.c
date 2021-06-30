/* ===========================================================
   #File: sphere_with_ground.c #
   #Date: 30 June 2021 #
   #Revision: 1.0 #
   #Creator: Omid Miresmaeili #
   #Description: A normals-colored sphere with ground #
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
ray_color (ray * r, hittable_list * hlist) {
    color ret;
    hit_record rec;
    if (hlist_hit(hlist, r, 0, g_infinity, &rec)) {
        color normals_colored = vec3_add(rec.normal, (color){1,1,1});
        ret = vec3_scale(normals_colored, 0.5f);
    } else {            // bg: blend white and blue based on ray.y
        vec3f unit_dir = vec3_normalize(r->dir);
        float wt = 0.5f * (unit_dir.y + 1.0f);
        color white = {1.0f, 1.0f, 1.0f};
        color blue = {.5f, .7f, 1.0f};
        ret = blend_lin(white, blue, wt);
    }
    return ret;
}
//
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

    // -- world setup
    int const world_cap = 10;
    byte * world_memory = malloc(hlist_size(world_cap));
    hittable_list * world = hlist_init(world_memory, world_cap);

    sphere s1 = {0};
    sphere s2 = {0};
    sphere_init(&s1, (point3){0.0f, 0.0f, -1.0f}, 0.5f);
    sphere_init(&s2, (point3){0.0f, -100.5f, -1.0f}, 100.0f);
    hlist_add(world, (hittable *)&s1);
    hlist_add(world, (hittable *)&s2);

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
            color pixel_color = ray_color(&r, world);
            int icolor[3] = {0};
            write_color(icolor, pixel_color);
            printf("%d %d %d\n", icolor[0], icolor[1], icolor[2]);
        }
    }

    return(0);
}

