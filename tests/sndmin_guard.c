/* Linker interposition catches CRT allocation/IO/time anywhere reached by DSP,
 * including code outside sndmin.c. Unknown external calls are additionally
 * rejected by the callback call-graph audit. */
#include "sndmin_plat.h"
#include <stdlib.h>
#include <time.h>
void *__real_malloc(size_t);
void *__real_calloc(size_t,size_t);
void *__real_realloc(void *,size_t);
void __real_free(void *);
FILE *__real_fopen(const char *,const char *);
size_t __real_fread(void *,size_t,size_t,FILE *);
size_t __real_fwrite(const void *,size_t,size_t,FILE *);
int __real_fclose(FILE *);
clock_t __real_clock(void);
void *__wrap_malloc(size_t n);
void *__wrap_calloc(size_t n,size_t s);
void *__wrap_realloc(void *p,size_t n);
void __wrap_free(void *p);
FILE *__wrap_fopen(const char *p,const char *m);
size_t __wrap_fread(void *p,size_t s,size_t n,FILE *f);
size_t __wrap_fwrite(const void *p,size_t s,size_t n,FILE *f);
int __wrap_fclose(FILE *f);
clock_t __wrap_clock(void);
static void check(void) { if(sndmin_callback_active()) abort(); }
void *__wrap_malloc(size_t n) { check(); return __real_malloc(n); }
void *__wrap_calloc(size_t n,size_t s) { check(); return __real_calloc(n,s); }
void *__wrap_realloc(void *p,size_t n) { check(); return __real_realloc(p,n); }
void __wrap_free(void *p) { check(); __real_free(p); }
FILE *__wrap_fopen(const char *p,const char *m) { check(); return __real_fopen(p,m); }
size_t __wrap_fread(void *p,size_t s,size_t n,FILE *f) { check(); return __real_fread(p,s,n,f); }
size_t __wrap_fwrite(const void *p,size_t s,size_t n,FILE *f) { check(); return __real_fwrite(p,s,n,f); }
int __wrap_fclose(FILE *f) { check(); return __real_fclose(f); }
clock_t __wrap_clock(void) { check(); return __real_clock(); }
