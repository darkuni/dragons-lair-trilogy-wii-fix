/* MOC5 (Wii Mobiclip) container: parse and rebuild. */
#ifndef DLT_MOC5_H
#define DLT_MOC5_H
#include <stdint.h>
#include <stddef.h>

typedef struct { uint32_t pos, tot, vsize; } moc5_frame;   /* packet offset, size field, video size */

typedef struct {
    const uint8_t *d; size_t len;
    uint32_t hs;                 /* header size (after 8-byte magic+size) */
    uint32_t fps_raw, nframes, max_frame, width, height;
    const uint8_t *tag_pc, *tag_ki; uint32_t ki_len;
    moc5_frame *fr;              /* nframes entries */
    size_t end;                  /* offset after last frame */
} moc5;

int  moc5_parse(moc5 *m, const uint8_t *d, size_t len);
void moc5_free(moc5 *m);
int  moc5_is_key(const moc5 *m, uint32_t i);
const uint8_t *moc5_video(const moc5 *m, uint32_t i);
/* Rebuild: replace video payload of frames [first, first+n) with vid[k] (len vlen[k]).
   Audio bytes - including the 3-4 bytes after the size field, which belong to the audio chunk - and all
   other frames are kept; keyframe-index offsets are recomputed. Returns malloc'd buffer. */
uint8_t *moc5_build(const moc5 *m, uint32_t first, uint32_t n, uint8_t *const *vid, const uint32_t *vlen, size_t *outlen);
#endif
