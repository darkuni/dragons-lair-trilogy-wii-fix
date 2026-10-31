/* dltfix - restores the red "ENERGIZE" sequences of Space Ace in Dragon's Lair Trilogy (Wii, SL3ENR).
 * Takes the player's own disc image and writes a patched copy. No game content is shipped with this tool:
 * every restored frame is generated from the player's disc.
 *
 * Stages (all resumable through a work folder):
 *   1. video   decode each blue energize segment, restore red, re-encode, rebuild the .mo file
 *   2. iso     copy the image, write patched files in original disc order, rehash, fakesign
 *   3. verify  re-read everything from the new image and check it                                  */
#include "crypto.h"
#include "wiidisc.h"
#include "moc5.h"
#include "mobidec.h"
#include "mobienc.h"
#include "color.h"
#include "segments.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#define RMDIR(p) _rmdir(p)
#else
#define MKDIR(p) mkdir(p,0755)
#define RMDIR(p) rmdir(p)
#endif

#define DLT_VERSION "1.0.0"
#define EXIT_INCOMPLETE 3

/* reference build: SHA-1 of every patched file (filled from the reference run; empty = unknown) */
typedef struct { const char *name; const char *sha1; } refhash;
static const refhash REF_OUT[] = {
#include "refhashes.inc"
    {NULL,NULL}};

/* ------------------------------------------------------------------ utilities */
static char g_work[1024];
static time_t g_start; static long g_budget;          /* seconds, 0 = unlimited */
static pthread_mutex_t g_log=PTHREAD_MUTEX_INITIALIZER;
static int over_budget(void){ return g_budget>0 && (long)(time(NULL)-g_start)>=g_budget; }
static void logf_(const char *fmt, ...) __attribute__((format(printf,1,2)));
#include <stdarg.h>
static void logf_(const char *fmt, ...){ va_list a; va_start(a,fmt); pthread_mutex_lock(&g_log); vprintf(fmt,a); fflush(stdout); pthread_mutex_unlock(&g_log); va_end(a); }
static void die(const char *fmt, ...){ va_list a; va_start(a,fmt); fprintf(stderr,"\nERROR: "); vfprintf(stderr,fmt,a); fprintf(stderr,"\n"); va_end(a); exit(1); }

static int file_exists(const char *p){ struct stat s; return stat(p,&s)==0; }
static uint8_t *read_all(const char *p, size_t *len){
    FILE *f=fopen(p,"rb"); if(!f) return NULL;
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t *d=(uint8_t*)malloc(n>0?(size_t)n:1); if(!d){fclose(f);return NULL;}
    if (fread(d,1,(size_t)n,f)!=(size_t)n){ fclose(f); free(d); return NULL; }
    fclose(f); *len=(size_t)n; return d;
}
static int write_all(const char *p, const void *d, size_t n){
    char tmp[1100]; snprintf(tmp,sizeof tmp,"%s.tmp",p);
    FILE *f=fopen(tmp,"wb"); if(!f) return 0;
    int ok=fwrite(d,1,n,f)==n; ok&=fclose(f)==0;
    if (!ok) return 0;
    remove(p); return rename(tmp,p)==0;
}
static void workpath(char *out, size_t n, const char *name){ snprintf(out,n,"%s/%s",g_work,name); }
static int read_text(const char *name, char *buf, size_t n){
    char p[1100]; workpath(p,sizeof p,name); size_t len; uint8_t *d=read_all(p,&len);
    if(!d) return 0; if(len>=n) len=n-1; memcpy(buf,d,len); buf[len]=0; free(d); return 1;
}
static int write_text(const char *name, const char *s){ char p[1100]; workpath(p,sizeof p,name); return write_all(p,s,strlen(s)); }

/* ------------------------------------------------------------------ stage 1: video */
static const char *g_input;
static int g_next_job; static pthread_mutex_t g_jobs=PTHREAD_MUTEX_INITIALIZER;
static int g_failed;

