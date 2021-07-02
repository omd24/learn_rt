#pragma once

#include <math.h>
#include <stdarg.h>

typedef union {
    struct {
        float x;
        float y;
        float z;
    };
    float E[3];
} vec3f;

// type aliases for vec3f
typedef vec3f point3;
typedef vec3f color;

inline vec3f
vec3_add (vec3f a, vec3f b) {
    vec3f ret;
    for (int i = 0; i < 3; ++i)
        ret.E[i] = a.E[i] + b.E[i];
    return ret;
}
inline vec3f
vec3_addvarg (int count, ...) {
    va_list arg_ptr;
    va_start(arg_ptr, count);
    vec3f ret = {0};
    for (int i = 0; i < count; ++i) {
        vec3f v = va_arg(arg_ptr, vec3f);
        ret = vec3_add(ret, v);
    }
    va_end(arg_ptr);
    return ret;
}
inline vec3f
vec3_sub (vec3f a, vec3f b) {
    vec3f ret;
    for (int i = 0; i < 3; ++i)
        ret.E[i] = a.E[i] - b.E[i];
    return ret;
}
inline vec3f
vec3_subvarg (int count, ...) {
    va_list arg_ptr;
    va_start(arg_ptr, count);
    vec3f ret = {0};
    for (int i = 0; i < count; ++i) {
        vec3f v = va_arg(arg_ptr, vec3f);
        if (0 == i)     // assign the first one
            ret = v;
        else            // subtract the rest
            ret = vec3_sub(ret, v);
    }
    va_end(arg_ptr);
    return ret;
}
inline vec3f
vec3_scale (vec3f const v, float const s) {
    vec3f ret;
    for (int i = 0; i < 3; ++i)
        ret.E[i] = v.E[i] * s;
    return ret;
}
inline float
vec3_mul_dot (vec3f const a, vec3f const b) {
    float ret = 0.0f;
    for (int i = 0; i < 3; ++i)
        ret += (a.E[i] * b.E[i]);
    return ret;
}
inline vec3f
vec3_mul_cross (vec3f const a, vec3f const b) {
    vec3f ret;
    ret.E[0] = a.E[1] * b.E[2] - a.E[2] * b.E[1];
    ret.E[1] = a.E[2] * b.E[0] - a.E[0] * b.E[2];
    ret.E[2] = a.E[0] * b.E[1] - a.E[1] * b.E[0];
    return ret;
}
inline float
vec3_len_squared (vec3f const v) {
    return vec3_mul_dot(v, v);
}
inline float
vec3_len (vec3f const v) {
    return sqrtf(vec3_mul_dot(v, v));
}
inline vec3f
vec3_normalize (vec3f v) {
    float s = 1.f / vec3_len(v);
    return vec3_scale(v, s);
}
inline vec3f
random_vec3 () {
    vec3f ret = {
        .x = random_float(),
        .y = random_float(),
        .z = random_float()
    };
    return ret;
}
inline vec3f
random_vec3_shifted (float min, float max) {
    vec3f ret = {
        .x = random_float_shifted(min, max),
        .y = random_float_shifted(min, max),
        .z = random_float_shifted(min, max)
    };
    return ret;
}
inline vec3f
random_vec3_in_unit_sphere () {
    vec3f ret;
    while (true) {
        ret = random_vec3_shifted(-1, 1);
        if (vec3_len_squared(ret) < 1.0f)
            break;
    }
    return ret;
}
inline vec3f
random_unit_vector () {
    return vec3_normalize(random_vec3_in_unit_sphere());
}
inline vec3f
random_in_hemisphere (vec3f normal) {
    vec3f ret = random_unit_vector();
    if (vec3_mul_dot(ret, normal) <= 0)     // if not in the same hemisphere
        ret = vec3_scale(ret, -1.0f);
    return ret;
}
