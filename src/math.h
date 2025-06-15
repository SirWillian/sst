#ifndef INC_MATH_H
#define INC_MATH_H

#include "engineapi.h"

static inline float vec3f_dot(const struct vec3f v1, const struct vec3f v2) {
    return v1.x*v2.x + v1.y*v2.y + v1.z*v2.z;
}

static inline struct vec3f vec3f_transform(const struct vec3f v,
            const float matrix[3][4]) {
    return (struct vec3f){
        .x = vec3f_dot(v, *(struct vec3f *)&matrix[0]) + matrix[0][3],
        .y = vec3f_dot(v, *(struct vec3f *)&matrix[1]) + matrix[1][3],
        .z = vec3f_dot(v, *(struct vec3f *)&matrix[2]) + matrix[2][3],
    };
}

#endif