static uint32_t iframe_q(const uint8_t *pkt){ uint32_t v=((uint32_t)pkt[1]<<24)|((uint32_t)pkt[0]<<16)|((uint32_t)pkt[3]<<8)|pkt[2]; return (v>>23)&63; }
static uint32_t bits_at(const uint8_t *pkt, int size, int pos){   /* 32 bits at bit pos of the 16-bit-swapped stream */
    uint32_t v=0;
    for (int i=0;i<32;i++,pos++){
        int byte=pos>>3, sb=byte^1;   /* swapped byte index */
        int bit = sb<size ? (pkt[sb]>>(7-(pos&7)))&1 : 0;
        v=(v<<1)|(uint32_t)bit;
    }
    return v;
}
static int cmp_u32(const void *a,const void *b){ uint32_t x=*(const uint32_t*)a,y=*(const uint32_t*)b; return x<y?-1:x>y; }

static int seg_done(const dlt_segment *sg){
    char p[1100], s[64]; snprintf(p,sizeof p,"%s.sha1",sg->name);
    if (!read_text(p,s,sizeof s)) return 0;
    char mo[1100]; snprintf(mo,sizeof mo,"%s/%s.mo",g_work,sg->name);
    return file_exists(mo);
}

static int process_segment(wii_part *p, const dlt_segment *sg){
    char path[256]; snprintf(path,sizeof path,"Content16/%s.mo",sg->name);
    const wii_file *wf=wii_find(p,path); if(!wf) return 0;
    uint8_t *orig; if(!wii_read_file(p,wf,&orig)) return 0;
    uint8_t *outbuf=NULL; size_t outlen=0;
    time_t t0=time(NULL);
    if (!strcmp(sg->name,"SAScene1")){
        /* the developers' red original of this scene ships unused on the disc: use it whole */
        const wii_file *of=wii_find(p,"Content16/SAScene1_original.mo"); if(!of){ free(orig); return 0; }
        if(!wii_read_file(p,of,&outbuf)){ free(orig); return 0; }
        outlen=of->size;
        logf_("  %-11s replaced by the unused red original on the disc\n",sg->name);
    } else {
        moc5 m; if(!moc5_parse(&m,orig,wf->size)){ free(orig); return 0; }
        int w=(int)m.width,h=(int)m.height, n=sg->last-sg->first+1; size_t fs=(size_t)w*h*3/2;
        uint8_t *src=(uint8_t*)malloc(fs), *tgt=(uint8_t*)malloc(fs*(size_t)n);
        char *types=(char*)malloc((size_t)n+1); uint32_t *words=(uint32_t*)calloc((size_t)n,4), *iq=(uint32_t*)malloc(4*(size_t)n); int niq=0;
        mobidec *dec=mobidec_new(w,h); int ok=dec!=NULL && moc5_is_key(&m,(uint32_t)sg->first);
        for (int k=0;k<n && ok;k++){
            uint32_t i=(uint32_t)(sg->first+k); const uint8_t *pkt=moc5_video(&m,i); int vs=(int)m.fr[i].vsize;
            const uint8_t *yuv; if(!mobidec_decode(dec,pkt,vs,&yuv)){ ok=0; break; }
            types[k]=moc5_is_key(&m,i)?'I':'P';
            if (types[k]=='I'){
                int end=mobidec_iframe_end_bits(dec); int a=end+((16-end%16)%16);
                words[k]=bits_at(pkt,vs,a); iq[niq++]=iframe_q(pkt);
            }
            memcpy(src,yuv,fs);
            color_restore_frame(src,tgt+fs*(size_t)k,w,h,sg->clamp);
        }
        types[n]=0;
        if (ok && niq){
            qsort(iq,(size_t)niq,4,cmp_u32);
            int q = niq%2 ? (int)iq[niq/2] : (int)((iq[niq/2-1]+iq[niq/2])/2);
            uint8_t **pk=(uint8_t**)calloc((size_t)n,sizeof(uint8_t*)); uint32_t *pl=(uint32_t*)calloc((size_t)n,4);
            int attempt; uint64_t total=0; uint32_t maxf=0;
            for (attempt=0;attempt<8;attempt++){
                if(!mobienc_encode(tgt,n,w,h,types,words,q,sg->maxref,sg->intra,pk,pl,NULL)){ ok=0; break; }
                int big=0; total=0; maxf=0;
                for (int k=0;k<n;k++){ total+=pl[k]; if(pl[k]>maxf)maxf=pl[k]; if(pl[k]+8+600>m.max_frame) big=1; }
                if (!big) break;
                for (int k=0;k<n;k++){ free(pk[k]); pk[k]=NULL; }
                q++;
            }
            if (ok && attempt<8){
                outbuf=moc5_build(&m,(uint32_t)sg->first,(uint32_t)n,pk,pl,&outlen);
                if(!outbuf) ok=0;
                uint64_t orig_seg=0; for(int k=0;k<n;k++) orig_seg+=m.fr[sg->first+k].vsize;
                logf_("  %-11s %3d frames  q%-2d  data x%.2f  largest frame %u KB  (%ds)\n",sg->name,n,q,(double)total/(double)orig_seg,maxf/1024,(int)(time(NULL)-t0));
            } else ok=0;
            for (int k=0;k<n;k++) free(pk[k]);
            free(pk); free(pl);
        } else ok=0;
        mobidec_free(dec); free(src); free(tgt); free(types); free(words); free(iq); moc5_free(&m);
        if(!ok){ free(orig); free(outbuf); return 0; }
    }
    char mo[1100], name[300]; snprintf(mo,sizeof mo,"%s/%s.mo",g_work,sg->name);
    uint8_t d[20]; char hx[41]; sha1(outbuf,outlen,d); sha1_hex(d,hx);
    int ok=write_all(mo,outbuf,outlen);
    snprintf(name,sizeof name,"%s.sha1",sg->name); ok&=write_text(name,hx);
    free(outbuf); free(orig);
    return ok;
}

