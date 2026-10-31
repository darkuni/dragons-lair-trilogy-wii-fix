/* MOC5 (Wii Mobiclip) container: parse and rebuild.
   Layout: "MOC5" u32 hdrsize, tagged chunks (2-char tag, u16 size in dwords) up to "HE",
   then packets: u32 tot, u32 vsize, video[vsize], audio..., packet stride = (tot+4)&~3.
   The bytes between tot and the stride end are the tail of the 400/480-byte audio chunk. */
#include "moc5.h"
#include <stdlib.h>
#include <string.h>

static uint32_t le32(const uint8_t *p){ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static uint16_t le16(const uint8_t *p){ return (uint16_t)(p[0]|(p[1]<<8)); }
static void wle32(uint8_t *p, uint32_t v){ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }

int moc5_parse(moc5 *m, const uint8_t *d, size_t len){
    memset(m,0,sizeof *m); m->d=d; m->len=len;
    if (len<16||memcmp(d,"MOC5",4)) return 0;
    m->hs=le32(d+4); if (8+(size_t)m->hs>len) return 0;
    size_t p=8; int have_tl=0,have_v2=0;
    while (p+4<=8+(size_t)m->hs){
        const uint8_t *t=d+p; uint32_t n=le16(t+2)*4u;
        if (p+4+n>len) return 0;
        if (!memcmp(t,"TL",2)&&n>=12){ m->fps_raw=le32(t+4); m->nframes=le32(t+8); m->max_frame=le32(t+12); have_tl=1; }
        else if (!memcmp(t,"V2",2)&&n>=8){ m->width=le32(t+4); m->height=le32(t+8); have_v2=1; }
        else if (!memcmp(t,"pc",2)) m->tag_pc=t+4;
        else if (!memcmp(t,"KI",2)){ m->tag_ki=t+4; m->ki_len=n; }
        else if (!memcmp(t,"HE",2)) break;
        p+=4+n;
    }
    if (!have_tl||!have_v2||m->nframes==0||m->nframes>100000) return 0;
    m->fr=(moc5_frame*)calloc(m->nframes,sizeof(moc5_frame));
    size_t q=8+m->hs;
    for (uint32_t i=0;i<m->nframes;i++){
        if (q+8>len){ free(m->fr); m->fr=NULL; return 0; }
        uint32_t tot=le32(d+q), vs=le32(d+q+4);
        if (tot<8+vs||q+tot>len){ free(m->fr); m->fr=NULL; return 0; }
        m->fr[i].pos=(uint32_t)q; m->fr[i].tot=tot; m->fr[i].vsize=vs;
        q+=(tot+4)&~3u;
    }
    m->end=q;
    return 1;
}
void moc5_free(moc5 *m){ free(m->fr); m->fr=NULL; }
const uint8_t *moc5_video(const moc5 *m, uint32_t i){ return m->d+m->fr[i].pos+8; }
int moc5_is_key(const moc5 *m, uint32_t i){ return (m->d[m->fr[i].pos+9]>>7)&1; }   /* first bit of the 16-bit-swapped stream */

uint8_t *moc5_build(const moc5 *m, uint32_t first, uint32_t n, uint8_t *const *vid, const uint32_t *vlen, size_t *outlen){
    size_t cap=m->len+64; for (uint32_t k=0;k<n;k++) cap+=vlen[k]+8;
    uint8_t *o=(uint8_t*)malloc(cap); if(!o) return NULL;
    size_t hdr=8+m->hs; memcpy(o,m->d,hdr);
    uint32_t *offs=(uint32_t*)malloc(sizeof(uint32_t)*m->nframes);
    size_t q=hdr;
    for (uint32_t i=0;i<m->nframes;i++){
        const moc5_frame *f=&m->fr[i];
        uint32_t stride=(f->tot+4)&~3u;
        const uint8_t *pk=m->d+f->pos;
        const uint8_t *audio=pk+8+f->vsize; uint32_t alen=f->tot-8-f->vsize; uint32_t tail=stride-f->tot;
        const uint8_t *v=pk+8; uint32_t vs=f->vsize;
        if (i>=first && i<first+n){ v=vid[i-first]; vs=vlen[i-first]; }
        uint32_t ntot=8+vs+alen;
        if (((ntot+4)&~3u)-ntot!=tail){ free(o); free(offs); return NULL; }  /* video size must keep 4-byte parity */
        offs[i]=(uint32_t)q;
        wle32(o+q,ntot); wle32(o+q+4,vs); memcpy(o+q+8,v,vs); memcpy(o+q+8+vs,audio,alen); memcpy(o+q+ntot,pk+f->tot,tail);
        q+=ntot+tail;
    }
    memcpy(o+q,m->d+m->end,m->len-m->end); q+=m->len-m->end;
    /* patch KI offsets (pairs: offset, frame) */
    if (m->tag_ki){
        uint8_t *ki=o+(m->tag_ki-m->d);
        for (uint32_t k=0;k+8<=m->ki_len;k+=8){ uint32_t fno=le32(ki+k+4); if (fno<m->nframes) wle32(ki+k,offs[fno]); }
    }
    free(offs); *outlen=q; return o;
}
