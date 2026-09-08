/* Finite window cycle for external resize/minimize/restore tests. */
#include "vkmin.h"
int main(int argc, char **argv) {
    vkmin_ctx *c=vkmin_init(&(vkmin_desc){.argc=argc,.argv=argv,.title="vkmin resize fixture",.width=160,.height=120,.vsync=true});
    fprintf(stderr,"window fixture ready\n");
    while(vkmin_running(c)) {
        (void)vkmin_frame_begin(c,&(vkmin_clear){0.25f,0.5f,0.75f,1.0f});
        vkmin_frame_end(c);
    }
    vkmin_shutdown(c);
    return 0;
}
