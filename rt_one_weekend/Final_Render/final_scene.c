/* ===========================================================
   #File: final_scene.c #
   #Date: 05 July 2021 #
   #Revision: 1.0 #
   #Creator: Omid Miresmaeili #
   #Description: A final render wrapping up RT in one weekend (Singlethreaded) #
   #Notice: (C) Copyright 2021 by Omid. All Rights Reserved. #
   =========================================================== */

#include "headers/common.h"

// NOTE(omid): To output the result of the program to .ppm instead of console: 
// Final_Render.exe > image.ppm

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
ray_color (ray r, hittable_list * hlist, int depth) {
    color ret = {0,0,0};
    hit_record rec;
    if (depth > 0) {
        if (hlist_hit(hlist, &r, 0.001f /*Fixing Shadow Acne*/, g_infinity, &rec)) {
            ray scattered;
            color attenuation;
            if (material_scatter(rec.mat_ptr, &r, &rec, &attenuation, &scattered))
                ret = vec3_mul_elementwise(ray_color(scattered, hlist, depth - 1), attenuation);
        } else {    // bg: blend white and blue based on ray.y
            vec3f unit_dir = vec3_normalize(r.dir);
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

#define samples_per_pixel 500
hittable_list * g_world;

int main () {
    //
    // -- image setup
    float aspect_ratio = 16.f / 9.f;
    int width = 100;
    int height = (int)(width / aspect_ratio);
    int max_depth = 50;
    int * image_colors = calloc(height * width, 3 * sizeof(int));;

    //
    // -- g_world setup
    int const world_cap = 1000;
    byte * world_memory = malloc(hlist_size(world_cap));
    g_world = hlist_init(world_memory, world_cap);

    sphere s0 = {0};
    lambertian mat_ground;
    lambertian_init(&mat_ground, (color) { 0.5f, 0.5f, 0.5f });
    sphere_init(&s0, (point3) { 0.0f, -1000.0f, 0.0f }, 1000.0f, (material *)(&mat_ground));
    hlist_add(g_world, (hittable *)&s0);

    for (int a = -11; a < 11; ++a) {
        for (int b = -11; b < 11; ++b) {
            float choose_mat = random_float();
            point3 center = {a + 0.9f * random_float(), 0.2f, b + 0.9f * random_float()};
            if (vec3_len(vec3_sub(center, (point3) { 4.0f, 0.2f, 0.0f })) > 0.9f) {
                if (choose_mat < 0.8f) {
                    // diffuse
                    color albedo = vec3_mul_elementwise(random_vec3(), random_vec3());
                    lambertian * mat_sphere_ptr = malloc(sizeof(lambertian));           // hello mem leak :)
                    lambertian_init(mat_sphere_ptr, albedo);
                    sphere * s_ptr = malloc(sizeof(sphere));
                    sphere_init(s_ptr, center, 0.2f, (material *)(mat_sphere_ptr));
                    hlist_add(g_world, (hittable *)s_ptr);
                } else if (choose_mat < 0.95) {
                    // metal
                    color albedo = random_vec3_shifted(0.5f, 1.0f);
                    float fuzz = random_float(0.0f, 0.5f);
                    metal * mat_sphere_ptr = malloc(sizeof(metal));
                    metal_init(mat_sphere_ptr, albedo, fuzz);
                    sphere * s_ptr = malloc(sizeof(sphere));
                    sphere_init(s_ptr, center, 0.2f, (material *)(mat_sphere_ptr));
                    hlist_add(g_world, (hittable *)s_ptr);
                } else {
                    // dielectric
                    dielectric * mat_sphere_ptr = malloc(sizeof(dielectric));
                    dielectric_init(mat_sphere_ptr, 1.5f);
                    sphere * s_ptr = malloc(sizeof(sphere));
                    sphere_init(s_ptr, center, 0.2f, (material *)(mat_sphere_ptr));
                    hlist_add(g_world, (hittable *)s_ptr);
                }
            }
        }
    }

    sphere s1 = {0};
    dielectric mat1;
    dielectric_init(&mat1, 1.5f);
    sphere_init(&s1, (point3) { 0.f, 1.0f, 0.0f }, 1.0f, (material *)(&mat1));
    hlist_add(g_world, (hittable *)&s1);

    sphere s2 = {0};
    lambertian mat2;
    lambertian_init(&mat2, (color) { .4f, .2f, 0.1f });
    sphere_init(&s2, (point3) { -4.f, 1.0f, 0.0f }, 1.0f, (material *)(&mat2));
    hlist_add(g_world, (hittable *)&s2);

    sphere s3 = {0};
    metal mat3;
    metal_init(&mat3, (color) { .7f, .6f, 0.5f }, 0.0f);
    sphere_init(&s3, (point3) { 4.f, 1.0f, 0.0f }, 1.0f, (material *)(&mat3));
    hlist_add(g_world, (hittable *)&s3);

    //
    // -- camera setup
    camera cam = {0};
    point3 lookfrom = {13.f, 2.f, 3.f};
    point3 lookat = {0.f, 0.f, 0.f};
    vec3f vup = {0.f, 1.f, 0.f};
    float dist_to_focus = 10.f;
    float aperture = .1f;
    camera_init(
        &cam,
        lookfrom, lookat, vup,
        20.0f, aspect_ratio,
        aperture, dist_to_focus
    );

    //
    // -- render
    printf("P3\n%d %d\n255\n", width, height);

    int j;
    int k = 0;
    for (j = height - 1; j >= 0; --j) {
        fprintf(stderr, "\nScanlines remaining: %d\n", j);
        //fflush(stdout); // Will now print everything in the stdout buffer
        for (int i = 0; i < width; ++i) {
            fprintf(stderr, "\r\t\t%d", i);
            color pixel_color = {0,0,0};
            int s;

                for (s = 0; s < samples_per_pixel; ++s) {
                    float u = (float)(i + random_float()) / (width - 1);
                    float v = (float)(j + +random_float()) / (height - 1);
                    ray r = camera_cast_ray(&cam, u, v);
                    pixel_color = vec3_add(pixel_color, ray_color(r, g_world, max_depth));
                }

            write_color(&image_colors[k], pixel_color, samples_per_pixel);
            k += 3;
        }
    }

    //
    // -- output results
    k = 0;
    for (j = height - 1; j >= 0; --j) {
        fprintf(stderr, "\nScanlines remaining: %d", j);
        //fflush(stdout); // Will now print everything in the stdout buffer
        for (int i = 0; i < width; ++i) {
            printf("%d %d %d\n", image_colors[k], image_colors[k + 1], image_colors[k + 2]);
            k += 3;
        }
    }

    return(0);
}
