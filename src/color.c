/* Energize color restoration.
 *  1. YUV420p -> RGB (BT.601 limited range, chroma replicated per 2x2)
 *  2. degree-4 polynomial RGB->RGB fitted on SAScene1 (shipped blue) vs SAScene1_original (red)
 *  3. optional palette clamp: hues outside -40..clamp_hi degrees are blended toward the red tint
 *  4. non-local-means denoise per RGB plane (patch 5x5, search 9x9, h = 25)
 *  5. RGB -> YUV420p
 * Deterministic: float math only with + - * /, no libm calls. */
#include "color.h"
#include <stdlib.h>
#include <string.h>

extern const uint32_t dlt_poly_w_bits[105];
extern const uint32_t dlt_palette_bits[768];
static float bitsf(uint32_t u){ float f; memcpy(&f,&u,4); return f; }

static uint8_t clip8f(float v){ if (v<=0.f) return 0; if (v>=255.f) return 255; return (uint8_t)(int)(v+0.5f); }
static int clipi(int v,int lo,int hi){ return v<lo?lo:v>hi?hi:v; }

static void yuv_to_rgb(const uint8_t *yuv, uint8_t *rgb, int w, int h){
    const uint8_t *Y=yuv, *U=yuv+w*h, *V=U+(w/2)*(h/2);
    for (int y=0;y<h;y++) for (int x=0;x<w;x++){
        float yy=1.164383f*((float)Y[y*w+x]-16.f), u=(float)U[(y/2)*(w/2)+x/2]-128.f, v=(float)V[(y/2)*(w/2)+x/2]-128.f;
        uint8_t *p=rgb+3*(y*w+x);
        p[0]=clip8f(yy+1.596027f*v); p[1]=clip8f(yy-0.391762f*u-0.812968f*v); p[2]=clip8f(yy+2.017232f*u);
    }
}
static void rgb_to_yuv(const uint8_t *rgb, uint8_t *yuv, int w, int h){
    uint8_t *Y=yuv, *U=yuv+w*h, *V=U+(w/2)*(h/2);
    for (int y=0;y<h;y++) for (int x=0;x<w;x++){
        const uint8_t *p=rgb+3*(y*w+x);
        Y[y*w+x]=clip8f(16.f+0.256788f*p[0]+0.504129f*p[1]+0.097906f*p[2]);
    }
    for (int y=0;y<h/2;y++) for (int x=0;x<w/2;x++){
        float r=0,g=0,b=0;
        for (int k=0;k<4;k++){ const uint8_t *p=rgb+3*((2*y+(k>>1))*w+2*x+(k&1)); r+=p[0]; g+=p[1]; b+=p[2]; }
        r*=0.25f; g*=0.25f; b*=0.25f;
        U[y*(w/2)+x]=clip8f(128.f-0.148223f*r-0.290993f*g+0.439216f*b);
        V[y*(w/2)+x]=clip8f(128.f+0.439216f*r-0.367788f*g-0.071427f*b);
    }
}

/* polynomial features in itertools.combinations_with_replacement order, degree 0..4 (35 terms) */
static void poly_apply(const uint8_t *in, uint8_t *out, int n, int clamp_hi){
    float W[35][3], CURVE[256][3];
    for (int i=0;i<35;i++) for (int c=0;c<3;c++) W[i][c]=bitsf(dlt_poly_w_bits[i*3+c]);
    for (int i=0;i<256;i++) for (int c=0;c<3;c++) CURVE[i][c]=bitsf(dlt_palette_bits[i*3+c])/255.f;
    for (int px=0;px<n;px++){
        float v[3]={in[3*px]/255.f, in[3*px+1]/255.f, in[3*px+2]/255.f}, f[35]; int k=0;
        f[k++]=1.f;
        for (int a=0;a<3;a++) f[k++]=v[a];
        for (int a=0;a<3;a++) for (int b=a;b<3;b++) f[k++]=v[a]*v[b];
        for (int a=0;a<3;a++) for (int b=a;b<3;b++) for (int c=b;c<3;c++) f[k++]=v[a]*v[b]*v[c];
        for (int a=0;a<3;a++) for (int b=a;b<3;b++) for (int c=b;c<3;c++) for (int d=c;d<3;d++) f[k++]=v[a]*v[b]*v[c]*v[d];
        float o[3];
        for (int c=0;c<3;c++){ float s=0.f; for (int i=0;i<35;i++) s+=f[i]*W[i][c]; o[c]=s<0.f?0.f:s>1.f?1.f:s; }
        if (clamp_hi>=0){
            float R=o[0],G=o[1],B=o[2], mx=R>G?(R>B?R:B):(G>B?G:B), mn=R<G?(R<B?R:B):(G<B?G:B), d=mx-mn+1e-6f, hh;
            if (mx==R){ hh=(G-B)/d; if (hh<0) hh+=6.f; } else if (mx==G) hh=(B-R)/d+2.f; else hh=(R-G)/d+4.f;
            hh*=60.f; if (hh>=180.f) hh-=360.f;                   /* -> [-180,180) */
            float s=(mx-mn)/(mx+1e-6f);
            float dist=hh-(float)clamp_hi; if (-40.f-hh>dist) dist=-40.f-hh; if (dist<0) dist=0;
            float wgt=dist/15.f; if (wgt>1.f) wgt=1.f;
            float ws=(s-0.10f)/0.15f; if (ws<0) ws=0; if (ws>1.f) ws=1.f; wgt*=ws;
            int L=(int)((0.299f*R+0.587f*G+0.114f*B)*255.f); L=clipi(L,0,255);
            for (int c=0;c<3;c++) o[c]=o[c]*(1.f-wgt)+CURVE[L][c]*wgt;
        }
        for (int c=0;c<3;c++){ float t=o[c]*255.f; out[3*px+c]=(uint8_t)(t<=0.f?0:t>=255.f?255:(int)(t+0.5f)); }
    }
}

