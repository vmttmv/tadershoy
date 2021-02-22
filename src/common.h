#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

#pragma pack(push, 1)
typedef union
{
    struct
    {
        float x;
        float y;
    };
    struct
    {
        float u;
        float v;
    };
} vec2;

typedef struct
{
    float x;
    float y;
    float w;
    float h;
} rect_t;
#pragma pack(pop)

#endif
