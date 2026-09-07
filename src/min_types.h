/* min_types.h -- the vocabulary shared by both libraries and by the shaders.
 * Smallest file in the tree and the first one to read, because it is the reason
 * a struct can be declared once and mean the same thing on both sides of the
 * PCIe bus. No graphics or audio dependency; nothing here allocates or calls.
 *
 * ---- the one thing to understand ------------------------------------------
 *
 * This header is compiled TWICE, by two different compilers, from the same
 * bytes: once by the C compiler for the host, and once by glslangValidator for
 * the GPU (src/shaders/common.glsl defines VKMIN_GLSL and includes
 * render_shared.h, which includes this). The two branches below are that split.
 *
 * A record written with these spellings -- U32 not uint32_t, VKMIN_STRUCT not
 * `typedef struct` -- exists exactly once in the tree and cannot drift between
 * the two sides, which is what the C/GLSL transport contract in vkmin_gpu.h and
 * render_shared.h is built on. A record written with plain C spellings compiles
 * on the host, fails on the shader, and never reaches the contract at all.
 *
 * So the rule is: any struct the GPU reads is spelled in these names, and the
 * _Static_asserts beside those structs (host side only -- GLSL has no such
 * thing) are the only automated check that the two agree on size and layout.
 * Adding a name here means adding it to BOTH branches.
 *
 * Nothing in the GLSL branch declares vec2/vec3/vec4/uvec4/mat4: GLSL already
 * has them, with the same names and the same layout rules. The C branch has to
 * invent them, and its definitions are chosen to match what GLSL means by them
 * under the scalar block layout the shaders request.
 */
#ifndef MIN_TYPES_H
#define MIN_TYPES_H
#ifdef VKMIN_GLSL
/* GPU side. ADDR is a real 64-bit integer only because common.glsl requires
 * GL_EXT_shader_explicit_arithmetic_types_int64 first; it is what a
 * buffer_reference address is cast through. VKMIN_STRUCT expands to a bare
 * `struct name`, since GLSL has no typedefs and does not need one. */
#define U32 uint
#define I32 int
#define F32 float
#define ADDR uint64_t
#define VKMIN_STRUCT(name) struct name
#else
/* Host side. The fixed-width types are why the names are shouted: `uint` is not
 * a C type and `unsigned` is not a width, so a GPU record must say U32. */
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
/* The typedef the GLSL branch does not need, folded in so one declaration
 * serves both: `VKMIN_STRUCT(Foo) { ... };` yields a usable `Foo` in C and a
 * plain `struct Foo` in GLSL, with the braces and members written once. */
#define VKMIN_STRUCT(name) typedef struct name name; struct name
#endif

#endif
