/* Wii disc image (plain ISO) access: game partition decryption, FST, and group rebuild. */
#include "wiidisc.h"
#include <stdlib.h>
#include <string.h>

/* Wii common keys (retail / Korean). These are published in many open-source tools (e.g. Dolphin). */
static const uint8_t COMMON_KEY[2][16] = {
    {0xeb,0xe4,0x2a,0x22,0x5e,0x85,0x93,0xe4,0x48,0xd9,0xc5,0x45,0x73,0x81,0xaa,0xf7},
    {0x63,0xb8,0x2b,0xb4,0xf4,0x61,0x4e,0x2e,0x13,0xf2,0xfe,0xfb,0xba,0x4c,0x9b,0x7e}};

static uint32_t be32(const uint8_t *p){ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }

int dlt_fseek(FILE *f, uint64_t off){
#ifdef _WIN32
    return _fseeki64(f,(__int64)off,SEEK_SET);
#else
    return fseeko(f,(off_t)off,SEEK_SET);
#endif
}
static int rd(FILE *f, uint64_t off, void *b, size_t n){ return dlt_fseek(f,off)==0 && fread(b,1,n,f)==n; }

int wii_read(wii_part *p, uint64_t off, void *buf, size_t n){
    uint8_t *o=(uint8_t*)buf;
    while (n){
        int64_t cl=(int64_t)(off/WII_CLUSTER_DATA); size_t co=(size_t)(off%WII_CLUSTER_DATA);
        if (cl!=p->cache_cl){
            uint8_t raw[WII_CLUSTER];
            if (!rd(p->f,p->data_off+(uint64_t)cl*WII_CLUSTER,raw,WII_CLUSTER)) return 0;
            aes128_cbc_decrypt(&p->key,raw+0x3D0,raw+0x400,p->cache,WII_CLUSTER_DATA);
            p->cache_cl=cl;
        }
        size_t take=WII_CLUSTER_DATA-co; if (take>n) take=n;
        memcpy(o,p->cache+co,take); o+=take; off+=take; n-=take;
    }
    return 1;
}

static int walk(wii_part *p, uint32_t s, uint32_t e, const char *prefix, uint32_t nent, const uint8_t *names, int *cap){
    uint32_t i=s;
    while (i<e){
        const uint8_t *en=p->fst+12*i; uint32_t w0=be32(en), a=be32(en+4), b=be32(en+8);
        const char *nm=(const char*)names+(w0&0xFFFFFF);
        char path[200]; snprintf(path,sizeof path,"%s%s",prefix,nm);
        if (w0>>24){
            char pre[200]; snprintf(pre,sizeof pre,"%s/",path);
            if (b>nent||b<=i) return 0;
            if (!walk(p,i+1,b,pre,nent,names,cap)) return 0;
            i=b;
        } else {
            if (p->nfiles==*cap){ *cap=*cap?*cap*2:1024; p->files=(wii_file*)realloc(p->files,sizeof(wii_file)*(size_t)*cap); }
            wii_file *wf=&p->files[p->nfiles++];
            snprintf(wf->path,sizeof wf->path,"%s",path); wf->off=(uint64_t)a<<2; wf->size=b; wf->entry=i;
            i++;
        }
    }
    return 1;
}

