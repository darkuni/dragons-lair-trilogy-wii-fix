#ifndef DLT_SEGMENTS_H
#define DLT_SEGMENTS_H
#include <stdint.h>
typedef struct { const char *name; int first, last, clamp, maxref; const char *intra; uint32_t size; const char *sha1; } dlt_segment;
extern const dlt_segment dlt_segments[];
extern const int dlt_nsegments;
extern const dlt_segment dlt_sa1_original;
#endif
