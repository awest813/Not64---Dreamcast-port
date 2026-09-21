/**
 * Mupen64 - bl.cpp
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
#include <stdlib.h>
#include <stdint.h>

#include "bl.h"

unsigned short* BL::zLUT = NULL;

BL::BL(GFX_INFO info) : gfxInfo(info), zero(0), one(0xFFFFFFFF)
{
   alphaCompare=colorDither=alphaDither=depthSource=0;
   cImg=zImg=NULL; width=format=size=0;
   oldBlenderMode=-1; setBlender(0);
   if(zLUT == NULL)
     {
	zLUT = new unsigned short[0x40000];
	
	for(int i=0; i<0x40000; i++)
	  {
	     unsigned long exponent = 0;
	     unsigned long testbit = 1 << 17;
	     while((i & testbit) && (exponent < 7))
	       {
		  exponent++;
		  testbit = 1 << (17 - exponent);
	       }
	     
	     unsigned short mantissa = (i >> (6 - (6 < exponent ? 6 : exponent))) & 0x7ff;
	     zLUT[i] = ((exponent << 11) | mantissa) << 2;
	  }
     }
}

BL::~BL()
{
}

void BL::setAlphaCompare(int value)
{
   alphaCompare = value;
}

void BL::setColorDither(int value)
{
   colorDither = value;
}

void BL::setAlphaDither(int value)
{
   alphaDither = value;
}

void BL::setDepthSource(int value)
{
   depthSource = value;
}

void BL::setCImg(int f, int s, int w, void *c)
{
   if (f != 0 || s != 2) printf("bl: unknown framebuffer format\n");
   format = f;
   size = s;
   width = w;
   cImg = c;
}

void BL::setZImg(void *z)
{
   zImg = z;
}

Color32* BL::getBlenderSource(int src, int pos, int cycle)
{
   if(pos==1 || pos==3) {
       Color32 *colors[]={cycle==1?&pixelColor:&blendedPixelColor,
                          &memoryColor,&blendColor,&fogColor};
       return colors[src&3];
   }
   if(pos==2) {
       Color32 *alpha[]={&pixelColor,&fogColor,&shadeColor,&zero};
       return alpha[src&3];
   }
   Color32 *alpha[]={&invertedAlpha,&memoryColor,&one,&zero};
   return alpha[src&3];
}

void BL::setBlender(int value)
{
   // render modes
   aa_en         = (value & 0x0008) != 0;
   z_cmp         = (value & 0x0010) != 0;
   z_upd         = (value & 0x0020) != 0;
   im_rd         = (value & 0x0040) != 0;
   clr_on_cvg    = (value & 0x0080) != 0;
   cvg_dst_wrap  = (value & 0x0100) != 0;
   cvg_dst_full  = (value & 0x0200) != 0;
   zmode_inter   = (value & 0x0400) != 0;
   zmode_xlu     = (value & 0x0800) != 0;
   cvg_x_alpha   = (value & 0x1000) != 0;
   alpha_cvg_sel = (value & 0x2000) != 0;
   force_bl      = (value & 0x4000) != 0;
   renderMode = value & 0xffff;
   if (value & ~0xffff7ff8)
     printf("bl: unknwown render mode:%x\n", value & ~0xffff7ff8);
   
   // blender modes
   if (oldBlenderMode == (value>>16)) return;
   oldBlenderMode = value>>16;
   
   int sa1,sb1,ca1,cb1, sa2, sb2, ca2, cb2;
   sa1 = (value >> 30) & 3;
   sa2 = (value >> 28) & 3;
   ca1 = (value >> 26) & 3;
   ca2 = (value >> 24) & 3;
   sb1 = (value >> 22) & 3;
   sb2 = (value >> 20) & 3;
   cb1 = (value >> 18) & 3;
   cb2 = (value >> 16) & 3;

   psa1 = getBlenderSource(sa1, 1, 1);
   psa2 = getBlenderSource(sa2, 1, 2);
   pca1 = getBlenderSource(ca1, 2, 1);
   pca2 = getBlenderSource(ca2, 2, 2);
   psb1 = getBlenderSource(sb1, 3, 1);
   psb2 = getBlenderSource(sb2, 3, 2);
   pcb1 = getBlenderSource(cb1, 4, 1);
   pcb2 = getBlenderSource(cb2, 4, 2);
}

void BL::setFillColor(int color)
{
   fillColor = color;
}

void BL::setFogColor(int color)
{
   fogColor = color;
}

void BL::setBlendColor(int color)
{
   blendColor = color;
}

void BL::fillModeDraw(int x, int y)
{
   if(!validPixel(cImg,x,y,4)) return;
   int *p = (int*)cImg;
   p[(y*width+x)/2] = fillColor;
   //vi->debug_plot(x,y,(fillColor>>16)&0xffff);
   //vi->debug_plot(x+1,y,fillColor&0xffff);
}


void BL::cycle1ModeDraw(int x, int y, Color32 c, float z, Color32 shade)
{
   cycleModeDraw(x,y,c,z,shade,false);
}

void BL::cycle2ModeDraw(int x, int y, Color32 c, float z, Color32 shade)
{
   cycleModeDraw(x,y,c,z,shade,true);
}

void BL::cycleModeDraw(int x, int y, Color32 c, float z, Color32 shade, bool twoCycles)
{
   if(size!=2 || format!=0 || !validPixel(cImg,x,y,2)) return;
   c.clamp(); shade.clamp();
   /* No subpixel coverage buffer yet: treat covered samples as full coverage.
    * Alpha-to-coverage must still discard cutout texels before color/Z writes. */
   if(cvg_x_alpha && c.getAlpha()<32) return;
   if(alpha_cvg_sel && !cvg_x_alpha) c.setAlpha(255);
   static const unsigned char dither[16]={0,128,32,160,192,64,224,96,48,176,16,144,240,112,208,80};
   if((alphaCompare&1) && c.getAlpha()<((alphaCompare&2)?dither[(y&3)*4+(x&3)]:blendColor.getAlpha())) return;

   unsigned short *p=(unsigned short *)cImg, *pz=(unsigned short *)zImg;
   unsigned pixel=(y*width+x)^S16;
   if(z_cmp || z_upd) {
       if(!validPixel(zImg,x,y,2)) return;
       if(depthSource) z=primitiveZ;
       int fz=(int)(z*8.0f+0.5f);
       if(fz<0) fz=0; if(fz>0x3ffff) fz=0x3ffff;
       unsigned short encodedZ=zLUT[fz];
       if(z_cmp && encodedZ>pz[pixel]+((zmode_inter && zmode_xlu)?256:0)) return;
       if(z_upd && !(zmode_inter && zmode_xlu)) pz[pixel]=encodedZ;
   }

   unsigned v=p[pixel], r=(v>>11)&31, g=(v>>6)&31, b=(v>>1)&31;
   memoryColor=Color32((r<<3)|(r>>2),(g<<3)|(g>>2),(b<<3)|(b>>2),255);
   pixelColor=c; shadeColor=shade;
   Color32 result=*psa1;
   if(twoCycles || force_bl) {
       float ca=pca1->getAlpha()/255.0f;
       invertedAlpha=Color32(0,0,0,255.0f-pca1->getAlpha());
       float cb=pcb1->getAlpha()/255.0f;
       result=*psa1*ca+*psb1*cb;
   }
   if(twoCycles) {
       blendedPixelColor=result;
       result=*psa2;
       if(force_bl) {
           float ca=pca2->getAlpha()/255.0f;
           invertedAlpha=Color32(0,0,0,255.0f-pca2->getAlpha());
           float cb=pcb2->getAlpha()/255.0f;
           result=*psa2*ca+*psb2*cb;
       }
   }
   unsigned out=(unsigned)(int)result;
   p[pixel]=((out>>16)&0xf800)|((out>>13)&0x7c0)|((out>>10)&0x3e)|1;
}

