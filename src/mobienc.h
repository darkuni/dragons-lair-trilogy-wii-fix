/* MobiClip (Wii MOC5) video encoder. LGPL-2.1-or-later. */
#ifndef DLT_MOBIENC_H
#define DLT_MOBIENC_H
#include <stdint.h>
#include <stddef.h>
void mobienc_init(void);   /* call once before any (threaded) use */
/* Encode n YUV420p frames. types[i] is 'I' or 'P'; words[i] = chain word stored after each I-frame.
 * All frames use quantizer q. P-frames reference only frames since the last I-frame, at most maxref back.
 * intra_modes: allowed intra prediction modes, e.g. "013" (NULL = 0,1,3-7). Packets are malloc'd.
 * recon (optional): n*w*h*3/2 bytes receive the decoder-identical reconstruction. Returns 1 on success. */
int mobienc_encode(const uint8_t *frames, int n, int w, int h, const char *types, const uint32_t *words,
                   int q, int maxref, const char *intra_modes, uint8_t **pk, uint32_t *pklen, uint8_t *recon);
#endif
