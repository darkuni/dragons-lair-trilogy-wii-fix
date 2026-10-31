/* MobiClip (Wii MOC5, moflex=1) video encoder for dltfix.
 * Prediction, motion compensation and reconstruction mirror FFmpeg's libavcodec/mobiclip.c decoder
 * (Copyright (c) 2015-2016 Florian Nouwt, (c) 2017 Adib Surani, (c) 2020 Paul B Mahol) so that the
 * encoder's reconstruction is bit-identical to the decoder output. LGPL-2.1-or-later (see LICENSE).
 * Determinism: no libm transcendental functions; only + - * / and sqrt (IEEE-exact) are used. */
#include "mobienc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <stdint.h>
#ifdef _MSC_VER
#define TLS __declspec(thread)
#else
#define TLS __thread
#endif

static const uint8_t zigzag_direct[64]={0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63};
static const uint8_t quant8x8_tab[][64]={
{20,19,19,25,18,25,19,24,24,19,20,18,32,18,20,19,19,24,24,19,19,25,18,25,18,25,18,25,19,24,24,19,19,24,24,19,18,32,18,20,18,32,18,24,24,19,19,24,24,18,25,18,25,18,19,24,24,19,18,32,18,24,24,18},
{22,21,21,28,19,28,21,26,26,21,22,19,35,19,22,21,21,26,26,21,21,28,19,28,19,28,19,28,21,26,26,21,21,26,26,21,19,35,19,22,19,35,19,26,26,21,21,26,26,19,28,19,28,19,21,26,26,21,19,35,19,26,26,19},
{26,24,24,33,23,33,24,31,31,24,26,23,42,23,26,24,24,31,31,24,24,33,23,33,23,33,23,33,24,31,31,24,24,31,31,24,23,42,23,26,23,42,23,31,31,24,24,31,31,23,33,23,33,23,24,31,31,24,23,42,23,31,31,23},
{28,26,26,35,25,35,26,33,33,26,28,25,45,25,28,26,26,33,33,26,26,35,25,35,25,35,25,35,26,33,33,26,26,33,33,26,25,45,25,28,25,45,25,33,33,26,26,33,33,25,35,25,35,25,26,33,33,26,25,45,25,33,33,25},
{32,30,30,40,28,40,30,38,38,30,32,28,51,28,32,30,30,38,38,30,30,40,28,40,28,40,28,40,30,38,38,30,30,38,38,30,28,51,28,32,28,51,28,38,38,30,30,38,38,28,40,28,40,28,30,38,38,30,28,51,28,38,38,28},
{36,34,34,46,32,46,34,43,43,34,36,32,58,32,36,34,34,43,43,34,34,46,32,46,32,46,32,46,34,43,43,34,34,43,43,34,32,58,32,36,32,58,32,43,43,34,34,43,43,32,46,32,46,32,34,43,43,34,32,58,32,43,43,32}};
static const uint8_t block8x8_coefficients_tab[]={0x00,0x1F,0x3F,0x0F,0x08,0x04,0x02,0x01,0x0B,0x0E,0x1B,0x0D,0x03,0x07,0x0C,0x17,0x1D,0x0A,0x1E,0x05,0x10,0x2F,0x37,0x3B,0x13,0x3D,0x3E,0x09,0x1C,0x06,0x15,0x1A,0x33,0x11,0x12,0x14,0x18,0x20,0x3C,0x35,0x19,0x16,0x3A,0x30,0x31,0x32,0x27,0x34,0x2B,0x2D,0x39,0x38,0x23,0x36,0x2E,0x21,0x25,0x22,0x24,0x2C,0x2A,0x28,0x29,0x26};
static const uint8_t run_residue[2][256]={{
12,6,4,3,3,3,3,2,2,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
3,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
1,27,11,7,3,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,41,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},{
27,10,5,4,3,3,3,3,2,2,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
8,3,2,2,2,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
1,15,10,8,4,3,2,2,2,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,21,7,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}};
static const uint8_t bits0[]={9,11,11,11,11,10,10,10,10,10,10,10,10,10,10,10,10,11,11,11,11,11,11,11,11,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,7,10,10,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,8,8,8,8,8,8,8,8,8,8,8,8,8,7,7,7,7,7,7,7,7,6,6,6,6,6,6,6,6,6,6,5,5,5,4,2,3,4,4};
static const uint16_t syms0[]={0x0,0x822,0x803,0xB,0xA,0xB81,0xB61,0xB41,0xB21,0x122,0x102,0xE2,0xC2,0xA2,0x63,0x43,0x24,0xC,0x25,0x2E1,0x301,0xBA1,0xBC1,0xBE1,0xC01,0x26,0x44,0x83,0xA3,0xC3,0x142,0x321,0x341,0xC21,0xC41,0xC61,0xC81,0xCA1,0xCC1,0xCE1,0xD01,0x0,0x9,0x8,0xB01,0xAE1,0xAC1,0xAA1,0xA81,0xA61,0xA41,0xA21,0x802,0x2C1,0x2A1,0x281,0x261,0x241,0x221,0x201,0x1E1,0x82,0x62,0x7,0x6,0xA01,0x9E1,0x9C1,0x9A1,0x981,0x961,0x941,0x921,0x1C1,0x1A1,0x42,0x23,0x5,0x901,0x8E1,0x8C1,0x8A1,0x181,0x161,0x141,0x4,0x881,0x861,0x841,0x821,0x121,0x101,0xE1,0xC1,0x22,0x3,0xA1,0x81,0x61,0x801,0x1,0x21,0x41,0x2};
static const uint16_t syms1[]={0x0,0x807,0x806,0x16,0x15,0x842,0x823,0x805,0x1A1,0xA3,0x102,0x83,0x64,0x44,0x27,0x14,0x13,0x17,0x18,0x28,0x122,0x862,0x882,0x9E1,0xA01,0x19,0x1A,0x1B,0x29,0xC3,0x2A,0x45,0xE3,0x1C1,0x808,0x8A2,0x8C2,0xA21,0xA41,0xA61,0xA81,0x0,0x12,0x11,0x9C1,0x9A1,0x981,0x961,0x941,0x822,0x804,0x181,0x161,0xE2,0xC2,0xA2,0x63,0x43,0x26,0x25,0x10,0x82,0xF,0xE,0xD,0x901,0x8E1,0x8C1,0x803,0x141,0x121,0x101,0x921,0x62,0x24,0xC,0xB,0xA,0x881,0x861,0xC1,0x8A1,0xE1,0x42,0x23,0x9,0x802,0xA1,0x841,0x821,0x81,0x61,0x8,0x7,0x22,0x6,0x41,0x5,0x4,0x801,0x1,0x2,0x21,0x3};

