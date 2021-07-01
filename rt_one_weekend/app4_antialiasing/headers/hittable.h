#pragma once

#include "vec3.h"
#include "ray.h"

typedef struct {
    point3 p;
    vec3f normal;
    float t;
    bool front_face;
} hit_record;

/* virtual functions in C */
struct HitVtbl;

typedef struct {
    struct HitVtbl * vptr;
} hittable;

/* hittable's virtual table */
struct HitVtbl {
    bool (*hit)(hittable * me, ray * r, float tmin, float tmax, hit_record * out_rec);

    /* additional virtual functions */
};

/* virtual function stub */
inline bool
hittable_virtual_hit (hittable * me, ray * r, float tmin, float tmax, hit_record * out_rec) {
    return me->vptr->hit(me, r, tmin, tmax, out_rec);
}

// TODO(omid): add hittable ctor to "hook" the vptr to the vtbl

inline void
record_set_normal (hit_record * rec, ray * r, vec3f outward_normal) {
    rec->front_face = vec3_mul_dot(r->dir, outward_normal) < 0.0f;
    rec->normal = rec->front_face ? outward_normal : vec3_scale(outward_normal, -1.0f);
}
