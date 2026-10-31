/* dev test: decode frames [a,b] of a .mo, restore colour, write yuv420p */
#include "moc5.h"
#include "mobidec.h"
#include "color.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char**argv){
  int a=atoi(argv[3]),b=atoi(argv[4]),cl=argc>5?atoi(argv[5]):-1;
  FILE*f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET); uint8_t*d=malloc(n); if(fread(d,1,n,f)!=(size_t)n)return 1; fclose(f);
  moc5 m; moc5_parse(&m,d,n); mobidec*dec=mobidec_new(m.width,m.height); color_init();
  size_t fs=m.width*m.height*3/2; uint8_t*o=malloc(fs); FILE*out=fopen(argv[2],"wb");
  for(int i=0;i<=b;i++){ const uint8_t*y; mobidec_decode(dec,moc5_video(&m,i),m.fr[i].vsize,&y); if(i>=a){ color_restore_frame(y,o,m.width,m.height,cl); fwrite(o,1,fs,out);} }
  fclose(out); return 0;}
