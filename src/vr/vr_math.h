#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace snap::vr {
constexpr float pi = 3.14159265358979323846f;
struct Vec3 { float x=0, y=0, z=0; };
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x*s,a.y*s,a.z*s}; }
inline float dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
inline float length(Vec3 v) { return std::sqrt(dot(v,v)); }
inline Vec3 normalized(Vec3 v) { float n=length(v); return n>1e-6f?v*(1/n):Vec3{}; }
struct Quat { float x=0,y=0,z=0,w=1; };
inline Quat conjugate(Quat q) { return {-q.x,-q.y,-q.z,q.w}; }
inline Quat operator*(Quat a, Quat b) {
    Vec3 av{a.x,a.y,a.z},bv{b.x,b.y,b.z}; auto v=bv*a.w+av*b.w+cross(av,bv);
    return {v.x,v.y,v.z,a.w*b.w-dot(av,bv)};
}
inline Vec3 rotate(Quat q, Vec3 v) { Vec3 u{q.x,q.y,q.z}; return v+cross(u,v)*(2*q.w)+cross(u,cross(u,v))*2; }
inline Quat yaw(float angle) { return {0,std::sin(angle/2),0,std::cos(angle/2)}; }
inline float heading(Quat q) { Vec3 f=rotate(q,{0,0,-1}); return std::atan2(-f.x,-f.z); }
struct Pose { Quat orientation{}; Vec3 position{}; };
inline Pose compose(Pose a, Pose b) { return {a.orientation*b.orientation,a.position+rotate(a.orientation,b.position)}; }
inline Pose inverse(Pose p) { auto q=conjugate(p.orientation); return {q,rotate(q,p.position*-1)}; }
// Row-major matrices for RT64's row-vector convention; OpenXR is RH, -Z forward.
using Matrix = std::array<float,16>;
inline Matrix transform(Pose p) {
    Vec3 x=rotate(p.orientation,{1,0,0}),y=rotate(p.orientation,{0,1,0}),z=rotate(p.orientation,{0,0,1});
    return {x.x,x.y,x.z,0,y.x,y.y,y.z,0,z.x,z.y,z.z,0,p.position.x,p.position.y,p.position.z,1};
}
inline Matrix view(Pose p) { return transform(inverse(p)); }
struct Fov { float left=-pi/4,right=pi/4,up=pi/4,down=-pi/4; };
inline Matrix projection(Fov f,float nearZ,float farZ) {
    float l=std::tan(f.left),r=std::tan(f.right),u=std::tan(f.up),d=std::tan(f.down);
    // N64/RT64 uses OpenGL's -1..1 clip depth. The raster shader converts to D3D.
    return {2/(r-l),0,0,0,0,2/(u-d),0,0,(r+l)/(r-l),(u+d)/(u-d),-(farZ+nearZ)/(farZ-nearZ),-1,0,0,-2*farZ*nearZ/(farZ-nearZ),0};
}
}
