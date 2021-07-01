#pragma once

#include "vec3.h"
#include "ray.h"
#include "hittable.h"

typedef uint8_t byte;

typedef struct {
    int capacity;
    int size;
    hittable ** objects;

} hittable_list;


inline size_t
hlist_size (int capacity) {
    return sizeof(hittable_list) + (sizeof(hittable *) * capacity);
}
inline hittable_list *
hlist_init (byte * memory, int capacity) {
    hittable_list * ret;
    ret = (hittable_list *)memory;
    ret->objects = (hittable **)(memory + sizeof(hittable_list));
    ret->capacity = capacity;
    ret->size = 0;
    return ret;
}
inline void
hlist_add (hittable_list * hlist, hittable * object) {
    if (hlist->size < hlist->capacity)
        hlist->objects[hlist->size++] = object;
}
inline bool
hlist_hit (hittable_list * hlist, ray * r, float tmin, float tmax, hit_record * out_rec) {
    bool ret = false;
    float closets_so_far = tmax;
    hit_record temp_rec = {0};
    for (int i = 0; i < hlist->size; ++i) {
        if (hittable_virtual_hit(hlist->objects[i], r, tmin, closets_so_far, &temp_rec)) {
            ret = true;
            closets_so_far = temp_rec.t;
            *out_rec = temp_rec;
        }
    }
    return ret;
}
