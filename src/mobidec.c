/*
 * MobiClip video decoder
 * Port of FFmpeg libavcodec/mobiclip.c to standalone C.
 * Copyright (c) 2015-2016 Florian Nouwt
 * Copyright (c) 2017 Adib Surani
 * Copyright (c) 2020 Paul B Mahol
 *
 * This file is free software; you can redistribute it and/or modify it under the terms of the
 * GNU Lesser General Public License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version. See LICENSE.
 */
#include "mobidec.h"
#include "mobitab.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* ---------------- bit reader over the 16-bit byte-swapped packet ---------------- */
typedef struct { const uint8_t *b; int size_bits; int pos; } gbits;
static unsigned show(gbits *g, int n){            /* n <= 25 */
    unsigned v=0; int p=g->pos;
    for (int i=0;i<n;i++,p++){ v<<=1; if (p<g->size_bits) v|=(g->b[p>>3]>>(7-(p&7)))&1; }
    return v;
}
static unsigned getb(gbits *g, int n){ unsigned v=show(g,n); g->pos+=n; return v; }
static int get1(gbits *g){ return (int)getb(g,1); }
static int left(gbits *g){ return g->size_bits-g->pos; }
static unsigned ue(gbits *g){ int z=0; while (z<32 && get1(g)==0) z++; if (z>=32) return 0xFFFFFFFFu; return (1u<<z)-1+getb(g,z); }
static int se(gbits *g){ unsigned v=ue(g); if (v==0xFFFFFFFFu) return 0; return (v&1)? (int)((v+1)>>1) : -(int)(v>>1); }

/* ---------------- VLC tables (built from code lengths, as ff_init_vlc_from_lengths) ---------------- */
typedef struct { int16_t sym; uint8_t len; } vlce;
static vlce rl_tab[2][1<<12];
static vlce mv_tab[2][16][1<<6];
static int tables_built;
static void build_vlc(vlce *t, int bits, int n, const uint8_t *lens, const void *syms, int symsize){
    uint64_t code=0;
    for (int i=0;i<(1<<bits);i++){ t[i].sym=-1; t[i].len=0; }
    for (int i=0;i<n;i++){
        int l=lens[i]; int sym = symsize==2 ? ((const uint16_t*)syms)[i] : ((const uint8_t*)syms)[i];
        unsigned c=(unsigned)(code>>(32-l));
        unsigned first=c<<(bits-l), cnt=1u<<(bits-l);
        for (unsigned k=0;k<cnt;k++){ t[first+k].sym=(int16_t)sym; t[first+k].len=(uint8_t)l; }
        code+=1ULL<<(32-l);
    }
}
static void init_tables(void){
    if (tables_built) return;
    build_vlc(rl_tab[0],12,104,bits0,syms0,2);
    build_vlc(rl_tab[1],12,104,bits0,syms1,2);
    for (int i=0;i<2;i++) for (int j=0;j<16;j++) build_vlc(mv_tab[i][j],6,mv_len[j],mv_bits[i][j],mv_syms[i][j],1);
    tables_built=1;
}
static int vlc_get(gbits *g, const vlce *t, int bits){
    vlce e=t[show(g,bits)]; if (e.len==0) return -1; g->pos+=e.len; return e.sym;
}

/* ---------------- decoder state ---------------- */
typedef struct { uint8_t *data[3]; int linesize[3]; } pic;
typedef struct { int x,y; } mxy;
struct mobidec {
    int width,height;
    pic pics[6]; uint8_t *mem[6];
    int current_pic, moflex, dct_tab_idx, quantizer;
    gbits gb;
    uint8_t *bitstream; int bitstream_size;
    int last_end_bits;
    int qtab[2][64]; uint8_t pre[32];
    mxy *motion; int motion_n;
};

