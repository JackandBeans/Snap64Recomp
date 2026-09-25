#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace snap::vr {
// Vertex indices are local to a matched limb, not offsets into the rebuilt
// guest vertex heap. Mixed-limb triangles need a different correspondence
// scheme; decline them instead of guessing across a joint boundary.
struct VertexTopology {
    std::vector<uint32_t> corners;
    bool valid=true;
};
inline std::vector<VertexTopology> vertexTopology(std::span<const uint16_t> owners,
    std::span<const uint32_t> starts,std::span<const uint32_t> faces) {
    std::vector<VertexTopology> result(starts.size());
    if(faces.size()%3){for(auto& t:result)t.valid=false;return result;}
    for(size_t i=0;i<faces.size();i+=3) {
        uint32_t matrix[3]{};
        for(unsigned j=0;j<3;++j) {
            if(faces[i+j]>=owners.size()||owners[faces[i+j]]>=result.size()) {
                for(auto& t:result)t.valid=false;
                return result;
            }
            matrix[j]=owners[faces[i+j]];
        }
        if(matrix[0]!=matrix[1]||matrix[0]!=matrix[2]) {
            for(auto m:matrix)result[m].valid=false;
            continue;
        }
        auto& topology=result[matrix[0]];
        const auto begin=starts[matrix[0]];
        const size_t end=matrix[0]+1<starts.size()?starts[matrix[0]+1]:owners.size();
        for(unsigned j=0;j<3;++j) {
            if(faces[i+j]<begin||faces[i+j]>=end)topology.valid=false;
            else topology.corners.push_back(faces[i+j]-begin);
        }
    }
    return result;
}
struct VertexMesh {
    std::span<const float> positions,texcoords;
    const VertexTopology& topology;
};
struct VertexMotion {
    bool matched=false;
    unsigned moving=0;
    float maxDelta=0;
};
// Count alone does not identify vertices. Require identical indexed topology
// and per-index UVs as well as the caller's generation-stamped limb identity.
// Changed topology/ordering/UVs and large local discontinuities snap for this
// source pair. The next compatible pair can interpolate immediately.
inline VertexMotion vertexMotion(const VertexMesh& current,const VertexMesh& previous,
    std::span<float> velocity) {
    VertexMotion result;
    if(!current.topology.valid||!previous.topology.valid||current.topology.corners.empty()||
        current.topology.corners!=previous.topology.corners||current.positions.empty()||
        current.positions.size()%3||current.positions.size()!=previous.positions.size()||
        velocity.size()!=current.positions.size()||current.texcoords.size()!=current.positions.size()/3*2||
        current.texcoords.size()!=previous.texcoords.size()||
        !std::equal(current.texcoords.begin(),current.texcoords.end(),previous.texcoords.begin()))return result;
    float low[3]={INFINITY,INFINITY,INFINITY},high[3]={-INFINITY,-INFINITY,-INFINITY};
    float maxDeltaSquared=0;
    for(size_t i=0;i<current.positions.size();i+=3) {
        float deltaSquared=0;
        for(unsigned j=0;j<3;++j) {
            const float a=previous.positions[i+j],b=current.positions[i+j];
            if(!std::isfinite(a)||!std::isfinite(b))return {};
            low[j]=std::min(low[j],a);high[j]=std::max(high[j],a);
            deltaSquared+=(b-a)*(b-a);
        }
        maxDeltaSquared=std::max(maxDeltaSquared,deltaSquared);
        result.moving+=deltaSquared>1e-8f;
    }
    float diagonalSquared=0;
    for(unsigned j=0;j<3;++j)diagonalSquared+=(high[j]-low[j])*(high[j]-low[j]);
    if(maxDeltaSquared>std::max(1.f,diagonalSquared*.25f))return {};
    for(size_t i=0;i<velocity.size();++i)velocity[i]=current.positions[i]-previous.positions[i];
    result.matched=true;result.maxDelta=std::sqrt(maxDeltaSquared);
    return result;
}
}
