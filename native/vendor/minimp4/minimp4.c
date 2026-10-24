/* Isolated bounded muxer: all allocation belongs to one operation/thread. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include <stdio.h>
typedef union { max_align_t alignment; struct { size_t size; } value; } Allocation;
static _Thread_local size_t allocated;
static _Thread_local int active;
int floggfy_mp4_begin(void) { if(active || allocated)return 0;active=1;return 1; }
void floggfy_mp4_end(void) { active=0; }
static void* bounded_malloc(size_t size) {
    const size_t budget=128u*1024u*1024u;
    Allocation* p;
    if(!active || size>budget-allocated || size>SIZE_MAX-sizeof(*p))return NULL;
    p=(Allocation*)malloc(sizeof(*p)+size);if(!p)return NULL;
    p->value.size=size;allocated+=size;return p+1;
}
static void bounded_free(void* ptr) {
    Allocation* p;if(!ptr)return;p=((Allocation*)ptr)-1;
    allocated-=p->value.size;free(p);
}
static void* bounded_realloc(void* ptr,size_t size) {
    Allocation *p,*next;size_t old;
    const size_t budget=128u*1024u*1024u;
    if(!ptr)return bounded_malloc(size);
    if(!size){bounded_free(ptr);return NULL;}
    p=((Allocation*)ptr)-1;old=p->value.size;
    if(!active || size>budget-(allocated-old) || size>SIZE_MAX-sizeof(*p))return NULL;
    next=(Allocation*)realloc(p,sizeof(*p)+size);if(!next)return NULL;
    next->value.size=size;allocated=allocated-old+size;return next+1;
}
#define malloc bounded_malloc
#define realloc bounded_realloc
#define free bounded_free
#define MINIMP4_IMPLEMENTATION
#define FLOGGFY_MINIMP4_MUX_ONLY
#include "minimp4.h"