/* ---------------- VLC tables ---------------- */
static int vcode[2][2][64][32], vlen[2][2][64][32]; /* [tab][last][run][level] */
static int esc_code[2], esc_len[2];
static void init_vlc(void){
  memset(vlen,0,sizeof vlen);
  for(int t=0;t<2;t++){
    const uint16_t*sy=t?syms1:syms0; uint64_t code=0; esc_len[t]=99;
    for(int i=0;i<104;i++){
      int len=bits0[i]; unsigned c=(unsigned)(code>>(32-len)); int n=sy[i];
      int last=(n>>11)==1, run=(n>>5)&63, lev=n&31;
      if(lev==0){ if(len<esc_len[t]){esc_len[t]=len;esc_code[t]=c;} }
      else if(!vlen[t][last][run][lev]||len<vlen[t][last][run][lev]){vlen[t][last][run][lev]=len;vcode[t][last][run][lev]=c;}
      code+=1ULL<<(32-len);
    }
  }
}
static const uint8_t mvb10[10]={3,4,5,5,3,5,6,6,4,1}, mvs10[10]={8,2,3,6,1,7,5,4,9,0};
static int mv6_code,mv6_len;
static void init_mv(void){uint64_t code=0;for(int i=0;i<10;i++){int l=mvb10[i];if(mvs10[i]==6){mv6_code=(int)(code>>(32-l));mv6_len=l;}code+=1ULL<<(32-l);}}
/* ---------------- bit writer ---------------- */
typedef struct {uint8_t*buf;size_t cap,nbits;} BW;
static void bw_init(BW*b,size_t cap){b->buf=calloc(cap,1);b->cap=cap;b->nbits=0;}
static void bw_put(BW*b,unsigned v,int n){for(int i=n-1;i>=0;i--){if(b->nbits/8>=b->cap){fprintf(stderr,"bw overflow\n");exit(1);} if((v>>i)&1)b->buf[b->nbits>>3]|=0x80>>(b->nbits&7); b->nbits++;}}
static void bw_ue(BW*b,unsigned v){unsigned x=v+1;int n=0;while((x>>n)>1)n++; bw_put(b,0,n); bw_put(b,x,n+1);}
static void bw_se(BW*b,int v){bw_ue(b,v>0?2*v-1:-2*v);}
static int ue_len(unsigned v){unsigned x=v+1;int n=0;while((x>>n)>1)n++;return 2*n+1;}

/* ---------------- decoder mirror ---------------- */
typedef struct BlockXY{int w,h,ax,ay,x,y,size;uint8_t*block;int linesize;} BlockXY;
static int clip(int v,int lo,int hi){return v<lo?lo:v>hi?hi:v;}
static uint8_t pget(BlockXY b){BlockXY r=b;int x,y;
  if(b.x==-1&&b.y>=b.size){r.x=-1,r.y=b.size-1;}else if(b.x>=-1&&b.y>=-1){r.x=b.x,r.y=b.y;}
  else if(b.x==-1&&b.y==-2){r.x=0,r.y=-1;}else if(b.x==-2&&b.y==-1){r.x=-1,r.y=0;}
  y=clip(r.ay+r.y,0,r.h-1);x=clip(r.ax+r.x,0,r.w-1);return r.block[y*r.linesize+x];}
