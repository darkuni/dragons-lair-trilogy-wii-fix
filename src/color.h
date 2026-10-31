/* Energize color restoration: blue (ADA-graded) frame -> red, denoised, as YUV420p. */
#ifndef DLT_COLOR_H
#define DLT_COLOR_H
#include <stdint.h>
/* yuv in/out: YUV420p, w*h*3/2 bytes. clamp_hi: palette clamp upper hue in degrees (e.g. 25), or <0 for none. */
void color_init(void);   /* call once before threaded use */
void color_restore_frame(const uint8_t *yuv_in, uint8_t *yuv_out, int w, int h, int clamp_hi);
#endif