int wii_open(wii_part *p, const char *path, char *err, size_t errlen){
    memset(p,0,sizeof *p); p->cache_cl=-1;
    p->f=fopen(path,"rb");
    if (!p->f){ snprintf(err,errlen,"cannot open %s",path); return 0; }
    uint8_t hdr[0x20]; if (!rd(p->f,0,hdr,sizeof hdr)){ snprintf(err,errlen,"not a disc image (too small)"); return 0; }
    memcpy(p->game_id,hdr,6); p->game_id[6]=0;
    if (be32(hdr+0x18)!=0x5D1C9EA3){ snprintf(err,errlen,"not a Wii disc image (magic missing) - if this is RVZ/WBFS/NKit, convert it to a plain ISO first (Dolphin: right-click > Convert File)"); return 0; }
    uint8_t pt[32]; if (!rd(p->f,0x40000,pt,32)){ snprintf(err,errlen,"partition table unreadable"); return 0; }
    uint64_t game=0;
    for (int t=0;t<4 && !game;t++){
        uint32_t cnt=be32(pt+8*t); uint64_t toff=(uint64_t)be32(pt+8*t+4)<<2;
        for (uint32_t i=0;i<cnt && i<16;i++){
            uint8_t e[8]; if (!rd(p->f,toff+8*i,e,8)) break;
            if (be32(e+4)==0){ game=(uint64_t)be32(e)<<2; break; }
        }
    }
    if (!game){ snprintf(err,errlen,"no game partition found"); return 0; }
    p->part_off=game;
    uint8_t tik[0x2A4], ph[0x1C];
    if (!rd(p->f,game,tik,sizeof tik)||!rd(p->f,game+0x2A4,ph,sizeof ph)){ snprintf(err,errlen,"partition header unreadable"); return 0; }
    uint8_t iv[16]={0}, tk[16]; memcpy(iv,tik+0x1DC,8);
    int ki=tik[0x1F1]; if (ki>1) ki=0;
    aes128_ctx ck; aes128_init(&ck,COMMON_KEY[ki]); aes128_cbc_decrypt(&ck,iv,tik+0x1BF,tk,16);
    aes128_init(&p->key,tk);
    p->tmd_size=be32(ph); p->tmd_off=game+((uint64_t)be32(ph+4)<<2);
    p->h3_off=game+((uint64_t)be32(ph+0x10)<<2);
    p->data_off=game+((uint64_t)be32(ph+0x14)<<2); p->data_size=(uint64_t)be32(ph+0x18)<<2;
    uint8_t boot[0x440];
    if (!wii_read(p,0,boot,sizeof boot)){ snprintf(err,errlen,"cannot read partition boot block"); return 0; }
    if (memcmp(boot,p->game_id,4)!=0){ snprintf(err,errlen,"partition decryption failed (bad dump?)"); return 0; }
    p->fst_off=(uint64_t)be32(boot+0x424)<<2; p->fst_size=be32(boot+0x428)<<2;
    if (p->fst_size<12||p->fst_size>(8u<<20)){ snprintf(err,errlen,"bad FST size"); return 0; }
    p->fst=(uint8_t*)malloc(p->fst_size);
    if (!wii_read(p,p->fst_off,p->fst,p->fst_size)){ snprintf(err,errlen,"cannot read FST"); return 0; }
    uint32_t nent=be32(p->fst+8);
    if ((uint64_t)nent*12>p->fst_size){ snprintf(err,errlen,"bad FST"); return 0; }
    int cap=0;
    if (!walk(p,1,nent,"",nent,p->fst+12*nent,&cap)){ snprintf(err,errlen,"FST walk failed"); return 0; }
    return 1;
}
void wii_close(wii_part *p){ if(p->f)fclose(p->f); free(p->fst); free(p->files); memset(p,0,sizeof *p); }

const wii_file *wii_find(const wii_part *p, const char *path){
    for (int i=0;i<p->nfiles;i++) if (!strcmp(p->files[i].path,path)) return &p->files[i];
    return NULL;
}
int wii_read_file(wii_part *p, const wii_file *wf, uint8_t **out){
    *out=(uint8_t*)malloc(wf->size?wf->size:1);
    return *out && wii_read(p,wf->off,*out,wf->size);
}

int wii_read_group(wii_part *p, FILE *f, uint32_t g, uint8_t *hb, uint8_t *data){
    static const uint8_t zero[16]={0};
    uint8_t *raw=(uint8_t*)malloc(WII_GROUP_ENC); if(!raw) return 0;
    int ok=rd(f,p->data_off+(uint64_t)g*WII_GROUP_ENC,raw,WII_GROUP_ENC);
    if (ok) for (int c=0;c<WII_GROUP_CLUSTERS;c++){
        uint8_t *r=raw+c*WII_CLUSTER;
        aes128_cbc_decrypt(&p->key,zero,r,hb+c*0x400,0x400);
        aes128_cbc_decrypt(&p->key,r+0x3D0,r+0x400,data+c*WII_CLUSTER_DATA,WII_CLUSTER_DATA);
    }
    free(raw); return ok;
}

void wii_build_group(const aes128_ctx *key, uint8_t *hb, const uint8_t *data, uint8_t *out, uint8_t h2[0xA0]){
    static const uint8_t zero[16]={0};
    uint8_t h1[8][0xA0];
    for (int c=0;c<WII_GROUP_CLUSTERS;c++){
        const uint8_t *cd=data+c*WII_CLUSTER_DATA; uint8_t *b=hb+c*0x400;
        for (int i=0;i<31;i++) sha1(cd+i*0x400,0x400,b+i*20);           /* H0 */
        sha1(b,0x26C,h1[c/8]+(c%8)*20);                                  /* H1 entry */
    }
    for (int s=0;s<8;s++) sha1(h1[s],0xA0,h2+s*20);                      /* H2 */
    for (int c=0;c<WII_GROUP_CLUSTERS;c++){
        uint8_t *b=hb+c*0x400, *o=out+c*WII_CLUSTER;
        memcpy(b+0x280,h1[c/8],0xA0); memcpy(b+0x340,h2,0xA0);
        aes128_cbc_encrypt(key,zero,b,o,0x400);
        aes128_cbc_encrypt(key,o+0x3D0,data+c*WII_CLUSTER_DATA,o+0x400,WII_CLUSTER_DATA);
    }
}

int wii_fakesign_tmd(uint8_t *tmd, uint32_t tmd_size){
    if (tmd_size<0x1E4) return 0;
    memset(tmd+4,0,0x100);
    for (uint32_t i=0;i<0xFFFFFFFFu;i++){
        tmd[0x1C6]=(uint8_t)(i>>24); tmd[0x1C7]=(uint8_t)(i>>16); tmd[0x1C8]=(uint8_t)(i>>8); tmd[0x1C9]=(uint8_t)i;
        uint8_t d[20]; sha1(tmd+0x140,tmd_size-0x140,d);
        if (d[0]==0) return 1;
    }
    return 0;
}