mobidec *mobidec_new(int w, int h){
    if ((w&15)||(h&15)||w<=0||h<=0) return NULL;
    init_tables();
    mobidec *d=(mobidec*)calloc(1,sizeof *d); d->width=w; d->height=h;
    size_t fs=(size_t)w*h*3/2;
    for (int i=0;i<6;i++){
        d->mem[i]=(uint8_t*)calloc(1,fs);
        d->pics[i].data[0]=d->mem[i]; d->pics[i].data[1]=d->mem[i]+w*h; d->pics[i].data[2]=d->mem[i]+w*h+w*h/4;
        d->pics[i].linesize[0]=w; d->pics[i].linesize[1]=d->pics[i].linesize[2]=w/2;
    }
    d->motion_n=w/16+3; d->motion=(mxy*)calloc((size_t)d->motion_n,sizeof(mxy));
    return d;
}
void mobidec_free(mobidec *d){ if(!d)return; for(int i=0;i<6;i++)free(d->mem[i]); free(d->motion); free(d->bitstream); free(d); }

static int clipi(int v,int lo,int hi){ return v<lo?lo:v>hi?hi:v; }
static uint8_t clip8(int v){ return (uint8_t)(v<0?0:v>255?255:v); }

static int setup_qtables(mobidec *s, int64_t quantizer){
    if (quantizer<12||quantizer>161) return -1;
    s->quantizer=(int)quantizer;
    int qx=(int)(quantizer%6), qy=(int)(quantizer/6);
    for (int i=0;i<16;i++) s->qtab[0][i]=quant4x4_tab[qx][i]<<qy;
    for (int i=0;i<64;i++) s->qtab[1][i]=quant8x8_tab[qx][i]<<(qy-2);
    for (int i=0;i<20;i++) s->pre[i]=9;
    return 0;
}
static void inverse4(unsigned *rs){
    unsigned a=rs[0]+rs[2], b=rs[0]-rs[2], c=rs[1]+((int)rs[3]>>1), d=((int)rs[1]>>1)-rs[3];
    rs[0]=a+c; rs[1]=b+d; rs[2]=b-d; rs[3]=a-c;
}
static void idct(int *arr, int size){
    int e,f,g,h; unsigned x3,x2,x1,x0; int tmp[4];
    if (size==4){ inverse4((unsigned*)arr); return; }
    tmp[0]=arr[0]; tmp[1]=arr[2]; tmp[2]=arr[4]; tmp[3]=arr[6];
    inverse4((unsigned*)tmp);
    e=(int)((unsigned)arr[7]+arr[1]-arr[3]-(arr[3]>>1));
    f=(int)((unsigned)arr[7]-arr[1]+arr[5]+(arr[5]>>1));
    g=(int)((unsigned)arr[5]-arr[3]-arr[7]-(arr[7]>>1));
    h=(int)((unsigned)arr[5]+arr[3]+arr[1]+(arr[1]>>1));
    x3=(unsigned)g+(h>>2); x2=(unsigned)e+(f>>2); x1=(e>>2)-(unsigned)f; x0=(unsigned)h-(g>>2);
    arr[0]=(int)(tmp[0]+x0); arr[1]=(int)(tmp[1]+x1); arr[2]=(int)(tmp[2]+x2); arr[3]=(int)(tmp[3]+x3);
    arr[4]=(int)(tmp[3]-x3); arr[5]=(int)(tmp[2]-x2); arr[6]=(int)(tmp[1]-x1); arr[7]=(int)(tmp[0]-x0);
}
static void read_run_encoding(mobidec *s, int *last, int *run, int *level){
    int n=vlc_get(&s->gb,rl_tab[s->dct_tab_idx],12);
    if (n<0) n=0;
    *last=(n>>11)==1; *run=(n>>5)&0x3F; *level=n&0x1F;
}
static int add_coefficients(mobidec *s, pic *frame, int bx, int by, int size, int plane){
    gbits *gb=&s->gb; int mat[64]={0};
    const uint8_t *ztab = size==8 ? mobi_zigzag_direct : zigzag4x4_tab;
    const int *qtab=s->qtab[size==8];
    uint8_t *dst=frame->data[plane]+by*frame->linesize[plane]+bx;
    for (int pos=0; left(gb)>0; pos++){
        int qval,last,run,level;
        read_run_encoding(s,&last,&run,&level);
        if (level){ if (get1(gb)) level=-level; }
        else if (!get1(gb)){
            read_run_encoding(s,&last,&run,&level);
            level+=run_residue[s->dct_tab_idx][(last?64:0)+run];
            if (get1(gb)) level=-level;
        } else if (!get1(gb)){
            read_run_encoding(s,&last,&run,&level);
            run+=run_residue[s->dct_tab_idx][128+(last?64:0)+level];
            if (get1(gb)) level=-level;
        } else {
            last=get1(gb); run=(int)getb(gb,6);
            level=(int)getb(gb,12); if (level&0x800) level-=0x1000;
        }
        pos+=run;
        if (pos>=size*size) return -1;
        qval=qtab[pos];
        mat[ztab[pos]]=(int)(qval*(unsigned)level);
        if (last) break;
    }
    mat[0]+=32;
    for (int y=0;y<size;y++) idct(&mat[y*size],size);
    for (int y=0;y<size;y++){
        for (int x=y+1;x<size;x++){ int a=mat[x*size+y], b=mat[y*size+x]; mat[y*size+x]=a; mat[x*size+y]=b; }
        idct(&mat[y*size],size);
        for (int x=0;x<size;x++) dst[x]=clip8(dst[x]+(mat[y*size+x]>>6));
        dst+=frame->linesize[plane];
    }
    return 0;
}
static int add_pframe_coefficients(mobidec *s, pic *frame, int bx, int by, int size, int plane){
    int ret=0; unsigned idx=ue(&s->gb);
    if (idx==0) ret=add_coefficients(s,frame,bx,by,size,plane);
    else if (idx<16){
        int flags=pframe_block4x4_coefficients_tab[idx];
        for (int y=by;y<by+8;y+=4) for (int x=bx;x<bx+8;x+=4){
            if (flags&1){ ret=add_coefficients(s,frame,x,y,4,plane); if (ret<0) return ret; }
            flags>>=1;
        }
    } else ret=-1;
    return ret;
}
static int adjust(int x,int size){ return size==16 ? (x+1)>>1 : x; }

