# vkmin -- C11 only, everywhere, including this build.
#
# Three libraries and one demo:
#   vkmin    the GPU layer, plus exactly one platform backend
#   render   the optional frame graph and overlay on top of vkmin
#   sndmin   the audio engine, which needs neither Vulkan nor a sound card
#   omega    the demo, which links vkmin and sndmin but not render
#
# Shaders live in src/shaders, compile to SPIR-V here and are embedded as
# uint32_t arrays in a generated shaders.h. There is no runtime shader
# compiler and so no file-path failure mode in the binary.

cmake_minimum_required(VERSION 3.20)
project(vkmin
    VERSION 0.5
    DESCRIPTION "A small Vulkan 1.4 renderer and an audio engine sharing one replay format"
    LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)          # -std=c11, not -std=gnu11
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)  # the make build's -O2 -g
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

option(VKMIN_HEADLESS "Build with no window system and no sound card" OFF)
option(VKMIN_SANITIZE "Address and UB sanitizers" OFF)
option(VKMIN_ANALYZER "Run the compiler's static analyser as part of the build" ON)
set(VKMIN_PLATFORM "auto" CACHE STRING "Platform backend: auto, glfw, sdl2, sdl3 or win32")
set_property(CACHE VKMIN_PLATFORM PROPERTY STRINGS auto glfw sdl2 sdl3 win32)

# ------------------------------------------------------------------ flags ---
# Maximum practical warning level, warnings as errors, static analysis in the
# normal build. There is deliberately no switch to turn -Werror off: if it is
# ever quietly disabled, defects accumulate from that moment on.
set(VKMIN_WARNINGS
    -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes
    -Wmissing-prototypes -Wold-style-definition -Wvla -Wpointer-arith -Wundef
    -Wformat=2 -Wdouble-promotion -Wwrite-strings -Wcast-align
    -Winit-self -Wredundant-decls)

