/* dev test: partition open, file read vs extracted copy, MOC5 parse/rebuild identity, group rebuild identity */
#include "wiidisc.h"
#include "moc5.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char**argv){
  if(argc<3){printf("usage: t_disc iso extracted_dir\n");return 1;}
  wii_part p; char err[256];
  if(!wii_open(&p,argv[1],err,sizeof err)){printf("open: %s\n",err);return 1;}
  printf("game %s files %d fst@%llx size %u data_size %llx\n",p.game_id,p.nfiles,(unsigned long long)p.fst_off,p.fst_size,(unsigned long long)p.data_size);
  int ok=0,bad=0,mo=0,mobad=0;
  for(int i=0;i<p.nfiles;i++){
    const wii_file*wf=&p.files[i];
    if(!strstr(wf->path,"SAScene1")&&!strstr(wf->path,"SAGame")) continue;
    uint8_t*d; if(!wii_read_file(&p,wf,&d)){bad++;continue;}
    char fn[512]; snprintf(fn,sizeof fn,"%s/%s",argv[2],wf->path); FILE*f=fopen(fn,"rb");
    uint8_t*e=malloc(wf->size); size_t got=f?fread(e,1,wf->size,f):0; if(f)fclose(f);
    if(got==wf->size&&!memcmp(d,e,wf->size))ok++; else bad++;
    if(strstr(wf->path,".mo")){ moc5 m; mo++;
      if(!moc5_parse(&m,d,wf->size)){mobad++;} else { size_t L; uint8_t*b=moc5_build(&m,0,0,NULL,NULL,&L); if(!b||L!=wf->size||memcmp(b,d,L))mobad++; free(b); moc5_free(&m);} }
    free(d);free(e);
  }
  printf("file reads match extracted: %d ok, %d bad | moc5 rebuild identity: %d/%d ok\n",ok,bad,mo-mobad,mo);
  uint8_t*hb=malloc(64*0x400),*data=malloc(WII_GROUP_DATA),*out=malloc(WII_GROUP_ENC),*orig=malloc(WII_GROUP_ENC),h2[0xA0];
  int gok=0; uint32_t gs[]={0,1,777,2000};
  for(int k=0;k<4;k++){ wii_read_group(&p,p.f,gs[k],hb,data); wii_build_group(&p.key,hb,data,out,h2);
    dlt_fseek(p.f,p.data_off+(uint64_t)gs[k]*WII_GROUP_ENC); fread(orig,1,WII_GROUP_ENC,p.f); gok+=!memcmp(out,orig,WII_GROUP_ENC); }
  printf("group rebuild byte-identical: %d/4\n",gok);
  return 0;}
