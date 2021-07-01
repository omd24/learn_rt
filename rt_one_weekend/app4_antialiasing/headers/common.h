#pragma once

#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <malloc.h>
#include <stdlib.h>

//
// constants
static const float g_pi = 3.1415926535897932385f;
static const float g_infinity = INFINITY;

//
// utility functions
inline float
degrees_to_radians (float degree) {
    return (degree * g_pi / 180.0f);
}
/* returns a random float in [0,1) */
inline float
random_float () {
    // NOTE(omid): rand() returns a random integer in the range 0 and RAND_MAX
    // so rand() / RAND_MAX returns a number in [0,1]
    // and the following line returns a random float in [0,1)
    return rand() / (RAND_MAX + 1.0f);
}
/* returns a random float in [min,max) */
inline float
random_float_shifted (float min, float max) {
    return min + (min-max) * random_float();
}
inline float
clamp (float x, float min, float max) {
    return x < min ? min : ((x > max) ? max : x);
}


//
// common headers
#include "vec3.h"
#include "ray.h"
#include "hittable.h"
#include "hittable_list.h"
#include "sphere.h"
#include "camera.h"