static void *video_worker(void *arg){
    (void)arg;
    wii_part p; char err[256];
    if(!wii_open(&p,g_input,err,sizeof err)){ logf_("worker: %s\n",err); g_failed=1; return NULL; }
    for(;;){
        if (over_budget()) break;
        pthread_mutex_lock(&g_jobs); int j=g_next_job++; pthread_mutex_unlock(&g_jobs);
        if (j>=dlt_nsegments) break;
        const dlt_segment *sg=&dlt_segments[j];
        if (seg_done(sg)) continue;
        if(!process_segment(&p,sg)){ logf_("  %-11s FAILED\n",sg->name); g_failed=1; }
    }
    wii_close(&p); return NULL;
}

/* ------------------------------------------------------------------ stage 2: iso */
typedef struct { const wii_file *f; uint64_t noff; uint32_t nsize; int patched; FILE *src; } placed;
static int cmp_placed_new(const void *a,const void *b){ const placed *x=(const placed*)a,*y=(const placed*)b; return x->noff<y->noff?-1:x->noff>y->noff; }
static int cmp_files_off(const void *a,const void *b){ const wii_file *x=*(wii_file*const*)a,*y=*(wii_file*const*)b; return x->off<y->off?-1:x->off>y->off; }

static int stage_iso(const char *outpath){
    wii_part p; char err[256];
    if(!wii_open(&p,g_input,err,sizeof err)) die("%s",err);
    int nf=p.nfiles;
    /* new sizes */
    uint32_t *nsize=(uint32_t*)malloc(sizeof(uint32_t)*(size_t)nf); int *patched=(int*)calloc((size_t)nf,sizeof(int));
    for (int i=0;i<nf;i++){
        nsize[i]=p.files[i].size;
        for (int s=0;s<dlt_nsegments;s++){
            char path[256]; snprintf(path,sizeof path,"Content16/%s.mo",dlt_segments[s].name);
            if (!strcmp(p.files[i].path,path)){
                char mo[1100]; snprintf(mo,sizeof mo,"%s/%s.mo",g_work,dlt_segments[s].name);
                struct stat st; if(stat(mo,&st)) die("missing %s",mo);
                nsize[i]=(uint32_t)st.st_size; patched[i]=1;
            }
        }
    }
    /* layout: keep disc order, pack from the end backwards; files move only as far as growth requires */
    const wii_file **ord=(const wii_file**)malloc(sizeof(void*)*(size_t)nf);
    for (int i=0;i<nf;i++) ord[i]=&p.files[i];
    qsort(ord,(size_t)nf,sizeof(void*),cmp_files_off);
    uint64_t nxt=0; for (int i=0;i<nf;i++){ uint64_t e=ord[i]->off+ord[i]->size; if(e>nxt) nxt=e; }
    uint64_t *noff=(uint64_t*)malloc(sizeof(uint64_t)*(size_t)nf);
    uint64_t maxshift=0;
    for (int k=nf-1;k>=0;k--){
        int i=(int)(ord[k]-p.files); uint64_t want=(nxt-nsize[i])&~(uint64_t)3;
        noff[i]= ord[k]->off<want ? ord[k]->off : want; nxt=noff[i];
        if (ord[k]->off-noff[i]>maxshift) maxshift=ord[k]->off-noff[i];
    }
    if (nxt<=p.fst_off+p.fst_size) die("patched files do not fit on the disc");
    /* new FST */
    uint8_t *fst=(uint8_t*)malloc(p.fst_size); memcpy(fst,p.fst,p.fst_size);
    int nplaced=0; placed *pl=(placed*)calloc((size_t)nf,sizeof(placed));
    for (int i=0;i<nf;i++){
        uint8_t *e=fst+12*p.files[i].entry; uint32_t o=(uint32_t)(noff[i]>>2), s=nsize[i];
        e[4]=(uint8_t)(o>>24);e[5]=(uint8_t)(o>>16);e[6]=(uint8_t)(o>>8);e[7]=(uint8_t)o;
        e[8]=(uint8_t)(s>>24);e[9]=(uint8_t)(s>>16);e[10]=(uint8_t)(s>>8);e[11]=(uint8_t)s;
        if (noff[i]!=p.files[i].off||nsize[i]!=p.files[i].size||patched[i]){
            placed *q=&pl[nplaced++]; q->f=&p.files[i]; q->noff=noff[i]; q->nsize=nsize[i]; q->patched=patched[i];
            if (patched[i]){ char mo[1100]; const char *nm=strrchr(p.files[i].path,'/')+1; snprintf(mo,sizeof mo,"%s/%s",g_work,nm); q->src=fopen(mo,"rb"); if(!q->src) die("cannot open %s",mo); }
        }
    }
    qsort(pl,(size_t)nplaced,sizeof(placed),cmp_placed_new);
    /* groups to rewrite */
    uint32_t ngroups_disc=(uint32_t)(p.data_size/WII_GROUP_ENC);
    uint8_t *need=(uint8_t*)calloc(ngroups_disc+1,1);
    for (int k=0;k<nplaced;k++) if (pl[k].nsize) for (uint64_t g=pl[k].noff/WII_GROUP_DATA; g<=(pl[k].noff+pl[k].nsize-1)/WII_GROUP_DATA; g++) need[g]=1;
    for (uint64_t g=p.fst_off/WII_GROUP_DATA; g<=(p.fst_off+p.fst_size-1)/WII_GROUP_DATA; g++) need[g]=1;
    uint32_t *plan=(uint32_t*)malloc(sizeof(uint32_t)*(ngroups_disc+1)); uint32_t nplan=0;
    for (uint32_t g=0;g<=ngroups_disc;g++) if (need[g]) plan[nplan++]=g;
    char st[256];
    if (!read_text("iso.plan",st,sizeof st)){
        snprintf(st,sizeof st,"%u %llu",nplan,(unsigned long long)nxt); write_text("iso.plan",st);
        logf_("  layout: %d files keep disc order; largest shift %.1f KB; %u groups to rewrite\n",nplaced,maxshift/1024.0,nplan);
    } else { unsigned a; unsigned long long b; if(sscanf(st,"%u %llu",&a,&b)!=2||a!=nplan||b!=nxt) die("work folder belongs to a different run - delete it and start again"); }

    /* copy the input image (resumable) */
    FILE *in=p.f; uint64_t total; dlt_fseek(in,0);
#ifdef _WIN32
    _fseeki64(in,0,SEEK_END); total=(uint64_t)_ftelli64(in);
#else
    fseeko(in,0,SEEK_END); total=(uint64_t)ftello(in);
#endif
    uint64_t copied=0; if (read_text("iso.copied",st,sizeof st)) copied=strtoull(st,NULL,10);
    FILE *out=fopen(outpath, copied?"r+b":"wb"); if(!out) die("cannot write %s",outpath);
    if (copied<total){
        logf_("  copying disc image%s...\n",copied?" (resuming)":"");
        size_t CH=16u<<20; uint8_t *buf=(uint8_t*)malloc(CH);
        while (copied<total){
            if (over_budget()){ snprintf(st,sizeof st,"%llu",(unsigned long long)copied); write_text("iso.copied",st); fclose(out); return 0; }
            size_t take=(size_t)((total-copied)<CH?(total-copied):CH);
            dlt_fseek(in,copied); if(fread(buf,1,take,in)!=take) die("read error");
            dlt_fseek(out,copied); if(fwrite(buf,1,take,out)!=take) die("write error (disk full?)");
            copied+=take;
        }
        free(buf); fflush(out);
        snprintf(st,sizeof st,"%llu",(unsigned long long)copied); write_text("iso.copied",st);
    }
    /* rewrite groups (resumable) */
    uint32_t done=0; if (read_text("iso.groups",st,sizeof st)) done=(uint32_t)strtoul(st,NULL,10);
    char h3p[1100]; workpath(h3p,sizeof h3p,"iso.h3");
    FILE *h3f=fopen(h3p, done?"r+b":"wb"); if(!h3f) die("cannot write %s",h3p);
    uint8_t *hb=(uint8_t*)malloc(64*0x400), *data=(uint8_t*)malloc(WII_GROUP_DATA), *enc=(uint8_t*)malloc(WII_GROUP_ENC), h2[0xA0];
    if (done<nplan) logf_("  rewriting %u groups%s...\n",nplan-done,done?" (resuming)":"");
    int first_idx=0;
    while (done<nplan){
        if (over_budget()){ fclose(h3f); snprintf(st,sizeof st,"%u",done); write_text("iso.groups",st); fclose(out); return 0; }
        uint32_t g=plan[done]; uint64_t ga=(uint64_t)g*WII_GROUP_DATA, gb=ga+WII_GROUP_DATA;
        if(!wii_read_group(&p,in,g,hb,data)) die("read error at group %u",g);
        if (p.fst_off<gb && p.fst_off+p.fst_size>ga){
            uint64_t lo=p.fst_off>ga?p.fst_off:ga, hi=p.fst_off+p.fst_size<gb?p.fst_off+p.fst_size:gb;
            memcpy(data+(lo-ga),fst+(lo-p.fst_off),(size_t)(hi-lo));
        }
        while (first_idx<nplaced && pl[first_idx].noff+pl[first_idx].nsize<=ga) first_idx++;
        for (int k=first_idx;k<nplaced && pl[k].noff<gb;k++){
            uint64_t a=pl[k].noff, b=a+pl[k].nsize; uint64_t lo=a>ga?a:ga, hi=b<gb?b:gb; if (lo>=hi) continue;
            if (pl[k].patched){ dlt_fseek(pl[k].src,lo-a); if(fread(data+(lo-ga),1,(size_t)(hi-lo),pl[k].src)!=hi-lo) die("read error (work file)"); }
            else if(!wii_read(&p,pl[k].f->off+(lo-a),data+(lo-ga),(size_t)(hi-lo))) die("read error (input)");
        }
        wii_build_group(&p.key,hb,data,enc,h2);
        dlt_fseek(out,p.data_off+(uint64_t)g*WII_GROUP_ENC); if(fwrite(enc,1,WII_GROUP_ENC,out)!=WII_GROUP_ENC) die("write error");
        uint8_t h3e[20]; sha1(h2,0xA0,h3e); dlt_fseek(h3f,(uint64_t)done*20); fwrite(h3e,1,20,h3f);
        done++;
        if (done%100==0){ fflush(h3f); fflush(out); snprintf(st,sizeof st,"%u",done); write_text("iso.groups",st); logf_("    %u / %u\n",done,nplan); }
    }
    fclose(h3f); snprintf(st,sizeof st,"%u",done); write_text("iso.groups",st);
    /* H3 table + TMD */
    uint8_t *h3=(uint8_t*)malloc(0x18000); dlt_fseek(in,p.h3_off); if(fread(h3,1,0x18000,in)!=0x18000) die("H3 read");
    FILE *hf=fopen(h3p,"rb"); for (uint32_t k=0;k<nplan;k++){ if(fread(h3+plan[k]*20,1,20,hf)!=20) die("h3 state"); } fclose(hf);
    dlt_fseek(out,p.h3_off); fwrite(h3,1,0x18000,out);
    uint8_t *tmd=(uint8_t*)malloc(p.tmd_size); dlt_fseek(in,p.tmd_off); if(fread(tmd,1,p.tmd_size,in)!=p.tmd_size) die("TMD read");
    sha1(h3,0x18000,tmd+0x1E4+16);
    if (!wii_fakesign_tmd(tmd,p.tmd_size)) die("fakesign failed");
    dlt_fseek(out,p.tmd_off); fwrite(tmd,1,p.tmd_size,out);
    if (fclose(out)) die("write error (disk full?)");
    write_text("iso.done","1");
    for (int k=0;k<nplaced;k++) if(pl[k].src) fclose(pl[k].src);
    free(h3); free(tmd); free(hb); free(data); free(enc); free(plan); free(need); free(pl); free(fst); free(noff); free(ord); free(nsize); free(patched);
    wii_close(&p);
    return 1;
}