typedef struct { int w,h,ax,ay,x,y,size; uint8_t *block; int linesize; } bxy_t;
static uint8_t pget(bxy_t b){
    bxy_t r=b; int x,y;
    if (b.x==-1 && b.y>=b.size){ r.x=-1; r.y=b.size-1; }
    else if (b.x>=-1 && b.y>=-1){ r.x=b.x; r.y=b.y; }
    else if (b.x==-1 && b.y==-2){ r.x=0; r.y=-1; }
    else if (b.x==-2 && b.y==-1){ r.x=-1; r.y=0; }
    y=clipi(r.ay+r.y,0,r.h-1); x=clipi(r.ax+r.x,0,r.w-1);
    return r.block[y*r.linesize+x];
}
static uint8_t half(int a,int b){ return (uint8_t)(((a+b)+1)/2); }
static uint8_t half3(int a,int b,int c){ return (uint8_t)(((a+b+b+c)*2/4+1)/2); }
static uint8_t pick_above(bxy_t b){ b.y=b.y-1; return pget(b); }
static uint8_t pick_left(bxy_t b){ b.x=b.x-1; return pget(b); }
static uint8_t half_horz(bxy_t b){ bxy_t a=b,c=b; a.x-=1; c.x+=1; return half3(pget(a),pget(b),pget(c)); }
static uint8_t half_vert(bxy_t b){ bxy_t a=b,c=b; a.y-=1; c.y+=1; return half3(pget(a),pget(b),pget(c)); }
static uint8_t pick_4(bxy_t bxy){
    int val;
    if ((bxy.x%2)==0){ bxy_t ba=bxy,bb=bxy; int a,b; ba.x=-1; ba.y=bxy.y+bxy.x/2; a=pget(ba); bb.x=-1; bb.y=bxy.y+bxy.x/2+1; b=pget(bb); val=half(a,b); }
    else { bxy_t ba=bxy; ba.x=-1; ba.y=bxy.y+bxy.x/2+1; val=half_vert(ba); }
    return (uint8_t)val;
}
static uint8_t pick_5(bxy_t bxy){
    int val;
    if (bxy.x==0){ bxy_t a=bxy,b=bxy; a.x=-1; a.y-=1; b.x=-1; val=half(pget(a),pget(b)); }
    else if (bxy.y==0){ bxy_t a=bxy; a.x-=2; a.y-=1; val=half_horz(a); }
    else if (bxy.x==1){ bxy_t a=bxy; a.x-=2; a.y-=1; val=half_vert(a); }
    else { bxy_t a=bxy; a.x-=2; a.y-=1; val=pget(a); }
    return (uint8_t)val;
}
static uint8_t pick_6(bxy_t bxy){
    int val;
    if (bxy.y==0){ bxy_t a=bxy,b=bxy; a.x-=1; a.y=-1; b.y=-1; val=half(pget(a),pget(b)); }
    else if (bxy.x==0){ bxy_t a=bxy; a.x-=1; a.y-=2; val=half_vert(a); }
    else if (bxy.y==1){ bxy_t a=bxy; a.x-=1; a.y-=2; val=half_horz(a); }
    else { bxy_t a=bxy; a.x-=1; a.y-=2; val=pget(a); }
    return (uint8_t)val;
}
static uint8_t pick_7(bxy_t bxy){
    int clr,acc1,acc2; bxy_t a=bxy;
    a.x-=1; a.y-=1; clr=pget(a);
    if (bxy.x && bxy.y) return (uint8_t)clr;
    if (bxy.x==0){ a.x=-1; a.y=bxy.y; } else { a.x=bxy.x-2; a.y=-1; }
    acc1=pget(a);
    if (bxy.y==0){ a.x=bxy.x; a.y=-1; } else { a.x=-1; a.y=bxy.y-2; }
    acc2=pget(a);
    return half3(acc1,clr,acc2);
}
static uint8_t pick_8(bxy_t bxy){
    bxy_t ba=bxy,bb=bxy; int val;
    if (bxy.y==0){ int a,b; ba.y=-1; a=pget(ba); bb.x+=1; bb.y=-1; b=pget(bb); val=half(a,b); }
    else if (bxy.y==1){ ba.x+=1; ba.y-=2; val=half_horz(ba); }
    else if (bxy.x<bxy.size-1){ ba.x+=1; ba.y-=2; val=pget(ba); }
    else if (bxy.y%2==0){ int a,b; ba.x=bxy.y/2+bxy.size-1; ba.y=-1; a=pget(ba); bb.x=bxy.y/2+bxy.size; bb.y=-1; b=pget(bb); val=half(a,b); }
    else { ba.x=bxy.y/2+bxy.size; ba.y=-1; val=half_horz(ba); }
    return (uint8_t)val;
}
static void block_fill_simple(uint8_t *block,int size,int linesize,int fill){ for(int y=0;y<size;y++){ memset(block,fill,(size_t)size); block+=linesize; } }
static void block_fill(uint8_t *block,int size,int linesize,int w,int h,int ax,int ay,uint8_t(*pick)(bxy_t)){
    bxy_t b; b.size=size; b.block=block; b.linesize=linesize; b.w=w; b.h=h; b.ay=ay; b.ax=ax;
    for (int y=0;y<size;y++){ b.y=y; for (int x=0;x<size;x++){ b.x=x; uint8_t v=pick(b); block[ax+x+(ay+y)*linesize]=v; } }
}
static int block_sum(const uint8_t *block,int w,int h,int linesize){ int sum=0; for(int y=0;y<h;y++){ for(int x=0;x<w;x++) sum+=block[x]; block+=linesize; } return sum; }

