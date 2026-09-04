/* cube_data.h -- 36 vertices of a unit cube, six per face, counter-clockwise
 * seen from outside, with per-face colours and UVs. Plain data for 06_cube. */
#ifndef CUBE_DATA_H
#define CUBE_DATA_H
#include "shared.h"
#define F(x, y, z, c, u, v) {x, y, z, c, u, v, 0, 0}
static const ExVertex cube_verts[36] = {
    /* +Z */ F(-1,-1, 1,0xffffffffu,0,1), F( 1,-1, 1,0xffffffffu,1,1), F( 1, 1, 1,0xffffffffu,1,0),
             F(-1,-1, 1,0xffffffffu,0,1), F( 1, 1, 1,0xffffffffu,1,0), F(-1, 1, 1,0xffffffffu,0,0),
    /* -Z */ F( 1,-1,-1,0xffff80bfu,0,1), F(-1,-1,-1,0xffff80bfu,1,1), F(-1, 1,-1,0xffff80bfu,1,0),
             F( 1,-1,-1,0xffff80bfu,0,1), F(-1, 1,-1,0xffff80bfu,1,0), F( 1, 1,-1,0xffff80bfu,0,0),
    /* +X */ F( 1,-1, 1,0xff7373ffu,0,1), F( 1,-1,-1,0xff7373ffu,1,1), F( 1, 1,-1,0xff7373ffu,1,0),
             F( 1,-1, 1,0xff7373ffu,0,1), F( 1, 1,-1,0xff7373ffu,1,0), F( 1, 1, 1,0xff7373ffu,0,0),
    /* -X */ F(-1,-1,-1,0xff8cff73u,0,1), F(-1,-1, 1,0xff8cff73u,1,1), F(-1, 1, 1,0xff8cff73u,1,0),
             F(-1,-1,-1,0xff8cff73u,0,1), F(-1, 1, 1,0xff8cff73u,1,0), F(-1, 1,-1,0xff8cff73u,0,0),
    /* +Y */ F(-1, 1, 1,0xffff9980u,0,1), F( 1, 1, 1,0xffff9980u,1,1), F( 1, 1,-1,0xffff9980u,1,0),
             F(-1, 1, 1,0xffff9980u,0,1), F( 1, 1,-1,0xffff9980u,1,0), F(-1, 1,-1,0xffff9980u,0,0),
    /* -Y */ F(-1,-1,-1,0xff66e6ffu,0,1), F( 1,-1,-1,0xff66e6ffu,1,1), F( 1,-1, 1,0xff66e6ffu,1,0),
             F(-1,-1,-1,0xff66e6ffu,0,1), F( 1,-1, 1,0xff66e6ffu,1,0), F(-1,-1, 1,0xff66e6ffu,0,0),
};
#undef F
#endif