static uint8_t half(int a,int b){return ((a+b)+1)/2;}
static uint8_t half3(int a,int b,int c){return ((a+b+b+c)*2/4+1)/2;}
static uint8_t pick_above(BlockXY b){b.y=b.y-1;return pget(b);}
static uint8_t pick_left(BlockXY b){b.x=b.x-1;return pget(b);}
static uint8_t half_horz(BlockXY b){BlockXY a=b,c=b;a.x-=1;c.x+=1;return half3(pget(a),pget(b),pget(c));}
static uint8_t half_vert(BlockXY b){BlockXY a=b,c=b;a.y-=1;c.y+=1;return half3(pget(a),pget(b),pget(c));}
static uint8_t pick_4(BlockXY bxy){int val;if((bxy.x%2)==0){BlockXY ba=bxy,bb=bxy;int a,b;ba.x=-1;ba.y=bxy.y+bxy.x/2;a=pget(ba);bb.x=-1;bb.y=bxy.y+bxy.x/2+1;b=pget(bb);val=half(a,b);}else{BlockXY ba=bxy;ba.x=-1;ba.y=bxy.y+bxy.x/2+1;val=half_vert(ba);}return val;}
static uint8_t pick_5(BlockXY bxy){int val;if(bxy.x==0){BlockXY a=bxy,b=bxy;a.x=-1;a.y-=1;b.x=-1;val=half(pget(a),pget(b));}else if(bxy.y==0){BlockXY a=bxy;a.x-=2;a.y-=1;val=half_horz(a);}else if(bxy.x==1){BlockXY a=bxy;a.x-=2;a.y-=1;val=half_vert(a);}else{BlockXY a=bxy;a.x-=2;a.y-=1;val=pget(a);}return val;}
static uint8_t pick_6(BlockXY bxy){int val;if(bxy.y==0){BlockXY a=bxy,b=bxy;a.x-=1;a.y=-1;b.y=-1;val=half(pget(a),pget(b));}else if(bxy.x==0){BlockXY a=bxy;a.x-=1;a.y-=2;val=half_vert(a);}else if(bxy.y==1){BlockXY a=bxy;a.x-=1;a.y-=2;val=half_horz(a);}else{BlockXY a=bxy;a.x-=1;a.y-=2;val=pget(a);}return val;}
static uint8_t pick_7(BlockXY bxy){int clr,acc1,acc2;BlockXY a=bxy;a.x-=1;a.y-=1;clr=pget(a);if(bxy.x&&bxy.y)return clr;
  if(bxy.x==0){a.x=-1;a.y=bxy.y;}else{a.x=bxy.x-2;a.y=-1;}acc1=pget(a);
  if(bxy.y==0){a.x=bxy.x;a.y=-1;}else{a.x=-1;a.y=bxy.y-2;}acc2=pget(a);return half3(acc1,clr,acc2);}
typedef uint8_t (*pickf)(BlockXY);
static void block_fill(uint8_t*block,int size,int ls,int w,int h,int ax,int ay,pickf pick){BlockXY b;b.size=size;b.block=block;b.linesize=ls;b.w=w;b.h=h;b.ay=ay;b.ax=ax;
  for(int y=0;y<size;y++){b.y=y;for(int x=0;x<size;x++){b.x=x;uint8_t v=pick(b);block[ax+x+(ay+y)*ls]=v;}}}
static int block_sum(const uint8_t*p,int w,int h,int ls){int s=0;for(int y=0;y<h;y++){for(int x=0;x<w;x++)s+=p[x];p+=ls;}return s;}
static void predict(uint8_t*pl,int ls,int w,int h,int ax,int ay,int size,int pmode){
  switch(pmode){
  case 0:block_fill(pl,size,ls,w,h,ax,ay,pick_above);break;
  case 1:block_fill(pl,size,ls,w,h,ax,ay,pick_left);break;
  case 3:{uint8_t fill;
    if(ax==0&&ay==0)fill=0x80;
    else if(ax>=1&&ay>=1){int l=block_sum(pl+ay*ls+ax-1,1,size,ls),t=block_sum(pl+(ay-1)*ls+ax,size,1,ls);fill=((l+t)*2/(2*size)+1)/2;}
    else if(ax>=1)fill=(block_sum(pl+ay*ls+ax-1,1,size,ls)*2/size+1)/2;
    else fill=(block_sum(pl+(ay-1)*ls+ax,size,1,ls)*2/size+1)/2;
    for(int y=0;y<size;y++)memset(pl+(ay+y)*ls+ax,fill,size);}break;
  case 4:block_fill(pl,size,ls,w,h,ax,ay,pick_4);break;
  case 5:block_fill(pl,size,ls,w,h,ax,ay,pick_5);break;
  case 6:block_fill(pl,size,ls,w,h,ax,ay,pick_6);break;
  case 7:block_fill(pl,size,ls,w,h,ax,ay,pick_7);break;
  }
}
static void inverse4(int*rs){int a=rs[0]+rs[2],b=rs[0]-rs[2],c=rs[1]+(rs[3]>>1),d=(rs[1]>>1)-rs[3];rs[0]=a+c;rs[1]=b+d;rs[2]=b-d;rs[3]=a-c;}
static void idct8(int*arr){int e,f,g,h,x3,x2,x1,x0,tmp[4];tmp[0]=arr[0];tmp[1]=arr[2];tmp[2]=arr[4];tmp[3]=arr[6];inverse4(tmp);
  e=arr[7]+arr[1]-arr[3]-(arr[3]>>1);f=arr[7]-arr[1]+arr[5]+(arr[5]>>1);g=arr[5]-arr[3]-arr[7]-(arr[7]>>1);h=arr[5]+arr[3]+arr[1]+(arr[1]>>1);
  x3=g+(h>>2);x2=e+(f>>2);x1=(e>>2)-f;x0=h-(g>>2);
  arr[0]=tmp[0]+x0;arr[1]=tmp[1]+x1;arr[2]=tmp[2]+x2;arr[3]=tmp[3]+x3;arr[4]=tmp[3]-x3;arr[5]=tmp[2]-x2;arr[6]=tmp[1]-x1;arr[7]=tmp[0]-x0;}