static int predict_intra(mobidec *s, pic *frame, int ax, int ay, int pmode, int add_coeffs, int size, int plane){
    gbits *gb=&s->gb; int w=s->width>>!!plane, h=s->height>>!!plane, ret=0;
    uint8_t *data=frame->data[plane]; int ls=frame->linesize[plane];
    switch (pmode){
    case 0: block_fill(data,size,ls,w,h,ax,ay,pick_above); break;
    case 1: block_fill(data,size,ls,w,h,ax,ay,pick_left); break;
    case 2: {
        int arr1[16],arr2[16];
        uint8_t *top=data+(ay-1>0?ay-1:0)*ls+ax;
        uint8_t *lft=data+ay*ls+(ax-1>0?ax-1:0);
        int bottommost=data[(ay+size-1)*ls+(ax-1>0?ax-1:0)];
        int rightmost=data[(ay-1>0?ay-1:0)*ls+ax+size-1];
        int avg=(bottommost+rightmost+1)/2+2*clipi(se(gb),-(1<<16),1<<16);
        int r6=adjust(avg-bottommost,size), r9=adjust(avg-rightmost,size);
        int shift=adjust(size,size)==8?3:2;
        uint8_t *block;
        for (int x=0;x<size;x++){ int val=top[x]; arr1[x]=adjust(((bottommost-val)*(1<<shift))+r6*(x+1),size); }
        for (int y=0;y<size;y++){ int val=lft[y*ls]; arr2[y]=adjust(((rightmost-val)*(1<<shift))+r9*(y+1),size); }
        block=data+ay*ls+ax;
        for (int y=0;y<size;y++){
            for (int x=0;x<size;x++)
                block[x]=(uint8_t)((((top[x]+lft[0]+((arr1[x]*(y+1)+arr2[y]*(x+1))>>2*shift))+1)/2)&0xFF);
            block+=ls; lft+=ls;
        }
        break; }
    case 3: {
        uint8_t fill;
        if (ax==0&&ay==0) fill=0x80;
        else if (ax>=1&&ay>=1){
            int l=block_sum(data+ay*ls+ax-1,1,size,ls), t=block_sum(data+(ay-1)*ls+ax,size,1,ls);
            fill=(uint8_t)(((l+t)*2/(2*size)+1)/2);
        } else if (ax>=1) fill=(uint8_t)((block_sum(data+ay*ls+ax-1,1,size,ls)*2/size+1)/2);
        else if (ay>=1) fill=(uint8_t)((block_sum(data+(ay-1)*ls+ax,size,1,ls)*2/size+1)/2);
        else return -1;
        block_fill_simple(data+ay*ls+ax,size,ls,fill);
        break; }
    case 4: block_fill(data,size,ls,w,h,ax,ay,pick_4); break;
    case 5: block_fill(data,size,ls,w,h,ax,ay,pick_5); break;
    case 6: block_fill(data,size,ls,w,h,ax,ay,pick_6); break;
    case 7: block_fill(data,size,ls,w,h,ax,ay,pick_7); break;
    case 8: block_fill(data,size,ls,w,h,ax,ay,pick_8); break;
    }
    if (add_coeffs) ret=add_coefficients(s,frame,ax,ay,size,plane);
    return ret;
}
static int get_prediction(mobidec *s, int x, int y, int size){
    int index=(y&0xC)|(x/4%4);
    uint8_t val=s->pre[index]; uint8_t o = index%4==0 ? 9 : s->pre[index+3]; if (o<val) val=o;
    if (val==9) val=3;
    if (!get1(&s->gb)){ int xx=(int)getb(&s->gb,3); val=(uint8_t)(xx+(xx>=val?1:0)); }
    s->pre[index+4]=val;
    if (size==8) s->pre[index+5]=s->pre[index+8]=s->pre[index+9]=val;
    return val;
}
static int process_block(mobidec *s, pic *frame, int x, int y, int pmode, int has_coeffs, int plane){
    int ret;
    if (!has_coeffs){ if (pmode<0) pmode=get_prediction(s,x,y,8); return predict_intra(s,frame,x,y,pmode,0,8,plane); }
    unsigned tmp=ue(&s->gb);
    if (tmp>16) return -1;
    if (tmp==0){ if (pmode<0) pmode=get_prediction(s,x,y,8); ret=predict_intra(s,frame,x,y,pmode,1,8,plane); }
    else {
        int flags=block4x4_coefficients_tab[tmp-1]; ret=0;
        for (int by=y;by<y+8;by+=4) for (int bx=x;bx<x+8;bx+=4){
            int np=pmode; if (np<0) np=get_prediction(s,bx,by,4);
            ret=predict_intra(s,frame,bx,by,np,flags&1,4,plane); if (ret<0) return ret;
            flags>>=1;
        }
    }
    return ret;
}
static int decode_macroblock(mobidec *s, pic *frame, int x, int y, int predict){
    unsigned idx=ue(&s->gb); int flags,pmode_uv,ret=0;
    if (idx>=64) return -1;
    flags=block8x8_coefficients_tab[idx];
    if (predict){
        for (int k=0;k<4;k++){ ret=process_block(s,frame,x+(k&1)*8,y+(k>>1)*8,-1,flags&1,0); if (ret<0) return ret; flags>>=1; }
    } else {
        int pmode=(int)getb(&s->gb,3);
        if (pmode==2){ ret=predict_intra(s,frame,x,y,pmode,0,16,0); if (ret<0) return ret; pmode=9; }
        for (int k=0;k<4;k++){ ret=process_block(s,frame,x+(k&1)*8,y+(k>>1)*8,pmode,flags&1,0); if (ret<0) return ret; flags>>=1; }
    }
    pmode_uv=(int)getb(&s->gb,3);
    if (pmode_uv==2){
        ret=predict_intra(s,frame,x>>1,y>>1,pmode_uv,0,8,1+!s->moflex); if (ret<0) return ret;
        ret=predict_intra(s,frame,x>>1,y>>1,pmode_uv,0,8,2-!s->moflex); if (ret<0) return ret;
        pmode_uv=9;
    }
    ret=process_block(s,frame,x>>1,y>>1,pmode_uv,flags&1,1+!s->moflex); if (ret<0) return ret;
    flags>>=1;
    ret=process_block(s,frame,x>>1,y>>1,pmode_uv,flags&1,2-!s->moflex); if (ret<0) return ret;
    return 0;
}
static int get_index(int x){ return x==16?0 : x==8?1 : x==4?2 : x==2?3 : 0; }
static int mid_pred(int a,int b,int c){ if (a>b){ if (c>b){ if (c>a) b=a; else b=c; } } else { if (b>c){ if (c>a) b=c; else b=a; } } return b; }

