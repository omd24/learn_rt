#pragma once

#include "vec3.h"
#include "ray.h"

struct hit_record;

/* virtual functions in C */
struct MatVtbl;

typedef struct material {
    struct MatVtbl * vptr;
} material;

/* material's virtual table */
struct MatVtbl {
    bool (*scatter)(material * me, ray * r_in, hit_record * rec, color * attenuation, ray * r_scatterd);

    /* additional virtual functions */
};

/* virtual function stub */
inline bool
material_scatter (struct material * me, ray * r_in, hit_record * rec, color * attenuation, ray * r_scatterd) {
    return me->vptr->scatter(me, r_in, rec, attenuation, r_scatterd);
}

//
//  Inhertance in C
//

//
// 1. lambertian material
typedef struct {
    material super;

    color albedo;

} lambertian;
//
// overriding virtual function
inline bool
lambertian_scatter (material * me, ray * r_in, hit_record * rec, color * attenuation, ray * r_scatterd) {
    bool ret = false;
    lambertian * lamb = (lambertian *)me;  /* explicit downcast */
    vec3f scatter_direction = vec3_add(rec->normal, random_unit_vector());
    // -- catch degenerate scatter direction
    if (vec3_near_zero(scatter_direction))
        scatter_direction = rec->normal;
    r_scatterd->origin = rec->p;
    r_scatterd->dir = scatter_direction;
    *attenuation = lamb->albedo;
    ret = true;
    return ret;
}
inline void
lambertian_init (lambertian * me, color a) {
    static struct MatVtbl vtbl = {  /* lambertian vtable */
        .scatter = lambertian_scatter
    };
    me->super.vptr = &vtbl;
    me->albedo = a;
}
//
// 2. metal material
typedef struct {
    material super;

    color albedo;
    float fuzziness;
} metal;
//
// overriding virtual function
inline bool
metal_scatter (material * me, ray * r_in, hit_record * rec, color * attenuation, ray * r_scatterd) {
    bool ret = false;
    metal * m = (metal *)me;  /* explicit downcast */
    vec3f reflected = vec3_reflect(vec3_normalize(r_in->dir), rec->normal);
    r_scatterd->origin = rec->p;
    r_scatterd->dir = vec3_add(reflected, vec3_scale(random_vec3_in_unit_sphere(), m->fuzziness));
    *attenuation = m->albedo;
    ret = (vec3_mul_dot(r_scatterd->dir, rec->normal) > 0);
    return ret;
}
inline void
metal_init (metal * me, color a, float fuzz) {
    static struct MatVtbl vtbl = {  /* metal vtable */
        .scatter = metal_scatter
    };
    me->super.vptr = &vtbl;
    me->albedo = a;
    me->fuzziness = fuzz < 1 ? fuzz : 1;
}
//
// 3. dielectric material
typedef struct {
    material super;

    float index_of_refraction;
} dielectric;

/* schlick approximation for reflectance */
inline float
dielectric_reflectance (float cosine, float refraction_ratio) {
    float ret;
    float r0 = (1.0f - refraction_ratio) / (1.0f + refraction_ratio);
    r0 = r0 * r0;
    ret = r0 + (1.0f - r0) * powf(1.0f - cosine, 5.0f);
    return ret;
}
//
// overriding virtual function
inline bool
dielectric_scatter (material * me, ray * r_in, hit_record * rec, color * attenuation, ray * r_scatterd) {
    bool ret = false;
    dielectric * diel = (dielectric *)me;  /* explicit downcast */
    *attenuation = (color) {1.0f,1.0f,1.0f};
    float refraction_ratio = rec->front_face ? (1.0f / diel->index_of_refraction) : diel->index_of_refraction;
    vec3f unit_direction = vec3_normalize(r_in->dir);

    float cos_theta = fminf(vec3_mul_dot(vec3_negate(unit_direction), rec->normal), 1.0f);
    float sin_theta = sqrtf(1.0f - cos_theta * cos_theta);

    bool cannot_refract = (refraction_ratio * sin_theta) > 1.0f;
    vec3f direction;
    if (cannot_refract || (dielectric_reflectance(cos_theta, refraction_ratio) > random_float()))
        direction = vec3_reflect(unit_direction, rec->normal);
    else
        direction = vec3_refract(unit_direction, rec->normal, refraction_ratio);

    r_scatterd->origin = rec->p;
    r_scatterd->dir = direction;
    ret = true;
    return ret;
}
inline void
dielectric_init (dielectric * me, float ir) {
    static struct MatVtbl vtbl = {  /* dielectric vtable */
        .scatter = dielectric_scatter
    };
    me->super.vptr = &vtbl;
    me->index_of_refraction = ir;
}
