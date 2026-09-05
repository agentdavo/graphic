#ifndef SNDMIN_DEMO_H
#define SNDMIN_DEMO_H
#include "sndmin.h"
#include "jrnl.h"
#include <stdio.h>
static inline sndmin_ctx *demo_init(int argc,char **argv,FILE **journal,char *wav,char *png) {
    const char *base=argc>1?argv[1]:"tests/out/sndmin/demo";
    char path[512];
    if(snprintf(path,sizeof path,"%s.jrnl",base)>=(int)sizeof path||
        snprintf(wav,512,"%s.wav",base)>=512||snprintf(png,512,"%s.png",base)>=512) return NULL;
    *journal=jrnl_open(path,true); if(!*journal) return NULL;
    sndmin_ctx *c=sndmin_init(&(sndmin_desc){.offline=true,.journal=*journal});
    if(!c) { fclose(*journal); *journal=NULL; } return c;
}
static inline int demo_finish(sndmin_ctx *c,FILE *journal,uint32_t frames,const char *wav,const char *png) {
    const bool ok=sndmin_render(c,frames,wav,png); sndmin_dump(c,stdout); sndmin_shutdown(c);
    return fclose(journal)==0&&ok?0:1;
}
#endif
