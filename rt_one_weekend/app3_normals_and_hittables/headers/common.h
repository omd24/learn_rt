#pragma once

#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <malloc.h>

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

//
// common headers
#include "vec3.h"
#include "ray.h"
#include "hittable.h"
#include "hittable_list.h"
#include "sphere.h"