/* add dequantised levels (scan order) to block, exactly as decoder */
static void add_coeffs(uint8_t*dst,int ls,const int*lev,const int*qtab){
  int mat[64]={0};for(int p=0;p<64;p++)if(lev[p])mat[zigzag_direct[p]]=qtab[p]*lev[p];
  mat[0]+=32;for(int y=0;y<8;y++)idct8(&mat[y*8]);
  for(int y=0;y<8;y++){for(int x=y+1;x<8;x++){int a=mat[x*8+y],b=mat[y*8+x];mat[y*8+x]=a;mat[x*8+y]=b;}
    idct8(&mat[y*8]);for(int x=0;x<8;x++)dst[x]=clip(dst[x]+(mat[y*8+x]>>6),0,255);dst+=ls;}
}
/* ---------------- forward transform (float inverse of decoder's linear map) ---------------- */
static double Finv[64][64]; /* coef(natural idx) = sum Finv * residual*64 */
static void fidct8(double*a){double t0=a[0]+a[4],t1=a[0]-a[4],t2=a[2]+a[6]/2,t3=a[2]/2-a[6];double tmp[4]={t0+t2,t1+t3,t1-t3,t0-t2};
  double e=a[7]+a[1]-a[3]-a[3]/2,f=a[7]-a[1]+a[5]+a[5]/2,g=a[5]-a[3]-a[7]-a[7]/2,h=a[5]+a[3]+a[1]+a[1]/2;
  double x3=g+h/4,x2=e+f/4,x1=e/4-f,x0=h-g/4;double o[8]={tmp[0]+x0,tmp[1]+x1,tmp[2]+x2,tmp[3]+x3,tmp[3]-x3,tmp[2]-x2,tmp[1]-x1,tmp[0]-x0};memcpy(a,o,sizeof o);}
static void init_fwd(void){double M[64][128];
  for(int k=0;k<64;k++){double m[64]={0};m[k]=1;for(int y=0;y<8;y++)fidct8(&m[y*8]);
    double t[64];for(int y=0;y<8;y++)for(int x=0;x<8;x++)t[y*8+x]=m[x*8+y];for(int y=0;y<8;y++)fidct8(&t[y*8]);
    for(int i=0;i<64;i++)M[i][k]=t[i];}
  for(int i=0;i<64;i++)for(int j=0;j<64;j++)M[i][64+j]=(i==j);
  for(int c=0;c<64;c++){int p=c;for(int r=c;r<64;r++)if(fabs(M[r][c])>fabs(M[p][c]))p=r;
    for(int j=0;j<128;j++){double t=M[c][j];M[c][j]=M[p][j];M[p][j]=t;}double d=M[c][c];for(int j=0;j<128;j++)M[c][j]/=d;
    for(int r=0;r<64;r++)if(r!=c){double f=M[r][c];if(f!=0)for(int j=0;j<128;j++)M[r][j]-=f*M[c][j];}}
  for(int i=0;i<64;i++)for(int j=0;j<64;j++)Finv[i][j]=M[i][64+j];}

/* ---------------- encoder ---------------- */
static TLS int TAB=1; static TLS int QTAB[64]; static TLS double DZ=1.0/3;
static double lambda_for_q(int q){ double l=0.85*0.6; for(int i=12;i<q;i++) l*=1.2599210498948731648; for(int i=q;i<12;i++) l/=1.2599210498948731648; return l; }
static void setq(int q){int qx=q%6,qy=q/6;for(int i=0;i<64;i++)QTAB[i]=quant8x8_tab[qx][i]<<(qy-2);}
/* code levels; returns bits (and writes if bw) */
static int code_coef(BW*bw,int last,int run,int lev){
  int a=abs(lev),s=lev<0,best=1<<30,how=-1,r2=0,a2=0;
  if(a<32&&run<64&&vlen[TAB][last][run][a]){best=vlen[TAB][last][run][a]+1;how=0;}
  {int res=run_residue[TAB][(last?64:0)+run];int aa=a-res; if(aa>0&&aa<32&&vlen[TAB][last][run][aa]){int c=esc_len[TAB]+1+vlen[TAB][last][run][aa]+1;if(c<best){best=c;how=1;a2=aa;}}}
  if(a<64){int res=run_residue[TAB][128+(last?64:0)+a];int rr=run-res; if(rr>=0&&a<32&&vlen[TAB][last][rr][a]){int c=esc_len[TAB]+2+vlen[TAB][last][rr][a]+1;if(c<best){best=c;how=2;r2=rr;}}}
  {int c=esc_len[TAB]+2+1+6+12;if(c<best){best=c;how=3;}}
  if(bw){switch(how){
    case 0:bw_put(bw,vcode[TAB][last][run][a],vlen[TAB][last][run][a]);bw_put(bw,s,1);break;
    case 1:bw_put(bw,esc_code[TAB],esc_len[TAB]);bw_put(bw,0,1);bw_put(bw,vcode[TAB][last][run][a2],vlen[TAB][last][run][a2]);bw_put(bw,s,1);break;
    case 2:bw_put(bw,esc_code[TAB],esc_len[TAB]);bw_put(bw,2,2);bw_put(bw,vcode[TAB][last][r2][a],vlen[TAB][last][r2][a]);bw_put(bw,s,1);break;
    case 3:bw_put(bw,esc_code[TAB],esc_len[TAB]);bw_put(bw,3,2);bw_put(bw,last,1);bw_put(bw,run,6);bw_put(bw,((unsigned)lev)&0xFFF,12);break;}}
  return best;}
static int code_block(BW*bw,const int*lev){int bits=0,lastp=-1;for(int p=0;p<64;p++)if(lev[p])lastp=p;
  int prev=-1;for(int p=0;p<64;p++)if(lev[p]){bits+=code_coef(bw,p==lastp,p-prev-1,lev[p]);prev=p;}return bits;}
