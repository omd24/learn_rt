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