/* ------------------------------------------------------------------ stage 3: verify */
static int stage_verify(const char *outpath){
    wii_part o, in; char err[256];
    if(!wii_open(&o,outpath,err,sizeof err)) die("output image unreadable: %s",err);
    if(!wii_open(&in,g_input,err,sizeof err)) die("%s",err);
    int bad=0, refmatch=0, refknown=0;
    if (o.nfiles!=in.nfiles){ logf_("  file count differs!\n"); bad++; }
    /* every patched file reads back exactly as produced */
    for (int s=0;s<dlt_nsegments;s++){
        char path[256], name[300], want[64]; snprintf(path,sizeof path,"Content16/%s.mo",dlt_segments[s].name);
        snprintf(name,sizeof name,"%s.sha1",dlt_segments[s].name); if(!read_text(name,want,sizeof want)){ bad++; continue; }
        const wii_file *wf=wii_find(&o,path); uint8_t *d; uint8_t h[20]; char hx[41];
        if(!wf||!wii_read_file(&o,wf,&d)){ bad++; continue; }
        sha1(d,wf->size,h); sha1_hex(h,hx); free(d);
        if (strcmp(hx,want)){ logf_("  %s does not read back correctly!\n",path); bad++; }
        for (const refhash *r=REF_OUT;r->name;r++) if(!strcmp(r->name,dlt_segments[s].name)){ refknown++; refmatch+=!strcmp(r->sha1,hx); }
    }
    char st[64]; int fstart=0; if (read_text("verify.files",st,sizeof st)) fstart=atoi(st);
    /* every other file is unchanged */
    for (int i=fstart;i<in.nfiles;i++){
        if (over_budget()){ snprintf(st,sizeof st,"%d",i); write_text("verify.files",st); wii_close(&o); wii_close(&in); return -1; }
        int seg=0; for (int s=0;s<dlt_nsegments;s++){ char path[256]; snprintf(path,sizeof path,"Content16/%s.mo",dlt_segments[s].name); if(!strcmp(path,in.files[i].path)) seg=1; }
        if (seg) continue;
        const wii_file *wf=wii_find(&o,in.files[i].path); uint8_t *a,*b;
        if(!wf||wf->size!=in.files[i].size){ bad++; continue; }
        if(!wii_read_file(&in,&in.files[i],&a)||!wii_read_file(&o,wf,&b)){ bad++; continue; }
        if (memcmp(a,b,wf->size)){ logf_("  %s changed unexpectedly!\n",wf->path); bad++; }
        free(a); free(b);
    }
    /* hash tree: TMD -> H3 -> every group's H2/H1/H0 */
    uint8_t *h3=(uint8_t*)malloc(0x18000), *tmd=(uint8_t*)malloc(o.tmd_size), d[20];
    dlt_fseek(o.f,o.h3_off); if(fread(h3,1,0x18000,o.f)!=0x18000) bad++;
    dlt_fseek(o.f,o.tmd_off); if(fread(tmd,1,o.tmd_size,o.f)!=o.tmd_size) bad++;
    sha1(h3,0x18000,d); if (memcmp(d,tmd+0x1E4+16,20)){ logf_("  TMD hash mismatch!\n"); bad++; }
    sha1(tmd+0x140,o.tmd_size-0x140,d); if (d[0]){ logf_("  TMD not signed!\n"); bad++; }
    uint8_t *hb=(uint8_t*)malloc(64*0x400), *data=(uint8_t*)malloc(WII_GROUP_DATA);
    snprintf(st,sizeof st,"%d",in.nfiles); write_text("verify.files",st);
    uint32_t ng=(uint32_t)(o.data_size/WII_GROUP_ENC), gbad=0, gstart=0;
    if (read_text("verify.groups",st,sizeof st)){ unsigned a,b; if(sscanf(st,"%u %u",&a,&b)==2){ gstart=a; gbad=b; } }
    for (uint32_t g=gstart;g<ng;g++){
        if (over_budget()){ snprintf(st,sizeof st,"%u %u",g,gbad); write_text("verify.groups",st); free(h3); free(tmd); free(hb); free(data); wii_close(&o); wii_close(&in); return -1; }
        if(!wii_read_group(&o,o.f,g,hb,data)){ gbad++; continue; }
        int okg=1;
        for (int c=0;c<64&&okg;c++){
            uint8_t *b=hb+c*0x400, x[20];
            for (int i=0;i<31&&okg;i++){ sha1(data+c*WII_CLUSTER_DATA+i*0x400,0x400,x); okg&=!memcmp(x,b+i*20,20); }
            sha1(b,0x26C,x); okg&=!memcmp(x,b+0x280+(c%8)*20,20);
            sha1(b+0x280,0xA0,x); okg&=!memcmp(x,b+0x340+(c/8)*20,20);
            okg&=!memcmp(b+0x340,hb+0x340,0xA0);
        }
        sha1(hb+0x340,0xA0,d); okg&=!memcmp(d,h3+g*20,20);
        if(!okg) gbad++;
        if (g%400==0) logf_("    hash check %u / %u\n",g,ng);
    }
    if (gbad){ logf_("  %u groups fail the hash check!\n",gbad); bad++; }
    free(h3); free(tmd); free(hb); free(data); wii_close(&o); wii_close(&in);
    if (refknown) logf_("  reference check: %d / %d patched files identical to the reference build\n",refmatch,refknown);
    return bad==0;
}