/* deterministic exp(-x) for x >= 0 using range reduction and a Taylor polynomial */
static double dexp_neg(double x){
    int k=0; while (x>0.5){ x*=0.5; k++; }
    double t=-x, s=1.0, term=1.0;
    for (int i=1;i<=12;i++){ term*=t/i; s+=term; }
    while (k--) s*=s;
    return s;
}

/* weights: exp(-ssd / h^2), h = 25 (sigma 2.5 * 10), cut off below ~1/255 */
static float nlm_lut[3500];
void color_init(void){ for (int i=0;i<3500;i++) nlm_lut[i]=(float)dexp_neg(i/625.0); }
#define NLM_P 2   /* patch radius  -> 5x5 */
#define NLM_R 4   /* search radius -> 9x9 */
static void nlm_plane(const uint8_t *src, uint8_t *dst, int w, int h, int stride, int off, const float *lut, int lutn){
    float *wsum=(float*)malloc(sizeof(float)*(size_t)w*h), *vsum=(float*)malloc(sizeof(float)*(size_t)w*h);
    int64_t *ii=(int64_t*)malloc(sizeof(int64_t)*(size_t)(w+1)*(h+1));
    for (int i=0;i<w*h;i++){ wsum[i]=1.f; vsum[i]=(float)src[i*stride+off]; }   /* centre pixel weight 1 */
    for (int dy=-NLM_R;dy<=NLM_R;dy++) for (int dx=-NLM_R;dx<=NLM_R;dx++){
        if (!dx&&!dy) continue;
        /* integral image of squared differences against the shifted plane (edge-replicated) */
        for (int x=0;x<=w;x++) ii[x]=0;
        for (int y=0;y<h;y++){
            int64_t row=0; int sy=clipi(y+dy,0,h-1);
            ii[(size_t)(y+1)*(w+1)]=0;
            for (int x=0;x<w;x++){
                int sx=clipi(x+dx,0,w-1); int d=(int)src[(y*w+x)*stride+off]-(int)src[(sy*w+sx)*stride+off];
                row+=d*d; ii[(size_t)(y+1)*(w+1)+x+1]=ii[(size_t)y*(w+1)+x+1]+row;
            }
        }
        for (int y=0;y<h;y++){
            int y0=y-NLM_P<0?0:y-NLM_P, y1=y+NLM_P>=h?h-1:y+NLM_P; int sy=clipi(y+dy,0,h-1);
            for (int x=0;x<w;x++){
                int x0=x-NLM_P<0?0:x-NLM_P, x1=x+NLM_P>=w?w-1:x+NLM_P;
                int64_t ssd=ii[(size_t)(y1+1)*(w+1)+x1+1]-ii[(size_t)y0*(w+1)+x1+1]-ii[(size_t)(y1+1)*(w+1)+x0]+ii[(size_t)y0*(w+1)+x0];
                if (ssd>=lutn) continue;
                float wt=lut[ssd]; int sx=clipi(x+dx,0,w-1);
                wsum[y*w+x]+=wt; vsum[y*w+x]+=wt*(float)src[(sy*w+sx)*stride+off];
            }
        }
    }
    for (int i=0;i<w*h;i++){ float v=vsum[i]/wsum[i]; dst[i*stride+off]=(uint8_t)(v<=0.f?0:v>=255.f?255:(int)(v+0.5f)); }
    free(wsum); free(vsum); free(ii);
}

void color_restore_frame(const uint8_t *yuv_in, uint8_t *yuv_out, int w, int h, int clamp_hi){
    size_t n=(size_t)w*h;
    uint8_t *rgb=(uint8_t*)malloc(3*n), *red=(uint8_t*)malloc(3*n), *den=(uint8_t*)malloc(3*n);
    yuv_to_rgb(yuv_in,rgb,w,h);
    poly_apply(rgb,red,(int)n,clamp_hi);
    for (int c=0;c<3;c++) nlm_plane(red,den,w,h,3,c,nlm_lut,3500);
    rgb_to_yuv(den,yuv_out,w,h);
    free(rgb); free(red); free(den);
}