/* quantise residual block: tgt-pred (8x8) -> levels; returns nonzero count */
static int quant_block(const uint8_t*tgt,int tls,const uint8_t*pred,int pls,int*lev){
  double r[64],c[64];for(int y=0;y<8;y++)for(int x=0;x<8;x++)r[y*8+x]=64.0*(tgt[y*tls+x]-pred[y*pls+x]);
  for(int i=0;i<64;i++){double s=0;for(int j=0;j<64;j++)s+=Finv[i][j]*r[j];c[i]=s;}
  int nz=0;for(int p=0;p<64;p++){double v=c[zigzag_direct[p]]/QTAB[p];int a=(int)(fabs(v)+DZ);if(a>2047)a=2047;lev[p]=v<0?-a:a;if(a)nz++;}
  return nz;}
typedef struct{int w,h;uint8_t*p[3];int ls[3],pw[3],ph[3];} Pic;
static int64_t ssd8(const uint8_t*a,int als,const uint8_t*b,int bls){int64_t s=0;for(int y=0;y<8;y++)for(int x=0;x<8;x++){int d=a[y*als+x]-b[y*bls+x];s+=d*d;}return s;}
/* encode one 8x8 block in place on recon; returns bits; levels out */
static int enc_block(Pic*rec,Pic*tgt,int pl,int ax,int ay,int pmode,int*lev,int*has){
  predict(rec->p[pl],rec->ls[pl],rec->pw[pl],rec->ph[pl],ax,ay,8,pmode);
  uint8_t*d=rec->p[pl]+ay*rec->ls[pl]+ax;const uint8_t*t=tgt->p[pl]+ay*tgt->ls[pl]+ax;
  int nz=quant_block(t,tgt->ls[pl],d,rec->ls[pl],lev);*has=nz>0;
  if(!nz)return 0; add_coeffs(d,rec->ls[pl],lev,QTAB); return ue_len(0)+code_block(NULL,lev);}
static void save_mb(Pic*r,int x,int y,uint8_t*buf){int k=0;for(int j=0;j<16;j++){memcpy(buf+k,r->p[0]+(y+j)*r->ls[0]+x,16);k+=16;}
  for(int pl=1;pl<3;pl++)for(int j=0;j<8;j++){memcpy(buf+k,r->p[pl]+(y/2+j)*r->ls[pl]+x/2,8);k+=8;}}
static void load_mb(Pic*r,int x,int y,const uint8_t*buf){int k=0;for(int j=0;j<16;j++){memcpy(r->p[0]+(y+j)*r->ls[0]+x,buf+k,16);k+=16;}
  for(int pl=1;pl<3;pl++)for(int j=0;j<8;j++){memcpy(r->p[pl]+(y/2+j)*r->ls[pl]+x/2,buf+k,8);k+=8;}}
static int64_t mb_ssd(Pic*r,Pic*t,int x,int y){int64_t s=0;for(int i=0;i<4;i++)s+=ssd8(r->p[0]+(y+(i/2)*8)*r->ls[0]+x+(i%2)*8,r->ls[0],t->p[0]+(y+(i/2)*8)*t->ls[0]+x+(i%2)*8,t->ls[0]);
  for(int pl=1;pl<3;pl++)s+=ssd8(r->p[pl]+(y/2)*r->ls[pl]+x/2,r->ls[pl],t->p[pl]+(y/2)*t->ls[pl]+x/2,t->ls[pl]);return s;}
static int flags_idx[64], pflags_idx[64];
static const uint8_t pframe8_tab[64]={0x00,0x0F,0x04,0x01,0x08,0x02,0x0C,0x03,0x05,0x0A,0x0D,0x07,0x0E,0x0B,0x1F,0x09,0x06,0x10,0x3F,0x1E,0x17,0x1D,0x1B,0x1C,0x13,0x18,0x1A,0x12,0x11,0x14,0x15,0x20,0x2F,0x16,0x19,0x37,0x3D,0x3E,0x3B,0x3C,0x33,0x35,0x21,0x24,0x22,0x28,0x23,0x2C,0x30,0x27,0x2D,0x25,0x3A,0x2B,0x2E,0x2A,0x31,0x34,0x38,0x32,0x29,0x26,0x39,0x36};
static int mvc[10],mvl[10];
static void init_mvall(void){uint64_t code=0;for(int i=0;i<10;i++){int l=mvb10[i];mvc[mvs10[i]]=(int)(code>>(32-l));mvl[mvs10[i]]=l;code+=1ULL<<(32-l);}}
static int se_len(int v){return ue_len(v>0?2*v-1:-2*v);}
static int mid3(int a,int b,int c){int mx=a>b?a:b,mn=a<b?a:b;if(c>mx)return mx;if(c<mn)return mn;return c;}
/* exact copy of decoder motion compensation for one plane block */
static void mc_block(const Pic*ref,Pic*dst,int pl,int ox,int oy,int w,int h,int mvx,int mvy){
  int sls=ref->ls[pl],dls=dst->ls[pl],method=(mvx&1)|((mvy&1)<<1);
  const uint8_t*src=ref->p[pl]+ox+(mvx>>1)+(oy+(mvy>>1))*sls; uint8_t*d=dst->p[pl]+ox+oy*dls;
  for(int y=0;y<h;y++){for(int x=0;x<w;x++){
      switch(method){
      case 0:d[x]=src[x];break;
      case 1:d[x]=(uint8_t)((src[x]>>1)+(src[x+1]>>1));break;
      case 2:d[x]=(uint8_t)((src[x]>>1)+(src[x+sls]>>1));break;
      default:d[x]=(uint8_t)((((src[x]>>1)+(src[x+1]>>1))>>1)+(((src[x+sls]>>1)+(src[x+1+sls]>>1))>>1));}}
    d+=dls;src+=sls;}}
