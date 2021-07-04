#pragma once

#include "hittable.h"
#include "ray.h"

typedef struct {
    hittable super;

    point3 center;
    float radius;
    struct material * mat_ptr;
} sphere;

inline bool
sphere_hit (hittable * me, ray * r, float tmin, float tmax, hit_record * out_rec) {
    bool ret = false;
    sphere * s = (sphere *)me;  /* explicit downcast */
    vec3f oc = vec3_sub(r->origin, s->center);
    float a = vec3_len_squared(r->dir);
    float half_b = vec3_mul_dot(oc, r->dir);
    float c = vec3_len_squared(oc) - (s->radius * s->radius);
    float discriminant = half_b * half_b - a * c;
    float root = 0.0f;
    if (discriminant < 0.0f) {
        ret = false;     // sphere behind camera
    } else {
        float sqrt_delta = sqrtf(discriminant);
        // -- find the nearest root in acceptable range [tmin, tmax]
        root = (-half_b - sqrt_delta) / a;
        if (root < tmin || root > tmax) {           // unacceptable root
            root = (-half_b + sqrt_delta) / a;      // change to other root
            if (root < tmin || root > tmax)         // unacceptable root again
                ret = false;
            else
                ret = true;                         // acceptable root
        } else {
            ret = true;
        }
    }
    out_rec->t = root;
    out_rec->p = ray_at(r, root);
    vec3f outward_normal = vec3_scale(vec3_sub(out_rec->p, s->center), 1.0f / s->radius);
    record_set_normal(out_rec, r, outward_normal);
    out_rec->mat_ptr = s->mat_ptr;

    return ret;
}
inline void
sphere_init (sphere * me, point3 c, float r, struct material * mat) {
    static struct HitVtbl vtbl = {  /* sphere vtable */
        .hit = sphere_hit
    };
    me->super.vptr = &vtbl;
    me->center = c;
    me->radius = r;
    me->mat_ptr = mat;
}