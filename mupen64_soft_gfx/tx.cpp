/**
 * Mupen64 - tx.cpp
 * Copyright (C) 2002 Hacktarux
 *
 * Mupen64 homepage: http://mupen64.emulation64.com
 * email address: hacktarux@yahoo.fr
 * 
 * If you want to contribute to the project please contact
 * me first (maybe someone is already making what you are
 * planning to do).
 *
 *
 * This program is free software; you can redistribute it and/
 * or modify it under the terms of the GNU General Public Li-
 * cence as published by the Free Software Foundation; either
 * version 2 of the Licence, or any later version.
 *
 * This program is distributed in the hope that it will be use-
 * ful, but WITHOUT ANY WARRANTY; without even the implied war-
 * ranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public Licence for more details.
 *
 * You should have received a copy of the GNU General Public
 * Licence along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 * USA.
 *
**/

#include <stdio.h>
#include <math.h>
#include <string.h>

#include "tx.h"
#include "global.h"

TX::TX(GFX_INFO info) : gfxInfo(info)
{
   memset(descriptor,0,sizeof(descriptor)); memset(tmem,0,sizeof(tmem));
   memset(paletteData,0,sizeof(paletteData));
   textureLUT=textureLOD=textureDetail=texturePersp=0;
   tImg=NULL; format=size=width=0;
   for(int i=0; i<8; i++) {
       unpackTexel[i] = &TX::sample;
       descriptor[i].sampleWidth = descriptor[i].sampleHeight = 1;
   }
}

TX::~TX()
{
}

void TX::setTextureLUT(int value)
{
   textureLUT = value;
}

void TX::setTextureLOD(int value)
{
   textureLOD = value;
}

void TX::setTextureDetail(int value)
{
   textureDetail = value;
}

void TX::setTexturePersp(int value)
{
   texturePersp = value;
}

void TX::setTImg(int f, int s, int w, void *t)
{
   format = f;
   size = s;
   width = w;
   tImg = t;
}

void TX::setTile(int f, int s, int l, int t, int tile, int p,
		 int ct, int mt, int st, int cs, int ms, int ss)
{
   descriptor[tile].format  = f;
   descriptor[tile].size    = s;
   descriptor[tile].line    = l;
   descriptor[tile].tmem    = t;
   descriptor[tile].palette = p;
   descriptor[tile].cmt     = ct;
   descriptor[tile].maskt   = mt;
   descriptor[tile].shiftt  = st;
   descriptor[tile].cms     = cs;
   descriptor[tile].masks   = ms;
   descriptor[tile].shifts  = ss;
   
}

void TX::loadBlock(float uls, float ult, int tile, float lrs, int dxt)
{
   unsigned dst=descriptor[tile].tmem*8;
   unsigned bits=4u<<descriptor[tile].size;
   unsigned bytes=(((unsigned)lrs+1)*bits+7)/8;
   unsigned src=(unsigned char *)tImg-gfxInfo.RDRAM;
   src+=((unsigned)ult*width+(unsigned)uls)*bits/8;
   if(dst>4096 || bytes>4096-dst || src>SOFT_RDRAM_BYTES || bytes>SOFT_RDRAM_BYTES-src) return;
   for(unsigned i=0;i<bytes;i++) tmem[dst+i]=gfxInfo.RDRAM[(src+i)^S8];
}
void TX::loadTile(int tile, float uls, float ult, float lrs, float lrt)
{
   unsigned bits=4u<<size;
   if(lrs<uls || lrt<ult || uls<0 || ult<0) return;
   unsigned bytes=(((unsigned)(lrs-uls)+1)*bits+7)/8;
   unsigned base=(unsigned char *)tImg-gfxInfo.RDRAM;
   for(unsigned y=0;y<=(unsigned)(lrt-ult);y++) {
       unsigned src=base+(((unsigned)ult+y)*width+(unsigned)uls)*bits/8;
       unsigned dst=descriptor[tile].tmem*8+y*descriptor[tile].line*8;
       if(dst>4096 || bytes>4096-dst || src>SOFT_RDRAM_BYTES || bytes>SOFT_RDRAM_BYTES-src) return;
       for(unsigned x=0;x<bytes;x++) tmem[dst+x]=gfxInfo.RDRAM[(src+x)^S8];
   }
}
void TX::loadTLUT(int tile, int count)
{
   int dst=descriptor[tile].tmem-256;
   unsigned src=(unsigned char *)tImg-gfxInfo.RDRAM;
   if(dst<0 || count<0 || dst+count>256 || src>SOFT_RDRAM_BYTES-(unsigned)count*2) return;
   for(int i=0;i<count;i++) paletteData[dst+i]=(gfxInfo.RDRAM[(src+i*2)^S8]<<8)|gfxInfo.RDRAM[(src+i*2+1)^S8];
}

