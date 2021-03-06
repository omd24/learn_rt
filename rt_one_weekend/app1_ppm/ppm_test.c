#include <stdio.h>

#include "headers/vec3.h"

// NOTE(omid): To output the result of the program to .ppm instead of console: 
// app1_ppm.exe > image.ppm

#if 0 // method #0

int main () {
    int width = 256;
    int height = 256;

    printf("P3\n%d %d\n255\n", width, height);
    for (int j = height - 1; j >= 0; --j) {
        fprintf(stderr, "\rScanlines remaining: %d", j);
        //fflush(stdout); // Will now print everything in the stdout buffer
        for (int i = 0; i < width; ++i) {
            float r = (float)i / (width - 1);
            float g = (float)j / (height - 1);
            float b = 0.25f;

            int ir = (int)(255 * r);
            int ig = (int)(255 * g);
            int ib = (int)(255 * b);

            printf("%d %d %d\n", ir, ig, ib);
        }
    }
}

#endif

#if 1 // method #1

// translate [0.f, 1.f] to [0, 255]
static void
write_color (int out_color[3], color pixel_color) {
    for (int i = 0; i < 3; ++i)
        out_color[i] = (int)(255 * pixel_color.E[i]);
}

int main () {
    int width = 256;
    int height = 256;

    printf("P3\n%d %d\n255\n", width, height);
    for (int j = height - 1; j >= 0; --j) {
        fprintf(stderr, "\rScanlines remaining: %d", j);
        //fflush(stdout); // Will now print everything in the stdout buffer
        for (int i = 0; i < width; ++i) {
            float r = (float)i / (width - 1);
            float g = (float)j / (height - 1);
            float b = 0.25f;

            color pixel_color = {r, g, b};
            int icolor[3] = {0};
            write_color(icolor, pixel_color);
            printf("%d %d %d\n", icolor[0], icolor[1], icolor[2]);
        }
    }
}


#endif // method #1


