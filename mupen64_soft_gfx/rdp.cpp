/**
 * Mupen64 - rdp.cpp
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

#include "rdp.h"
#ifdef DC_SOFT_PROFILE
#include <stdint.h>
#ifdef DC_HOST_STUB
#include <time.h>
static uint64_t soft_now() {
    timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;
}
#else
#include <kos.h>
static uint64_t soft_now() { return timer_us_gettime64(); }
#endif
namespace {
struct SoftBucket { uint64_t us,softwareUs; unsigned calls,softwareCalls; } softBuckets[7][4];
struct SoftState {
    unsigned kind,cycle,blender,mux0,mux1,filter,flags;
    SoftBucket bucket;
    unsigned prim,primChanges,whitePrim;
} softStates[128];
unsigned softStateCount;
unsigned softLists;
struct SoftScope {
    SoftBucket &bucket; SoftBucket *state; uint64_t start,softwareStart=0;
    SoftScope(unsigned kind,unsigned cycle,unsigned blender,unsigned mux0,unsigned mux1,unsigned filter,unsigned prim,unsigned flags):
        bucket(softBuckets[kind][cycle&3]),state(nullptr) {
        for(unsigned i=0;i<softStateCount;i++) {
            auto &s=softStates[i];
            if(s.kind==kind && s.cycle==cycle && s.blender==blender &&
               s.mux0==mux0 && s.mux1==mux1 && s.filter==filter && s.flags==flags){
                state=&s.bucket;if(s.prim!=prim)++s.primChanges;
                if((prim>>8)==0xffffff)++s.whitePrim;break;
            }
        }
        if(!state && softStateCount<128) {
            auto &s=softStates[softStateCount++];
            s={kind,cycle,blender,mux0,mux1,filter,flags,{0,0,0,0},prim,0,(prim>>8)==0xffffff?1u:0u};state=&s.bucket;
        }
        start=soft_now();
    }
    void software(){softwareStart=soft_now();}
    ~SoftScope(){
        uint64_t end=soft_now(),us=end-start;bucket.us+=us;++bucket.calls;
        if(state){state->us+=us;++state->calls;}
        if(softwareStart) {
            bucket.softwareUs+=end-softwareStart;++bucket.softwareCalls;
            if(state){state->softwareUs+=end-softwareStart;++state->softwareCalls;}
        }
    }
};
}
extern "C" void dc_soft_profile_reset() {
    for(auto &kind:softBuckets)for(auto &b:kind)b={0,0,0,0};
    softLists=0;
    softStateCount=0;
}
extern "C" void dc_soft_profile_list() {
    if(++softLists%60)return;
    const char *names[]={"fill","rect","texture-z","texture","shade-z","shade","debug"};
    for(unsigned k=0;k<7;k++)for(unsigned c=0;c<4;c++) {
        auto &b=softBuckets[k][c];
        if(b.calls)printf("Soft profile: lists=%u kind=%s cycle=%u calls=%u us=%llu software-calls=%u software-us=%llu\n",
            softLists,names[k],c,b.calls,(unsigned long long)b.us,b.softwareCalls,(unsigned long long)b.softwareUs);
    }
    for(unsigned i=0;i<softStateCount;i++) {
        auto &s=softStates[i];
        printf("Soft state: lists=%u kind=%s cycle=%u blender=%08x mux=%08x/%08x filter=%u calls=%u us=%llu prim=%08x changes=%u white=%u flags=%05x\n",
            softLists,names[s.kind],s.cycle,s.blender,s.mux0,s.mux1,s.filter,
            s.bucket.calls,(unsigned long long)s.bucket.us,s.prim,s.primChanges,s.whitePrim,s.flags);
        printf("Soft path: lists=%u kind=%s cycle=%u blender=%08x mux=%08x/%08x filter=%u flags=%05x sw=%u sw-us=%llu\n",
            softLists,names[s.kind],s.cycle,s.blender,s.mux0,s.mux1,s.filter,
            s.flags,s.bucket.softwareCalls,(unsigned long long)s.bucket.softwareUs);
    }
}
#define SOFT_SCOPE(kind) SoftScope softScope(kind,cycleType,bl->profileMode(),cc->profileMux(0),cc->profileMux(1),tf->getTextureFilter(),cc->profilePrim(),bl->profileFlags())
#define SOFT_SOFTWARE() softScope.software()
#else
#define SOFT_SCOPE(kind) ((void)0)
#define SOFT_SOFTWARE() ((void)0)
#endif
#ifdef DC_RASTER_PVR
#include "../platform/dc_raster_pvr.h"
#endif

RDP::RDP(GFX_INFO info) : gfxInfo(info)
{
   cycleType=0;
   rasterEnabled=true;
   tx = new TX(info);
   rs = new RS();
   tf = new TF();
   cc = new CC();
   bl = new BL(info);
}

RDP::~RDP()
{
#ifdef DC_RASTER_PVR
   PVRRaster::reset();
#endif
   delete tx;
   delete rs;
   delete tf;
   delete cc;
   delete bl;
}

void RDP::setOtherMode_l(int mode, int data)
{
   switch(mode)
     {
      case 0:
	bl->setAlphaCompare(data);
	break;
      case 2:
	bl->setDepthSource(data);
	break;
      case 3:
	bl->setBlender(data<<3);
	break;
      default:
	printf("RDP: unknown setOtherMode_l:%d\n", mode);
     }
}

void RDP::setOtherMode_h(int mode, int data)
{
   switch(mode)
     {
      case 4:
	bl->setAlphaDither(data);
	break;
      case 6:
	bl->setColorDither(data);
	break;
      case 8:
	cc->setCombineKey(data);
	break;
      case 9:
	tf->setTextureConvert(data);
	break;
      case 12:
	tf->setTextureFilter(data);
	break;
      case 14:
	tx->setTextureLUT(data);
	break;
      case 16:
	tx->setTextureLOD(data);
	break;
      case 17:
	tx->setTextureDetail(data);
	break;
      case 19:
	tx->setTexturePersp(data);
	break;
      case 20:
	cycleType = data;
	break;
      case 23:
	// ignoring pipeline mode
	break;
      default:
	printf("RDP: unknown setOtherMode_h:%d\n", mode);
     }
}

void RDP::setScissor(float ulx, float uly, float lrx, float lry, int mode)
{
   rs->setScissor(ulx, uly, lrx, lry, mode);
}

void RDP::setCombineMode(int cycle1, int cycle2)
{
   cc->setCombineMode(cycle1, cycle2);
}

void RDP::setCImg(int format, int size, int width, void *cimg)
{
#ifdef DC_RASTER_PVR
   PVRRaster::flush();
#endif
   bl->setCImg(format, size, width, cimg);
}

void RDP::setTImg(int format, int size, int width, void *timg)
{
   tx->setTImg(format, size, width, timg);
}

void RDP::setZImg(void *zimg)
{
   bl->setZImg(zimg);
}

void RDP::setFillColor(int color)
{
   bl->setFillColor(color);
}

void RDP::setFogColor(int color)
{
   bl->setFogColor(color);
}

void RDP::setBlendColor(int color)
{
   bl->setBlendColor(color);
}

void RDP::setEnvColor(int color)
{
   cc->setEnvColor(color);
}

void RDP::setPrimColor(int color, float mLOD, float lLOD)
{
   cc->setPrimColor(color, mLOD, lLOD);
}

void RDP::fillRect(float ulx, float uly, float lrx, float lry)
{
   if(!rasterEnabled)return;
   SOFT_SCOPE(0);
#ifdef DC_RASTER_PVR
   if(PVRRaster::fill(this,ulx,uly,lrx,lry)) return;
   PVRRaster::flush();
#endif
   SOFT_SOFTWARE();rs->fillRect(ulx, uly, lrx, lry, this);
}

void RDP::setTile(int format, int size, int line, int tmem, int tile, int palette,
		  int cmt, int maskt, int shiftt, int cms, int masks, int shifts)
{
   tx->setTile(format, size, line, tmem, tile, palette, cmt, maskt, shiftt, cms, masks, shifts);
}

void RDP::loadBlock(float uls, float ult, int tile, float lrs, int dxt)
{
   tx->loadBlock(uls, ult, tile, lrs, dxt);
}

void RDP::setTileSize(float uls, float ult, float lrs, float lrt, int tile)
{
   tx->setTileSize(uls, ult, lrs, lrt, tile);
}

void RDP::texRect(int tile, float ulx, float uly, float lrx, float lry, float s, float t, float dsdx, float dtdy, bool flip)
{
   if(!rasterEnabled)return;
   SOFT_SCOPE(1);
#ifdef DC_RASTER_PVR
   if(!flip && PVRRaster::rectangle(this,tile,ulx,uly,lrx,lry,s,t,dsdx,dtdy)) return;
   PVRRaster::flush();
#endif
   SOFT_SOFTWARE();rs->texRect(tile, ulx, uly, lrx, lry, s, t, dsdx, dtdy, this, flip);
}

void RDP::loadTLUT(int tile, int count)
{
   tx->loadTLUT(tile, count);
}

void RDP::loadTile(int tile, float uls, float ult, float lrs, float lrt)
{
   tx->loadTile(tile, uls, ult, lrs, lrt);
}

void RDP::debug_tri(Vektor<float,4>& v0, Vektor<float,4>& v1, Vektor<float,4>& v2)
{
   if(!rasterEnabled)return;
   SOFT_SCOPE(6);
#ifdef DC_RASTER_PVR
   PVRRaster::flush();
#endif
   SOFT_SOFTWARE();rs->debug_tri(v0, v1, v2, this);
}

void RDP::tri_shade_zbuff(Vektor<float,4>& v0, Vektor<float,4>& v1, Vektor<float,4>& v2,
			  Color32& c0, Color32& c1, Color32& c2, float z0, float z1, float z2)
{
   if(!rasterEnabled)return;
   SOFT_SCOPE(4);
#ifdef DC_RASTER_PVR
   PVRRaster::flush();
#endif
   SOFT_SOFTWARE();rs->tri_shade_zbuff(v0, v1, v2, c0, c1, c2, z0, z1, z2, this);
}

void RDP::tri_shade_txtr_zbuff(Vektor<float,4>& v0, Vektor<float,4>& v1, Vektor<float,4>& v2,
			       Color32& c0, Color32& c1, Color32& c2, 
			       float s0, float t0, float s1, float t1, float s2, float t2, int tile,
			       float w0, float w1, float w2, float z0, float z1, float z2)
{
   if(!rasterEnabled)return;
   SOFT_SCOPE(2);
#ifdef DC_RASTER_PVR
   if(PVRRaster::triangle(this,v0,v1,v2,c0,c1,c2,s0,t0,s1,t1,s2,t2,tile,w0,w1,w2)) return;
   PVRRaster::flush();
#endif
   SOFT_SOFTWARE();rs->tri_shade_txtr_zbuff(v0, v1, v2, c0, c1, c2, s0, t0, s1, t1, s2, t2, tile, w0, w1, w2, z0, z1, z2, this);
}

void RDP::tri_shade_txtr(Vektor<float,4>& v0, Vektor<float,4>& v1, Vektor<float,4>& v2,
			 Color32& c0, Color32& c1, Color32& c2, 
			 float s0, float t0, float s1, float t1, float s2, float t2, int tile, float w0, float w1, float w2)
{
   if(!rasterEnabled)return;
   SOFT_SCOPE(3);
#ifdef DC_RASTER_PVR
   PVRRaster::flush();
#endif
   SOFT_SOFTWARE();rs->tri_shade_txtr(v0, v1, v2, c0, c1, c2, s0, t0, s1, t1, s2, t2, tile, w0, w1, w2, this);
}

void RDP::tri_shade(Vektor<float,4>& v0, Vektor<float,4>& v1, Vektor<float,4>& v2,
		    Color32& c0, Color32& c1, Color32& c2)
{
   if(!rasterEnabled)return;
   SOFT_SCOPE(5);
#ifdef DC_RASTER_PVR
   PVRRaster::flush();
#endif
   SOFT_SOFTWARE();rs->tri_shade(v0, v1, v2, c0, c1, c2, this);
}