void TX::setTileSize(float uls, float ult, float lrs, float lrt, int tile)
{
   descriptor[tile].uls = uls;
   descriptor[tile].ult = ult;
   descriptor[tile].lrs = lrs;
   descriptor[tile].lrt = lrt;
   descriptor[tile].sampleWidth = (int)(lrs-uls)+1;
   descriptor[tile].sampleHeight = (int)(lrt-ult)+1;
}

Color32 TX::unpack_RGBA16(int tile, int s, int t)
{
   if(!translateCoordinates(s,t, tile)) return Color32(0,0,0,0);
   short *p = (short*)tmem;
   int c = p[descriptor[tile].tmem*4+t*descriptor[tile].line*4+s^S16];
   Color32 color(((c>>11)&0x1F)<<3, ((c>>6)&0x1F)<<3, ((c>>1)&0x1F)<<3, (c&1) != 0 ? 0xFF : 0);
   return color;
}

Color32 TX::unpack_CI8_RGBA16(int tile, int s, int t)
{
   if(!translateCoordinates(s,t, tile)) return Color32(0,0,0,0);
   unsigned char *p = (unsigned char*)tmem;
   short *p16 = (short*)tmem;
   int i = p[descriptor[tile].tmem*8+t*descriptor[tile].line*8+s^S8];
   int c = p16[256*4 + i^S16];
   Color32 color(((c>>11)&0x1F)<<3, ((c>>6)&0x1F)<<3, ((c>>1)&0x1F)<<3, (c&1) != 0 ? 0xFF : 0);
   return color;
}

Color32 TX::unpack_IA16(int tile, int s, int t)
{
   if(!translateCoordinates(s,t, tile)) return Color32(0,0,0,0);
   short *p = (short*)tmem;
   int c = p[descriptor[tile].tmem*4+t*descriptor[tile].line*4+s^S16];
   Color32 color((c>>8)&0xFF, (c>>8)&0xFF, (c>>8)&0xFF, c & 0xFF);
   return color;
}

Color32 TX::unpack_IA8(int tile, int s, int t)
{
   if(!translateCoordinates(s,t, tile)) return Color32(0,0,0,0);
   unsigned char *p = (unsigned char*)tmem;
   int c = p[descriptor[tile].tmem*8+t*descriptor[tile].line*8+s^S8];
   Color32 color(((c>>4)&0xF)<<4, ((c>>4)&0xF)<<4, ((c>>4)&0xF)<<4, (c & 0xF)<<4);
   return color;
}

Color32 TX::unpack_IA4(int tile, int s, int t)
{
   if(!translateCoordinates(s,t, tile)) return Color32(0,0,0,0);
   unsigned char *p = (unsigned char*)tmem;
   int c;
   if (s&1)
     c = p[descriptor[tile].tmem*8+t*descriptor[tile].line*8+(s/2)^S8] & 0xF;
   else
     c = p[descriptor[tile].tmem*8+t*descriptor[tile].line*8+(s/2)^S8] >> 4;
   Color32 color(c<<4, c<<4, c<<4, (c&1) ? 0xFF : 0x00);
   return color;
}

