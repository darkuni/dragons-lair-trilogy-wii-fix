/* dev test: decode every frame of a .mo and write raw yuv420p (compare with ffmpeg) */
#include "moc5.h"
#include "mobidec.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char**argv){
  FILE*f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET); uint8_t*d=malloc(n); if(fread(d,1,n,f)!=(size_t)n)return 1; fclose(f);
  moc5 m; if(!moc5_parse(&m,d,n)){printf("parse fail\n");return 1;}
  mobidec*dec=mobidec_new(m.width,m.height); FILE*o=fopen(argv[2],"wb"); size_t fs=m.width*m.height*3/2; int bad=0;
  for(uint32_t i=0;i<m.nframes;i++){ const uint8_t*y; if(!mobidec_decode(dec,moc5_video(&m,i),m.fr[i].vsize,&y)){bad++;continue;} fwrite(y,1,fs,o);}
  fclose(o); printf("%s: %u frames, %d decode errors\n",argv[1],m.nframes,bad); return 0;}
