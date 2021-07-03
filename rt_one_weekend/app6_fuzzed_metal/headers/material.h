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

} metal;
//
// overriding virtual function
inline bool
metal_scatter (material * me, ray * r_in, hit_record * rec, color * attenuation, ray * r_scatterd) {
    bool ret = false;
    metal * m = (metal *)me;  /* explicit downcast */
    vec3f reflected = vec3_reflect(vec3_normalize(r_in->dir), rec->normal);
    r_scatterd->origin = rec->p;
    r_scatterd->dir = reflected;
    *attenuation = m->albedo;
    ret = (vec3_mul_dot(r_scatterd->dir, rec->normal) > 0);
    return ret;
}
inline void
metal_init (metal * me, color a) {
    static struct MatVtbl vtbl = {  /* metal vtable */
        .scatter = metal_scatter
    };
    me->super.vptr = &vtbl;
    me->albedo = a;
}