bool TX::translateCoordinates(int &s, int &t, int tile)
{
   Descriptor &d=descriptor[tile];
   int w=d.sampleWidth, h=d.sampleHeight;
   if(w<=0 || h<=0) return false;
   int *coords[2]={&s,&t};
   int limits[2]={w,h}, modes[2]={d.cms,d.cmt}, masks[2]={d.masks,d.maskt};
   for(int a=0;a<2;a++) {
       int &c=*coords[a];
       if(modes[a]&2) { if(c<0)c=0; if(c>=limits[a])c=limits[a]-1; }
       if(masks[a]) { int span=1<<masks[a]; bool mirror=(modes[a]&1) && (c&span); c&=span-1; if(mirror)c=span-1-c; }
       if(c<0) return false;
   }
   return true;
}

Color32 TX::sample(int tile, int s, int t)
{
   if(!translateCoordinates(s,t,tile)) return Color32(0,0,0,0);
   Descriptor &d=descriptor[tile];
   unsigned addr=d.tmem*8+t*d.line*8+(s*(4u<<d.size))/8;
   unsigned bytes=d.size==3?4:d.size==2?2:1;
   if(addr>4096-bytes) return Color32(0,0,0,0);
   unsigned v=tmem[addr];
   if(d.size==0) v=(s&1)?v&15:v>>4;
   if(d.size==2) v=(v<<8)|tmem[addr+1];
   if(d.format==2) {
       if(d.size>1) return Color32(0,0,0,0);
       if(d.size==0) v+=d.palette*16;
       v=paletteData[v&255];
       if(textureLUT==3) return Color32(v>>8,v>>8,v>>8,v&255);
   }
   if(d.format==0 || d.format==2) {
       if(d.format==0 && d.size==3) return Color32(tmem[addr],tmem[addr+1],tmem[addr+2],tmem[addr+3]);
       unsigned r=(v>>11)&31,g=(v>>6)&31,b=(v>>1)&31;
       return Color32((r<<3)|(r>>2),(g<<3)|(g>>2),(b<<3)|(b>>2),(v&1)?255:0);
   }
   if(d.format==3) {
       unsigned i=d.size==0?(v>>1)*255/7:d.size==1?(v>>4)*17:v>>8;
       unsigned a=d.size==0?(v&1)*255:d.size==1?(v&15)*17:v&255;
       return Color32(i,i,i,a);
   }
   if(d.format==4) { unsigned i=d.size==0?v*17:v; return Color32(i,i,i,i); }
   return Color32(0,0,0,0);
}

Color32 TX::getTexel(float _s, float _t, int tile, TF* tf)
{
   tile &= 7;
   /* RGB texels do not require the YUV conversion coefficients. */
   int ss=descriptor[tile].shifts, st=descriptor[tile].shiftt;
   if(ss) _s=ss<=10?_s/(1<<ss):_s*(1<<(16-ss));
   if(st) _t=st<=10?_t/(1<<st):_t*(1<<(16-st));
   float s = _s - descriptor[tile].uls;
   float t = _t - descriptor[tile].ult;
   
   if(unpackTexel[tile] == NULL) return Color32(0,0,0,0);

   if (!tf) return sample(tile,(int)floorf(s),(int)floorf(t));
   float fs=floorf(s), ft=floorf(t);
   if(s==fs && t==ft) return sample(tile,(int)fs,(int)ft);
   float dx=s-fs,dy=t-ft;
   float distance[4]={dx*dx+dy*dy,(1-dx)*(1-dx)+dy*dy,
                      (1-dx)*(1-dx)+(1-dy)*(1-dy),dx*dx+(1-dy)*(1-dy)};
   if (tf->getTextureFilter() == 0) {
       int nearest=0;
       for(int i=1;i<4;i++) if(distance[i]<distance[nearest]) nearest=i;
       return sample(tile,(int)fs+(nearest==1 || nearest==2),
                          (int)ft+(nearest==2 || nearest==3));
   }
   Color32 texels[4]={sample(tile,(int)fs,(int)ft), sample(tile,(int)fs+1,(int)ft),
                      sample(tile,(int)fs+1,(int)ft+1), sample(tile,(int)fs,(int)ft+1)};
   return tf->filter(texels,distance);
}
