#pragma once
#include "vr_math.h"
#include <algorithm>
#include <utility>
#include <vector>

namespace snap::vr {
struct TransparentTriangle {unsigned first;Vec3 center;};

// Stereo eyes share geometry, but translucent surfaces need their own ordering
// in each eye. Sort by camera-space depth, not distance from the world origin.
inline std::vector<unsigned> transparentIndices(const std::vector<TransparentTriangle>& triangles,Pose eye) {
    const Vec3 forward=rotate(eye.orientation,{0,0,-1});
    std::vector<std::pair<float,unsigned>> order;order.reserve(triangles.size());
    for(const auto& triangle:triangles)order.emplace_back(dot(triangle.center-eye.position,forward),triangle.first);
    std::stable_sort(order.begin(),order.end(),[](const auto& a,const auto& b){return a.first>b.first;});
    std::vector<unsigned> indices;indices.reserve(triangles.size()*3);
    for(const auto& entry:order)for(unsigned k=0;k<3;k++)indices.push_back(entry.second+k);
    return indices;
}
}