static int mv_valid(const Pic*r,int x,int y,int mvx,int mvy){
  int ox=x,oy=y,W=16,H=16,fw=r->w,fh=r->h,mx=mvx,my=mvy;
  for(int i=0;i<3;i++){if(i==1){ox>>=1;oy>>=1;mx>>=1;my>>=1;W>>=1;H>>=1;fw>>=1;fh>>=1;}
    if(ox+(mx>>1)<0||oy+(my>>1)<0||ox+W+((mx+1)>>1)>fw||oy+H+((my+1)>>1)>fh)return 0;}
  return 1;}
static int64_t sad_mv(const Pic*ref,const Pic*tgt,int x,int y,int mvx,int mvy,int64_t best){
  int sls=ref->ls[0],method=(mvx&1)|((mvy&1)<<1);int64_t s=0;
  const uint8_t*src=ref->p[0]+x+(mvx>>1)+(y+(mvy>>1))*sls;const uint8_t*t=tgt->p[0]+x+y*tgt->ls[0];
  for(int j=0;j<16;j++){for(int i=0;i<16;i++){int p;
      switch(method){case 0:p=src[i];break;case 1:p=(src[i]>>1)+(src[i+1]>>1);break;case 2:p=(src[i]>>1)+(src[i+sls]>>1);break;
      default:p=(((src[i]>>1)+(src[i+1]>>1))>>1)+(((src[i+sls]>>1)+(src[i+1+sls]>>1))>>1);}
      s+=abs(p-t[i]);}
    if(s>=best)return s; src+=sls;t+=tgt->ls[0];}
  return s;}
/* inter-code one MB in place on rec. returns residual bits (coef payload incl. per-block ue) */
static int enc_inter(const Pic*ref,Pic*rec,Pic*tgt,int x,int y,int mvx,int mvy,int levs[6][64],int has[6],int allow){
  mc_block(ref,rec,0,x,y,16,16,mvx,mvy);mc_block(ref,rec,1,x>>1,y>>1,8,8,mvx>>1,mvy>>1);mc_block(ref,rec,2,x>>1,y>>1,8,8,mvx>>1,mvy>>1);
  int bits=0;
  for(int b=0;b<6;b++){int pl=b<4?0:b-3,ax=b<4?x+(b%2)*8:x>>1,ay=b<4?y+(b/2)*8:y>>1;
    uint8_t*d=rec->p[pl]+ay*rec->ls[pl]+ax;const uint8_t*t=tgt->p[pl]+ay*tgt->ls[pl]+ax;
    has[b]=0; if(!allow)continue;
    int nz=quant_block(t,tgt->ls[pl],d,rec->ls[pl],levs[b]);
    if(nz){has[b]=1;add_coeffs(d,rec->ls[pl],levs[b],QTAB);bits+=ue_len(0)+code_block(NULL,levs[b]);}}
  return bits;}
/* intra-code one MB in place (mode decision inside). returns bits, fills modes/levels */
static TLS int ALLOW[10]={1,1,0,1,1,1,1,1,0,0};
static int enc_intra(Pic*rec,Pic*tgt,int x,int y,double lambda,int*blo,int*bco,int levs[6][64],int has[6]){
  static TLS uint8_t tmpbuf[384];
  int lmodes[8],nl=0;
  if(x==0&&y==0){lmodes[nl++]=3;}else if(y==0){lmodes[nl++]=3;lmodes[nl++]=1;}else if(x==0){lmodes[nl++]=3;lmodes[nl++]=0;}
  else{int m[]={3,0,1,4,5,6,7};for(int i=0;i<7;i++)if(ALLOW[m[i]])lmodes[nl++]=m[i];}
  save_mb(rec,x,y,tmpbuf);
  double bestJ=1e300;int bl=3;
  for(int i=0;i<nl;i++){load_mb(rec,x,y,tmpbuf);int lev[64],hh,bits=3;int64_t d=0;
    for(int b=0;b<4;b++){int bx=x+(b%2)*8,by=y+(b/2)*8;bits+=enc_block(rec,tgt,0,bx,by,lmodes[i],lev,&hh);
      d+=ssd8(rec->p[0]+by*rec->ls[0]+bx,rec->ls[0],tgt->p[0]+by*tgt->ls[0]+bx,tgt->ls[0]);}
    double J=d+lambda*bits;if(J<bestJ){bestJ=J;bl=lmodes[i];}}
  double bestC=1e300;int bc=3;
  for(int i=0;i<nl;i++){load_mb(rec,x,y,tmpbuf);int lev[64],hh,bits=3;int64_t d=0;
    for(int pl=1;pl<3;pl++){bits+=enc_block(rec,tgt,pl,x/2,y/2,lmodes[i],lev,&hh);d+=ssd8(rec->p[pl]+(y/2)*rec->ls[pl]+x/2,rec->ls[pl],tgt->p[pl]+(y/2)*tgt->ls[pl]+x/2,tgt->ls[pl]);}
    double J=d+lambda*bits;if(J<bestC){bestC=J;bc=lmodes[i];}}
  load_mb(rec,x,y,tmpbuf);int bits=6;
  for(int b=0;b<4;b++)bits+=enc_block(rec,tgt,0,x+(b%2)*8,y+(b/2)*8,bl,levs[b],&has[b]);
  for(int pl=1;pl<3;pl++)bits+=enc_block(rec,tgt,pl,x/2,y/2,bc,levs[3+pl],&has[3+pl]);
  int fl=0;for(int b=0;b<6;b++)fl|=has[b]<<b; bits+=ue_len(flags_idx[fl]);
  *blo=bl;*bco=bc;return bits;}