static int predict_motion(mobidec *s, int width, int height, int index, int offsetm, int offsetx, int offsety){
    mxy *motion=s->motion; int fheight=s->height, fwidth=s->width;
    if (index<=5){
        int sidx=-(index>1?index:1)+s->current_pic; mxy mv=s->motion[0];
        if (sidx<0) sidx+=6;
        if (index>0){ mv.x=(int)(mv.x+(unsigned)se(&s->gb)); mv.y=(int)(mv.y+(unsigned)se(&s->gb)); }
        if (mv.x>=INT_MAX||mv.y>=INT_MAX) return -1;
        motion[offsetm]=mv;
        for (int i=0;i<3;i++){
            int method,sls,dls; uint8_t *src,*dst;
            if (i==1){ offsetx>>=1; offsety>>=1; mv.x>>=1; mv.y>>=1; width>>=1; height>>=1; fwidth>>=1; fheight>>=1; }
            method=(mv.x&1)|((mv.y&1)<<1);
            sls=s->pics[sidx].linesize[i]; dls=s->pics[s->current_pic].linesize[i];
            dst=s->pics[s->current_pic].data[i]+offsetx+offsety*dls;
            if (offsetx+(mv.x>>1)<0 || offsety+(mv.y>>1)<0 || offsetx+width+((mv.x+1)>>1)>fwidth || offsety+height+((mv.y+1)>>1)>fheight) return -1;
            src=s->pics[sidx].data[i]+offsetx+(mv.x>>1)+(offsety+(mv.y>>1))*sls;
            for (int y=0;y<height;y++){
                for (int x=0;x<width;x++){
                    switch (method){
                    case 0: dst[x]=src[x]; break;
                    case 1: dst[x]=(uint8_t)((src[x]>>1)+(src[x+1]>>1)); break;
                    case 2: dst[x]=(uint8_t)((src[x]>>1)+(src[x+sls]>>1)); break;
                    default: dst[x]=(uint8_t)((((src[x]>>1)+(src[x+1]>>1))>>1)+(((src[x+sls]>>1)+(src[x+1+sls]>>1))>>1));
                    }
                }
                dst+=dls; src+=sls;
            }
        }
    } else {
        int adjx = index==8 ? 0 : width/2, adjy = index==8 ? height/2 : 0;
        width-=adjx; height-=adjy;
        int tidx=get_index(height)*4+get_index(width);
        for (int i=0;i<2;i++){
            int idx2=vlc_get(&s->gb,mv_tab[s->moflex][tidx],6); if (idx2<0) return -1;
            int ret=predict_motion(s,width,height,idx2,offsetm,offsetx+i*adjx,offsety+i*adjy); if (ret<0) return ret;
        }
    }
    return 0;
}

