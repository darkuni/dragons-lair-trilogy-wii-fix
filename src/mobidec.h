/* MobiClip video decoder - port of FFmpeg's libavcodec/mobiclip.c (LGPL-2.1-or-later). */
#ifndef DLT_MOBIDEC_H
#define DLT_MOBIDEC_H
#include <stdint.h>

typedef struct mobidec mobidec;
mobidec *mobidec_new(int width, int height);
void     mobidec_free(mobidec *d);
/* Decode one packet (raw MOC5 video payload). On success returns 1 and points *yuv at the
   decoded YUV420p frame (Y w*h, U, V (w/2)*(h/2)), valid until the next call. */
int      mobidec_decode(mobidec *d, const uint8_t *pkt, int size, const uint8_t **yuv);
/* bit position (in the 16-bit-swapped stream) where the last decoded I-frame's macroblock data ended */
int      mobidec_iframe_end_bits(const mobidec *d);
#endif