static TLS int64_t stat_inter=0,stat_intra=0,stat_skipres=0;
static size_t encode_frame(Pic*tgt,Pic*rec,Pic**refs,int nref,int q,uint8_t*out,size_t cap,int isI,int prevq,uint32_t word){
  setq(q); double lambda=lambda_for_q(q), lsad=sqrt(lambda);
  BW bw;bw_init(&bw,cap);
  if(isI){TAB=1;bw_put(&bw,1,1);bw_put(&bw,1,1);bw_put(&bw,TAB,1);bw_put(&bw,q,6);}
  else{TAB=0;bw_put(&bw,0,1);bw_se(&bw,q-prevq);}
  int nmv=tgt->w/16+3; int mx[64],my[64]; memset(mx,0,sizeof mx);memset(my,0,sizeof my);
  static TLS uint8_t ibuf[384],pbuf[384],obuf[384];
  for(int y=0;y<tgt->h;y+=16)for(int x=0;x<tgt->w;x+=16){
    int c=x/16;
    int ilevs[6][64],ihas[6],bl,bc;
    if(isI){
      DZ=1.0/3; enc_intra(rec,tgt,x,y,lambda,&bl,&bc,ilevs,ihas);
      int fl=0;for(int b=0;b<6;b++)fl|=ihas[b]<<b;
      bw_put(&bw,0,1);bw_ue(&bw,flags_idx[fl]);bw_put(&bw,bl,3);
      for(int b=0;b<4;b++)if(ihas[b]){bw_ue(&bw,0);code_block(&bw,ilevs[b]);}
      bw_put(&bw,bc,3);for(int b=4;b<6;b++)if(ihas[b]){bw_ue(&bw,0);code_block(&bw,ilevs[b]);}
      continue;}
    /* ---- P frame MB ---- */
    int px=mid3(mx[c+1],mx[c+2],mx[c+3]),py=mid3(my[c+1],my[c+2],my[c+3]);
    mx[c+2]=0;my[c+2]=0;
    save_mb(rec,x,y,obuf);
    /* intra candidate */
    DZ=1.0/3; int ibits=enc_intra(rec,tgt,x,y,lambda,&bl,&bc,ilevs,ihas)+mvl[6];
    double Ji=mb_ssd(rec,tgt,x,y)+lambda*ibits; save_mb(rec,x,y,ibuf);
    /* motion search over refs 1..nref (half-pel units) */
    int64_t best=((int64_t)1<<60);int bmx=0,bmy=0,bref=1;
    for(int r=1;r<=nref;r++){const Pic*ref=refs[r];int R=(r==1)?32:16;
      int cands[3][2]={{0,0},{px,py},{px&~1,py&~1}};
      int64_t rb=((int64_t)1<<60);int rmx=0,rmy=0;
      for(int k=0;k<3;k++){int vx=cands[k][0],vy=cands[k][1];if(!mv_valid(ref,x,y,vx,vy))continue;
        int hb=(r==1&&vx==px&&vy==py)?mvl[0]:mvl[r]+se_len(vx-px)+se_len(vy-py);
        int64_t cst=sad_mv(ref,tgt,x,y,vx,vy,rb)+(int64_t)(lsad*hb);if(cst<rb){rb=cst;rmx=vx;rmy=vy;}}
      for(int vy=-R;vy<=R;vy+=2)for(int vx=-R;vx<=R;vx+=2){
        if(!mv_valid(ref,x,y,vx,vy))continue;
        int64_t pen=(int64_t)(lsad*(mvl[r]+se_len(vx-px)+se_len(vy-py)));
        int64_t cst=sad_mv(ref,tgt,x,y,vx,vy,rb-pen)+pen;if(cst<rb){rb=cst;rmx=vx;rmy=vy;}}
      {int cx=rmx,cy=rmy;for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){if(!dx&&!dy)continue;int vx=cx+dx,vy=cy+dy;
        if(!mv_valid(ref,x,y,vx,vy))continue;int64_t pen=(int64_t)(lsad*(mvl[r]+se_len(vx-px)+se_len(vy-py)));
        int64_t cst=sad_mv(ref,tgt,x,y,vx,vy,rb-pen)+pen;if(cst<rb){rb=cst;rmx=vx;rmy=vy;}}}
      if(rb<best){best=rb;bmx=rmx;bmy=rmy;bref=r;}}
    const Pic*ref=refs[bref];
    int usepred=(bref==1&&bmx==px&&bmy==py);
    int hdr=usepred?mvl[0]:mvl[bref]+se_len(bmx-px)+se_len(bmy-py);
    /* inter with residual */
    int plevs[6][64],phas[6]; DZ=1.0/6;
    load_mb(rec,x,y,obuf); int rb=enc_inter(ref,rec,tgt,x,y,bmx,bmy,plevs,phas,1);
    int fl=0;for(int b=0;b<6;b++)fl|=phas[b]<<b;
    double Jr=mb_ssd(rec,tgt,x,y)+lambda*(hdr+ue_len(pflags_idx[fl])+rb); save_mb(rec,x,y,pbuf);
    /* inter without residual */
    int zlevs[6][64],zhas[6]; load_mb(rec,x,y,obuf); enc_inter(ref,rec,tgt,x,y,bmx,bmy,zlevs,zhas,0);
    double Jz=mb_ssd(rec,tgt,x,y)+lambda*(hdr+ue_len(pflags_idx[0]));
    if(Ji<=Jr&&Ji<=Jz){ /* intra */
      load_mb(rec,x,y,ibuf); stat_intra++;
      int f2=0;for(int b=0;b<6;b++)f2|=ihas[b]<<b;
      bw_put(&bw,mvc[6],mvl[6]);bw_ue(&bw,flags_idx[f2]);bw_put(&bw,bl,3);
      for(int b=0;b<4;b++)if(ihas[b]){bw_ue(&bw,0);code_block(&bw,ilevs[b]);}
      bw_put(&bw,bc,3);for(int b=4;b<6;b++)if(ihas[b]){bw_ue(&bw,0);code_block(&bw,ilevs[b]);}
    } else {
      int useres=Jr<Jz; if(useres)load_mb(rec,x,y,pbuf); else stat_skipres++; /* Jz recon already in rec */
      stat_inter++;
      if(usepred)bw_put(&bw,mvc[0],mvl[0]); else {bw_put(&bw,mvc[bref],mvl[bref]);bw_se(&bw,bmx-px);bw_se(&bw,bmy-py);}
      mx[c+2]=bmx;my[c+2]=bmy;
      if(useres){bw_ue(&bw,pflags_idx[fl]);
        for(int b=0;b<6;b++)if(phas[b]){bw_ue(&bw,0);code_block(&bw,plevs[b]);}}
      else bw_ue(&bw,pflags_idx[0]);
    }
  }
  if(isI){ while(bw.nbits%16)bw_put(&bw,0,1); bw_put(&bw,word>>16,16); bw_put(&bw,word&0xffff,16); }
  while(bw.nbits%32)bw_put(&bw,0,1);
  size_t nb=bw.nbits/8; if(nb>cap){fprintf(stderr,"cap\n");exit(1);}
  for(size_t i=0;i<nb;i+=2){out[i]=bw.buf[i+1];out[i+1]=bw.buf[i];}
  free(bw.buf);return nb;}