int mobidec_decode(mobidec *s, const uint8_t *pkt, int size, const uint8_t **yuv){
    int ret;
    int bsz=(size+1)&~1;
    if (bsz>s->bitstream_size){ free(s->bitstream); s->bitstream=(uint8_t*)calloc((size_t)bsz+8,1); s->bitstream_size=bsz; }
    for (int i=0;i+1<bsz;i+=2){ s->bitstream[i]=i+1<size?pkt[i+1]:0; s->bitstream[i+1]=pkt[i]; }
    s->gb.b=s->bitstream; s->gb.size_bits=bsz*8; s->gb.pos=0;
    pic *frame=&s->pics[s->current_pic];
    if (get1(&s->gb)){
        s->moflex=get1(&s->gb); s->dct_tab_idx=get1(&s->gb);
        if (setup_qtables(s,getb(&s->gb,6))<0) return 0;
        for (int y=0;y<s->height;y+=16) for (int x=0;x<s->width;x+=16){
            ret=decode_macroblock(s,frame,x,y,get1(&s->gb)); if (ret<0) return 0;
        }
        s->last_end_bits=s->gb.pos;
    } else {
        mxy *motion=s->motion;
        memset(motion,0,sizeof(mxy)*(size_t)s->motion_n);
        s->dct_tab_idx=0;
        if (setup_qtables(s,s->quantizer+(int64_t)se(&s->gb))<0) return 0;
        for (int y=0;y<s->height;y+=16) for (int x=0;x<s->width;x+=16){
            motion[0].x=mid_pred(motion[x/16+1].x,motion[x/16+2].x,motion[x/16+3].x);
            motion[0].y=mid_pred(motion[x/16+1].y,motion[x/16+2].y,motion[x/16+3].y);
            motion[x/16+2].x=0; motion[x/16+2].y=0;
            int idx=vlc_get(&s->gb,mv_tab[s->moflex][0],6); if (idx<0) return 0;
            if (idx==6||idx==7){ ret=decode_macroblock(s,frame,x,y,idx==7); if (ret<0) return 0; }
            else {
                ret=predict_motion(s,16,16,idx,x/16+2,x,y); if (ret<0) return 0;
                unsigned idx2=ue(&s->gb); if (idx2>=64) return 0;
                int flags=pframe_block8x8_coefficients_tab[idx2];
                for (int sy=y;sy<y+16;sy+=8) for (int sx=x;sx<x+16;sx+=8){ if (flags&1) add_pframe_coefficients(s,frame,sx,sy,8,0); flags>>=1; }
                if (flags&1) add_pframe_coefficients(s,frame,x>>1,y>>1,8,1+!s->moflex);
                flags>>=1;
                if (flags&1) add_pframe_coefficients(s,frame,x>>1,y>>1,8,2-!s->moflex);
            }
        }
    }
    *yuv=s->mem[s->current_pic];
    s->current_pic=(s->current_pic+1)%6;
    return 1;
}

int mobidec_iframe_end_bits(const mobidec *d){ return d->last_end_bits; }
