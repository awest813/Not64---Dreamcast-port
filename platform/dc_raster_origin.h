#ifndef DC_RASTER_ORIGIN_H
#define DC_RASTER_ORIGIN_H
#include <cmath>

// Shift an interpolated attribute plane to integer screen sample origins.
// Geometry is unchanged. Perspective UVs require shifting U/W, V/W and 1/W
// together, rather than subtracting a fixed amount from the divided UVs.
struct DCRasterOrigin {
    float first,second;
    bool prepare(const float xy[3][2]) {
        float x1=xy[1][0]-xy[0][0],y1=xy[1][1]-xy[0][1];
        float x2=xy[2][0]-xy[0][0],y2=xy[2][1]-xy[0][1];
        float area=x1*y2-x2*y1;
        if(!std::isfinite(area) || std::fabs(area)<0.0001f)return false;
        float scale=0.5f/area;
        first=(y2-x2)*scale;second=(x1-y1)*scale;
        return std::isfinite(first) && std::isfinite(second);
    }
    float shift(float a,float b,float c) const {
        return (b-a)*first+(c-a)*second;
    }
    bool texture(const float u[3],const float v[3],const float q[3],
                 float outU[3],float outV[3],float outQ[3]) const {
        float dq=shift(q[0],q[1],q[2]);
        float du=shift(u[0]*q[0],u[1]*q[1],u[2]*q[2]);
        float dv=shift(v[0]*q[0],v[1]*q[1],v[2]*q[2]);
        for(unsigned i=0;i<3;i++) {
            outQ[i]=q[i]-dq;
            if(!(outQ[i]>0) || !std::isfinite(outQ[i]))return false;
            outU[i]=(u[i]*q[i]-du)/outQ[i];
            outV[i]=(v[i]*q[i]-dv)/outQ[i];
            if(!std::isfinite(outU[i]) || !std::isfinite(outV[i]))return false;
        }
        return true;
    }
};
#endif