static int inited;
void mobienc_init(void){
  if(inited) return;
  init_vlc();init_fwd();for(int i=0;i<64;i++){flags_idx[block8x8_coefficients_tab[i]]=i;pflags_idx[pframe8_tab[i]]=i;}init_mvall();init_mv();
  inited=1;
}
int mobienc_encode(const uint8_t *frames, int n, int w, int h, const char *types, const uint32_t *words,
                   int q, int maxref, const char *intra_modes, uint8_t **pk, uint32_t *pklen, uint8_t *recon){
  if(!inited) return 0;
  if(intra_modes){ for(int i=0;i<10;i++)ALLOW[i]=0; for(const char*p=intra_modes;*p;p++) if(*p>='0'&&*p<='9') ALLOW[*p-'0']=1; ALLOW[3]=1; }
  else { int d[10]={1,1,0,1,1,1,1,1,0,0}; memcpy(ALLOW,d,sizeof d); }
  if(maxref<1) maxref=1; if(maxref>5) maxref=5;
  size_t fs=(size_t)w*h*3/2; size_t cap=(size_t)4<<20;
  uint8_t*tb=malloc(fs),*rb=calloc(fs,1),*out=malloc(cap); uint8_t*rbuf[6]; Pic RING[6];
  if(!tb||!rb||!out){ free(tb);free(rb);free(out); return 0; }
  for(int i=0;i<6;i++){ rbuf[i]=calloc(fs,1); Pic t={w,h,{rbuf[i],rbuf[i]+w*h,rbuf[i]+w*h+w*h/4},{w,w/2,w/2},{w,w/2,w/2},{h,h/2,h/2}}; RING[i]=t; }
  Pic T={w,h,{tb,tb+w*h,tb+w*h+w*h/4},{w,w/2,w/2},{w,w/2,w/2},{h,h/2,h/2}};
  Pic R={w,h,{rb,rb+w*h,rb+w*h+w*h/4},{w,w/2,w/2},{w,w/2,w/2},{h,h/2,h/2}};
  int prevq=0, lastI=0, ok=1;
  for(int nfr=0;nfr<n;nfr++){
    int isI=types[nfr]=='I';
    if(!isI&&prevq==0){ ok=0; break; }
    memcpy(tb,frames+(size_t)nfr*fs,fs); memset(rb,0,fs);
    if(isI) lastI=nfr;
    Pic*refs[6]; int avail=nfr-lastI; int nref=avail<maxref?avail:maxref;
    for(int r=1;r<=nref;r++) refs[r]=&RING[(nfr-r)%6];
    size_t len=encode_frame(&T,&R,refs,nref,q,out,cap,isI,prevq,isI?words[nfr]:0);
    pk[nfr]=malloc(len); memcpy(pk[nfr],out,len); pklen[nfr]=(uint32_t)len;
    if(recon) memcpy(recon+(size_t)nfr*fs,rb,fs);
    memcpy(rbuf[nfr%6],rb,fs); prevq=q;
  }
  for(int i=0;i<6;i++) free(rbuf[i]);
  free(tb);free(rb);free(out);
  return ok;
}