add_library(vkmin_flags INTERFACE)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vkmin_flags INTERFACE ${VKMIN_WARNINGS})
    if(VKMIN_ANALYZER AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_compile_options(vkmin_flags INTERFACE -fanalyzer)
    endif()
else()
    message(WARNING "${CMAKE_C_COMPILER_ID}: the warning regime is GCC/Clang only "
                    "and is not being applied to this build")
endif()

# Determinism: the audio engine and the demo must not let the compiler contract
# or reassociate floating point, or a replay stops reproducing bit for bit.
set(VKMIN_FP_EXACT -ffp-contract=off)
set(SNDMIN_FP_EXACT -ffp-contract=off -fno-fast-math)

# Sanitizers go everywhere, third-party code included. Leak detection is off
# because the loader and GLFW own allocations that outlive us.
if(VKMIN_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# --------------------------------------------------------------- platform ---
# Exactly one backend links into a binary; the others are parallel
# implementations kept building so switching between them stays one word.
find_package(PkgConfig QUIET)

set(VKMIN_PLAT_RESOLVED "${VKMIN_PLATFORM}")
if(VKMIN_HEADLESS)
    set(VKMIN_PLAT_RESOLVED none)
elseif(VKMIN_PLATFORM STREQUAL auto)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(GLFW3 QUIET glfw3)
        pkg_check_modules(SDL3 QUIET sdl3)
        pkg_check_modules(SDL2 QUIET sdl2)
    endif()
    find_package(glfw3 CONFIG QUIET)          # the Windows binary drop ships no .pc
    if(GLFW3_FOUND OR glfw3_FOUND)
        set(VKMIN_PLAT_RESOLVED glfw)
    elseif(SDL3_FOUND)
        set(VKMIN_PLAT_RESOLVED sdl3)
    elseif(SDL2_FOUND)
        set(VKMIN_PLAT_RESOLVED sdl2)
    elseif(WIN32)
        set(VKMIN_PLAT_RESOLVED win32)        # nothing to find: the window is user32
    else()
        message(FATAL_ERROR "No platform backend found. Install GLFW, SDL2 or SDL3, "
                            "or configure with -DVKMIN_HEADLESS=ON.")
    endif()
endif()

add_library(vkmin_platform INTERFACE)
if(VKMIN_PLAT_RESOLVED STREQUAL none)
    set(VKMIN_PLAT_SOURCE "")
    target_compile_definitions(vkmin_platform INTERFACE VKMIN_NO_PLATFORM)
elseif(VKMIN_PLAT_RESOLVED STREQUAL win32)
    # The point of this one is that there is nothing to find: the window is
    # user32, which mingw and MSVC link by default, and XInput is loaded by
    # name at runtime.
    set(VKMIN_PLAT_SOURCE src/vkmin_plat_win32.c)
elseif(VKMIN_PLAT_RESOLVED STREQUAL glfw)
    set(VKMIN_PLAT_SOURCE src/vkmin_plat_glfw.c)
    if(NOT GLFW3_FOUND AND PKG_CONFIG_FOUND)
        pkg_check_modules(GLFW3 QUIET glfw3)
    endif()
    if(GLFW3_FOUND)
        target_include_directories(vkmin_platform SYSTEM INTERFACE ${GLFW3_INCLUDE_DIRS})
        target_link_directories(vkmin_platform INTERFACE ${GLFW3_LIBRARY_DIRS})
        target_link_libraries(vkmin_platform INTERFACE ${GLFW3_LIBRARIES})
    else()
        find_package(glfw3 CONFIG REQUIRED)
        target_link_libraries(vkmin_platform INTERFACE glfw)
    endif()
elseif(VKMIN_PLAT_RESOLVED STREQUAL sdl2)
    set(VKMIN_PLAT_SOURCE src/vkmin_plat_sdl2.c)
    if(NOT SDL2_FOUND)
        pkg_check_modules(SDL2 REQUIRED sdl2)
    endif()
    # -Dmain=SDL_main and -lSDL2main would hijack vkmin's own main(); the
    # backend defines SDL_MAIN_HANDLED and calls SDL_SetMainReady instead.
    set(sdl2_cflags ${SDL2_CFLAGS_OTHER})
    list(FILTER sdl2_cflags EXCLUDE REGEX "^-Dmain=")
    set(sdl2_libs ${SDL2_LIBRARIES})
    list(FILTER sdl2_libs EXCLUDE REGEX "^SDL2main$")
    target_compile_options(vkmin_platform INTERFACE ${sdl2_cflags})
    target_include_directories(vkmin_platform SYSTEM INTERFACE ${SDL2_INCLUDE_DIRS})
    target_link_directories(vkmin_platform INTERFACE ${SDL2_LIBRARY_DIRS})
    target_link_libraries(vkmin_platform INTERFACE ${sdl2_libs})
elseif(VKMIN_PLAT_RESOLVED STREQUAL sdl3)
    set(VKMIN_PLAT_SOURCE src/vkmin_plat_sdl3.c)
    if(NOT SDL3_FOUND)
        pkg_check_modules(SDL3 REQUIRED sdl3)
    endif()
    target_compile_options(vkmin_platform INTERFACE ${SDL3_CFLAGS_OTHER})
    target_include_directories(vkmin_platform SYSTEM INTERFACE ${SDL3_INCLUDE_DIRS})
    target_link_directories(vkmin_platform INTERFACE ${SDL3_LIBRARY_DIRS})
    target_link_libraries(vkmin_platform INTERFACE ${SDL3_LIBRARIES})
else()
    message(FATAL_ERROR "VKMIN_PLATFORM=${VKMIN_PLATFORM} is not a backend; "
                        "use auto, glfw, sdl2, sdl3 or win32")
endif()

# The SDL packages advertise -mwindows, which would detach the console vkmin
# prints its device and stats lines to. Only *_CFLAGS_OTHER and *_LIBRARIES are
# consumed above, so it never reaches the link line.
if(NOT WIN32 AND NOT VKMIN_HEADLESS)
    target_link_libraries(vkmin_platform INTERFACE pthread)
endif()

find_package(Vulkan REQUIRED)

# ---------------------------------------------------------------- shaders ---
set(VKMIN_SPV_DIR "${CMAKE_BINARY_DIR}/spv")
set(VKMIN_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${VKMIN_SPV_DIR}" "${VKMIN_GENERATED_DIR}")

file(GLOB VKMIN_SHADER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/*.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/*.frag"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/*.comp")
list(SORT VKMIN_SHADER_SOURCES)          # so the generated header is stable
if(NOT VKMIN_SHADER_SOURCES)
    message(FATAL_ERROR "No shaders found under src/shaders")
endif()

# Coarse but honest: any shared GLSL or transport header invalidates every
# module. Per-shader include tracking would need a depfile glslang does not
# reliably produce.
file(GLOB VKMIN_SHADER_DEPS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/*.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/lib/*.glsl")
list(APPEND VKMIN_SHADER_DEPS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/render_shared.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/vkmin_gpu.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/min_types.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/omega/omega_shared.h")

# SPIR-V 1.5 keeps discard as OpKill; newer compilers otherwise introduce the
# optional shaderDemoteToHelperInvocation feature without asking us.
set(VKMIN_GLSL_FLAGS
    -V --target-env vulkan1.3 --target-env spirv1.5
    "-I${CMAKE_CURRENT_SOURCE_DIR}/src"
    "-I${CMAKE_CURRENT_SOURCE_DIR}/src/shaders"
    "-I${CMAKE_CURRENT_SOURCE_DIR}/omega"
    "-P#extension GL_GOOGLE_include_directive : require")

foreach(shader IN LISTS VKMIN_SHADER_SOURCES)
    get_filename_component(shader_name "${shader}" NAME)
    set(shader_spv "${VKMIN_SPV_DIR}/${shader_name}.spv")
    add_custom_command(
        OUTPUT "${shader_spv}"
        COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} ${VKMIN_GLSL_FLAGS} "${shader}" -o "${shader_spv}"
        DEPENDS "${shader}" ${VKMIN_SHADER_DEPS}
        COMMENT "glslang ${shader_name}"
        VERBATIM)
    list(APPEND VKMIN_SPV_FILES "${shader_spv}")
endforeach()

set(VKMIN_SHADERS_H "${VKMIN_GENERATED_DIR}/shaders.h")
add_custom_command(
    OUTPUT "${VKMIN_SHADERS_H}"
    COMMAND ${CMAKE_COMMAND} "-DOUTPUT=${VKMIN_SHADERS_H}" "-DSPIRV=${VKMIN_SPV_FILES}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_spirv.cmake"
    DEPENDS ${VKMIN_SPV_FILES} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_spirv.cmake"
    COMMENT "embedding SPIR-V into shaders.h"
    VERBATIM)
add_custom_target(shaders DEPENDS "${VKMIN_SHADERS_H}")

get_filename_component(VKMIN_GLSLANG_DIR "${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}" DIRECTORY)
find_program(VKMIN_SPIRV_VAL NAMES spirv-val HINTS "${VKMIN_GLSLANG_DIR}")
if(VKMIN_SPIRV_VAL)
    # One invocation per module: spirv-val takes a single input and answers
    # "More than one input file specified" to anything else, so passing the
    # whole list made this target fail every time it was asked to run.
    set(VKMIN_VALIDATE_COMMANDS "")
    foreach(spv IN LISTS VKMIN_SPV_FILES)
        list(APPEND VKMIN_VALIDATE_COMMANDS
             COMMAND ${VKMIN_SPIRV_VAL} --target-env vulkan1.3 --scalar-block-layout "${spv}")
    endforeach()
    add_custom_target(validate-shaders
        ${VKMIN_VALIDATE_COMMANDS}
        DEPENDS ${VKMIN_SPV_FILES}
        COMMENT "spirv-val over every module"
        VERBATIM)
endif()

# ------------------------------------------------------------ third party ---
# Third-party code is quarantined in its own translation units and compiled
# without our warning set and without the analyser: it is not ours to fix, and
# mixing it into the main build is how -Werror gets disabled for everything.
add_library(vkmin_stb OBJECT src/vkmin_stb.c)
target_compile_options(vkmin_stb PRIVATE -w)
target_include_directories(vkmin_stb PRIVATE src)
target_include_directories(vkmin_stb SYSTEM PRIVATE src/third_party)

add_library(sndmin_io OBJECT src/sndmin_io.c)
target_compile_options(sndmin_io PRIVATE -w ${SNDMIN_FP_EXACT})
target_include_directories(sndmin_io PRIVATE src)
target_include_directories(sndmin_io SYSTEM PRIVATE src/third_party)

# ------------------------------------------------------------------ vkmin ---
add_library(vkmin STATIC
    src/vkmin.c
    src/vkmin_cvar.c
    ${VKMIN_PLAT_SOURCE}
    $<TARGET_OBJECTS:vkmin_stb>)
target_include_directories(vkmin PUBLIC src)
target_include_directories(vkmin SYSTEM PRIVATE src/third_party)
target_link_libraries(vkmin PRIVATE vkmin_flags)
target_link_libraries(vkmin PUBLIC vkmin_platform Vulkan::Vulkan)
if(NOT WIN32)
    target_link_libraries(vkmin PUBLIC m)
endif()

# ----------------------------------------------------------------- render ---
add_library(render STATIC
    src/render.c
    src/render_geometry.c
    src/render_ktx2.c
    src/render_scene.c)
target_include_directories(render PUBLIC src)
target_include_directories(render PRIVATE "${VKMIN_GENERATED_DIR}")
target_link_libraries(render PRIVATE vkmin_flags)
target_link_libraries(render PUBLIC vkmin)
add_dependencies(render shaders)

# ----------------------------------------------------------------- sndmin ---
add_library(sndmin STATIC
    src/sndmin.c
    src/sndmin_acoustics.c
    src/sndmin_output.c
    src/sndmin_song.c
    $<TARGET_OBJECTS:sndmin_io>)
target_include_directories(sndmin PUBLIC src)
target_include_directories(sndmin SYSTEM PRIVATE src/third_party)
target_compile_options(sndmin PRIVATE ${SNDMIN_FP_EXACT})
target_link_libraries(sndmin PRIVATE vkmin_flags)
if(NOT WIN32)
    target_link_libraries(sndmin PUBLIC m)
endif()

# The output backends are parallel implementations: null for headless runs and
# tests, miniaudio for a real sound card. Exactly one links into a binary.
add_library(sndmin_null STATIC src/sndmin_null.c)
target_include_directories(sndmin_null PUBLIC src)
target_compile_options(sndmin_null PRIVATE ${SNDMIN_FP_EXACT})
target_link_libraries(sndmin_null PRIVATE vkmin_flags)

add_library(sndmin_miniaudio STATIC src/sndmin_miniaudio.c)
target_compile_options(sndmin_miniaudio PRIVATE -w)   # miniaudio.h, quarantined
target_include_directories(sndmin_miniaudio PUBLIC src)
target_include_directories(sndmin_miniaudio SYSTEM PRIVATE src/third_party)
if(WIN32)
    target_link_libraries(sndmin_miniaudio PUBLIC ole32 uuid winmm avrt)
elseif(APPLE)
    target_link_libraries(sndmin_miniaudio PUBLIC
        "-framework CoreAudio" "-framework AudioToolbox" "-framework CoreFoundation" pthread)
else()
    target_link_libraries(sndmin_miniaudio PUBLIC dl pthread)
endif()

# ------------------------------------------------------------------ omega ---
if(VKMIN_HEADLESS)
    set(OMEGA_AUDIO sndmin_null)
else()
    set(OMEGA_AUDIO sndmin_miniaudio)
endif()

# ------------------------------------------------------------------ scene ---
# The render layer's only executable. Without it nothing links `render`, and
# the record/replay agreement -- the one automated check in the tree -- is
# blind to every line of it. gamekit.h beside it is the shared demo kit: it
# keeps its own gk_ prefix and its own malloc/exit policy, which is why it sits
# here and not in src/ with the four library owners.
add_executable(scene demo/scene.c)
target_include_directories(scene PRIVATE demo "${VKMIN_GENERATED_DIR}")
target_compile_options(scene PRIVATE ${VKMIN_FP_EXACT})
target_link_libraries(scene PRIVATE vkmin_flags render vkmin)

# ------------------------------------------------------------- unit tests ---
# The suballocator hands out ranges inside one device allocation, so a mistake
# there does not fault: two live resources overlap and quietly corrupt each
# other, and no validation layer can see it. It reads only offsets and sizes,
# never a Vulkan entry point, so this runs with no instance, no device and no
# driver -- the one check in the tree that needs no GPU at all.
add_executable(test_units tools/test_units.c)
target_include_directories(test_units PRIVATE src)
target_link_libraries(test_units PRIVATE vkmin_flags Vulkan::Vulkan)

# --------------------------------------------------------- sndmin unit test ---
# The audio half's pure maths and its song parser. sndmin links neither vkmin
# nor Vulkan, so this needs no GPU -- and because sndmin renders offline, no
# sound card either. sndmin_song.c had no caller at all before this.
add_executable(test_sndmin tools/test_sndmin.c)
target_link_libraries(test_sndmin PRIVATE vkmin_flags sndmin sndmin_null)

# ----------------------------------------------------------------- replay ---
# A journal replays into a different code path than the one that recorded it,
# but a replaying context is driven by vkmin_replay rather than the host's own
# loop -- so this is a separate program, not a flag on the demo. It needs only
# vkmin.h: the journal carries the resources, the shaders and the assets.
add_executable(replay tools/replay.c)
target_link_libraries(replay PRIVATE vkmin_flags vkmin)

# ------------------------------------------------------------------ omega ---
add_executable(omega omega/omega.c)
target_include_directories(omega PRIVATE omega "${VKMIN_GENERATED_DIR}")
target_compile_options(omega PRIVATE ${VKMIN_FP_EXACT})
target_link_libraries(omega PRIVATE vkmin_flags vkmin sndmin ${OMEGA_AUDIO})
add_dependencies(omega shaders)

# ---------------------------------------------------------------- summary ---
message(STATUS "vkmin ${PROJECT_VERSION}: ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}, "
               "${CMAKE_BUILD_TYPE}, backend ${VKMIN_PLAT_RESOLVED}, "
               "analyser ${VKMIN_ANALYZER}, sanitizers ${VKMIN_SANITIZE}")