void BL::copyModeDraw(int x, int y, Color32 c)
{
   if(!validPixel(cImg,x,y,2)) return;
   short *p = (short*)cImg;
   if (alphaCompare && c.getAlpha() < blendColor.getAlpha()) return;
   int colorValue = (int)c;
   colorValue = 
     ((((colorValue >> 24)&0xFF)>>3)<<11) |
     ((((colorValue >> 16)&0xFF)>>3)<< 6) |
     ((((colorValue >>  8)&0xFF)>>3)<< 1);
   p[y*width+x^S16] = colorValue;
   //vi->debug_plot(x,y,colorValue);
}

void BL::debug_plot(int x, int y, int c)
{
   if(!validPixel(cImg,x,y,2)) return;
   short *p = (short*)cImg;
   p[y*width+x^S16] = c;
}

bool BL::validPixel(void *image, int x, int y, unsigned bytes) const
{
   if(!image || width<=0 || width>1024 || x<0 || x>=width || y<0 || y>=1024) return false;
   uintptr_t base=(uintptr_t)image, ram=(uintptr_t)gfxInfo.RDRAM;
   if(base<ram || base-ram>SOFT_RDRAM_BYTES) return false;
   unsigned offset=(base-ram)+(y*width+x)*2;
   return offset<=SOFT_RDRAM_BYTES-bytes;
}
