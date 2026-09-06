/* Common value types. No graphics or audio dependency. */
#ifndef MIN_TYPES_H
#define MIN_TYPES_H
#ifdef VKMIN_GLSL
#define U32 uint
#define I32 int
#define F32 float
#define ADDR uint64_t
#define VKMIN_STRUCT(name) struct name
#else
#include <stdint.h>
typedef uint32_t U32;
typedef int32_t I32;
typedef float F32;
typedef uint64_t ADDR;
typedef struct { float x, y; } vec2;
typedef struct { float x, y, z; } vec3; /* host only; never a GPU record member */
typedef struct { float x, y, z, w; } vec4;
typedef struct { uint32_t x, y, z, w; } uvec4;
typedef struct { float m[16]; } mat4; /* column-major, as GLSL */
#define VKMIN_STRUCT(name) typedef struct name name; struct name
#endif

#endif