/* ------------------------------------------------------------------ main */
static void usage(void){
    printf("dltfix " DLT_VERSION " - restore the red ENERGIZE scenes in Dragon's Lair Trilogy (Wii)\n\n"
           "usage: dltfix <your SL3ENR disc image.iso> [output.iso] [--threads N] [--work DIR] [--keep-work]\n"
           "       (you can also drag the .iso onto dltfix.exe)\n");
}
int main(int argc, char **argv){
    const char *in=NULL,*out=NULL,*work=NULL; int threads=0, keep=0;
    for (int i=1;i<argc;i++){
        if (!strcmp(argv[i],"--threads")&&i+1<argc) threads=atoi(argv[++i]);
        else if (!strcmp(argv[i],"--work")&&i+1<argc) work=argv[++i];
        else if (!strcmp(argv[i],"--max-seconds")&&i+1<argc) g_budget=atol(argv[++i]);
        else if (!strcmp(argv[i],"--keep-work")) keep=1;
        else if (argv[i][0]=='-'){ usage(); return 1; }
        else if (!in) in=argv[i]; else if (!out) out=argv[i];
    }
    int dropped = argc==2;
    if (!in){ usage(); return 1; }
    g_start=time(NULL); g_input=in;
    printf("dltfix " DLT_VERSION "\n");
    if (!crypto_selftest()) die("crypto self-test failed");
    char outbuf[1024], workbuf[1100];
    if (!out){ snprintf(outbuf,sizeof outbuf,"%s",in); char *dot=strrchr(outbuf,'.'); if(dot&&(dot>strrchr(outbuf,'/'))&&(dot>strrchr(outbuf,'\\'))) *dot=0;
               strncat(outbuf,"_red_energize.iso",sizeof outbuf-strlen(outbuf)-1); out=outbuf; }
    if (!work){ snprintf(workbuf,sizeof workbuf,"%s.work",out); work=workbuf; }
    snprintf(g_work,sizeof g_work,"%s",work); MKDIR(g_work);
    if (threads<=0){ threads=4;
#ifdef _WIN32
        const char *np=getenv("NUMBER_OF_PROCESSORS"); if(np&&atoi(np)>0) threads=atoi(np);
#else
        long n=sysconf(_SC_NPROCESSORS_ONLN); if(n>0) threads=(int)n;
#endif
        if (threads>32) threads=32; }

    /* check the input */
    wii_part p; char err[256];
    if(!wii_open(&p,in,err,sizeof err)) die("%s",err);
    if (strcmp(p.game_id,"SL3ENR")) die("this is %s - dltfix needs Dragon's Lair Trilogy (USA) SL3ENR",p.game_id);
    printf("input:  %s (%s)\noutput: %s\n",in,p.game_id,out);
    const dlt_segment *chk[64]; int nchk=0; for(int s=0;s<dlt_nsegments;s++) chk[nchk++]=&dlt_segments[s]; chk[nchk++]=&dlt_sa1_original;
    if (!file_exists(g_work) || !read_text("input.ok",err,sizeof err)){
        printf("checking the disc...\n");
        for (int k=0;k<nchk;k++){
            char path[256]; snprintf(path,sizeof path,"Content16/%s.mo",chk[k]->name);
            const wii_file *wf=wii_find(&p,path); uint8_t *d; uint8_t h[20]; char hx[41];
            if(!wf) die("%s is missing - not a complete dump of this game",path);
            if(!wii_read_file(&p,wf,&d)) die("cannot read %s",path);
            sha1(d,wf->size,h); sha1_hex(h,hx); free(d);
            if (strcmp(hx,chk[k]->sha1)) die("%s does not match the retail disc (already patched, different revision, or bad dump)",path);
        }
        write_text("input.ok","1");
    }
    wii_close(&p);

    /* stage 1 */
    mobienc_init(); color_init();
    int todo=0; for (int s=0;s<dlt_nsegments;s++) todo+=!seg_done(&dlt_segments[s]);
    if (todo){
        printf("restoring %d energize segments with %d threads...\n",todo,threads);
        pthread_t th[32]; int nt=threads<todo?threads:todo;
        for (int i=0;i<nt;i++) pthread_create(&th[i],NULL,video_worker,NULL);
        for (int i=0;i<nt;i++) pthread_join(th[i],NULL);
        if (g_failed) die("video restoration failed");
        for (int s=0;s<dlt_nsegments;s++) if(!seg_done(&dlt_segments[s])){ printf("stopped (time budget) - run again to continue\n"); return EXIT_INCOMPLETE; }
    }
    /* stage 2 */
    if (!read_text("iso.done",err,sizeof err)){
        printf("building the patched disc image...\n");
        if (!stage_iso(out)){ printf("stopped (time budget) - run again to continue\n"); return EXIT_INCOMPLETE; }
    }
    /* stage 3 */
    printf("verifying...\n");
    { int v=stage_verify(out); if (v<0){ printf("stopped (time budget) - run again to continue\n"); return EXIT_INCOMPLETE; }
      if (!v) die("verification FAILED - do not use %s",out); }
    printf("\nDone: %s\n(%ld s)\n",out,(long)(time(NULL)-g_start));
    if (!keep){
        for (int s=0;s<dlt_nsegments;s++){ char a[1100]; snprintf(a,sizeof a,"%s/%s.mo",g_work,dlt_segments[s].name); remove(a); snprintf(a,sizeof a,"%s/%s.sha1",g_work,dlt_segments[s].name); remove(a); }
        const char *st[]={"iso.plan","iso.copied","iso.groups","iso.h3","iso.done","input.ok","verify.files","verify.groups"};
        for (int i=0;i<8;i++){ char a[1100]; workpath(a,sizeof a,st[i]); remove(a); }
        RMDIR(g_work);
    }
#ifdef _WIN32
    if (dropped){ printf("\nPress Enter to close."); getchar(); }
#else
    (void)dropped;
#endif
    return 0;
}
