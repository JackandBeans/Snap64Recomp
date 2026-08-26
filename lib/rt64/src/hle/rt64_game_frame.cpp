//
// RT64
//

#include <algorithm>
#include <chrono>
#include <unordered_map>
#include "common/rt64_math.h"

#include "rt64_game_frame.h"
#include "rt64_snap_diag.h"
#include "rt64_workload_queue.h"

#include <cstdio>

#include "xxHash/xxh3.h"

namespace RT64 {
    // GameFrame
    
    bool GameFrame::areFramebufferPairsCompatible(const WorkloadQueue &workloadQueue, const GameIndices::FramebufferPair &first, const GameIndices::FramebufferPair &second) {
        if (first == second) {
            return true;
        }

        const Workload &firstWorkload = workloadQueue.workloads[first.workloadIndex];
        const Workload &secondWorkload = workloadQueue.workloads[second.workloadIndex];
        const auto &firstFbPair = firstWorkload.fbPairs[first.fbPairIndex];
        const auto &secondFbPair = secondWorkload.fbPairs[second.fbPairIndex];
        if ((firstFbPair.depthRead || firstFbPair.depthWrite) && (secondFbPair.depthRead || secondFbPair.depthWrite)) {
            if (firstFbPair.depthImage.address != secondFbPair.depthImage.address) {
                return false;
            }
        }

        const auto &firstColorImage = firstFbPair.colorImage;
        const auto &secondColorImage = secondFbPair.colorImage;
        if ((firstColorImage.address != secondColorImage.address) ||
            (firstColorImage.fmt != secondColorImage.fmt) ||
            (firstColorImage.siz != secondColorImage.siz) ||
            (firstColorImage.width != secondColorImage.width))
        {
            return false;
        }

        return true;
    }

    bool GameFrame::isSceneCompatible(const WorkloadQueue &workloadQueue, const GameScene &scene, const GameIndices::Projection &proj) {
        assert(!scene.projections.empty());

        const float MatrixDiffTolerance = 1e-6f;
        const Workload &workload = workloadQueue.workloads[proj.workloadIndex];
        const FramebufferPair &fbPair = workload.fbPairs[proj.fbPairIndex];
        const Projection &fbProj = fbPair.projections[proj.projectionIndex];
        const GameIndices::Projection &firstProj = scene.projections.front();
        if (!areFramebufferPairsCompatible(workloadQueue, { firstProj.workloadIndex, firstProj.fbPairIndex }, { proj.workloadIndex, proj.fbPairIndex })) {
            return false;
        }

        const Workload &cmpWorkload = workloadQueue.workloads[firstProj.workloadIndex];
        const FramebufferPair &cmpFbPair = cmpWorkload.fbPairs[firstProj.fbPairIndex];
        const Projection &cmpProj = cmpFbPair.projections[firstProj.projectionIndex];
        const interop::float4x4 &cmpViewMatrix = cmpWorkload.drawData.viewTransforms[cmpProj.transformsIndex];
        const interop::float4x4 &fbViewMatrix = workload.drawData.viewTransforms[fbProj.transformsIndex];
        const float viewMatrixDiff = matrixDifference(cmpViewMatrix, fbViewMatrix);
        if (viewMatrixDiff > MatrixDiffTolerance) {
            return false;
        }

        const interop::float4x4 &cmpProjMatrix = cmpWorkload.drawData.projTransforms[cmpProj.transformsIndex];
        const interop::float4x4 &fbProjMatrix = workload.drawData.projTransforms[fbProj.transformsIndex];
        const float projMatrixDiff = matrixDifference(cmpProjMatrix, fbProjMatrix);
        if (projMatrixDiff > MatrixDiffTolerance) {
            return false;
        }

        return true;
    }

    void GameFrame::set(WorkloadQueue &workloadQueue, const uint32_t *workloadIndices, uint32_t indicesCount) {
        assert(workloadIndices != nullptr);
        assert(indicesCount > 0);

        matched = false;
        perspectiveScenes.clear();
        orthographicScenes.clear();
        workloads.clear();
        workloads.insert(workloads.end(), workloadIndices, workloadIndices + indicesCount);

        auto addProjection = [&](const WorkloadQueue &workloadQueue, const GameIndices::Projection &newProj, std::vector<GameScene> &gameScenes) {
            bool added = false;
            for (auto &gameScene : gameScenes) {
                if (isSceneCompatible(workloadQueue, gameScene, newProj)) {
                    gameScene.projections.emplace_back(newProj);
                    added = true;
                    break;
                }
            }

            if (!added) {
                gameScenes.emplace_back(GameScene());
                gameScenes.back().projections.emplace_back(newProj);
            }
        };

        for (uint32_t i = 0; i < indicesCount; i++) {
            uint32_t w = workloadIndices[i];
            const Workload &workload = workloadQueue.workloads[w];
            for (uint32_t f = 0; f < workload.fbPairCount; f++) {
                const FramebufferPair &fbPair = workload.fbPairs[f];
                for (uint32_t p = 0; p < fbPair.projectionCount; p++) {
                    const GameIndices::Projection newProj = { w, f, p };
                    const auto &fbPairProj = fbPair.projections[p];
                    switch (fbPairProj.type) {
                    case Projection::Type::Perspective:
                        addProjection(workloadQueue, newProj, perspectiveScenes);
                        break;
                    case Projection::Type::Orthographic:
                        addProjection(workloadQueue, newProj, orthographicScenes);
                        break;
                    default:
                        break;
                    }
                }
            }
        }

        // Use the default values for the preset scene.
        presetScene = PresetScene();

        /*
        // Use the settings from all enabled presets. Use the last one sorted by name enabled.
        // TODO: Figure out a way to skip the linear lookup on the map.
        for (const auto &it : sceneLibrary.presetMap) {
            if (!it.second.enabled) {
                continue;
            }

            gameFrame.presetScene = it.second;
        }
        */

        /*
        TODO: Must be per projection.
        if (presetScene.estimateAmbientLight && (lightManager.ambientSum > 0)) {
            hlslpp::float3 ambientLight = { 0.01f, 0.01f, 0.01f };
            ambientLight = lightManager.estimatedAmbientLight(gameFrame.presetScene.ambientLightIntensity);
            presetScene.ambientBaseColor = ambientLight;
            presetScene.ambientNoGIColor = ambientLight;
        }
        */

        for (const GameScene &scene : perspectiveScenes) {
            for (const GameIndices::Projection &projection : scene.projections) {
                Workload &workload = workloadQueue.workloads[projection.workloadIndex];
                FramebufferPair &fbPair = workload.fbPairs[projection.fbPairIndex];
                Projection &proj = fbPair.projections[projection.projectionIndex];

                // Add all the lights stored in the workload.
                for (const interop::PointLight &light : workload.pointLights) {
                    proj.addPointLight(light);
                }
            }
        }

        frameMap.clear();
        frameMap.workloads.resize(workloadQueue.workloads.size());
    }
    
    typedef std::pair<uint32_t, uint32_t> IndexPair;

    bool operator<(const IndexPair &lhs, const IndexPair &rhs) {
        return (lhs.first < rhs.first) || ((lhs.first == rhs.first) && lhs.second < rhs.second);
    }

    struct MatchCandidate {
        uint32_t curIndex = 0;
        uint32_t prevIndex = 0;
        float difference = FLT_MAX;

        MatchCandidate(uint32_t curIndex, uint32_t prevIndex, float difference) {
            this->curIndex = curIndex;
            this->prevIndex = prevIndex;
            this->difference = difference;
        }
    };

    bool operator<(const MatchCandidate &lhs, const MatchCandidate &rhs) {
        return lhs.difference < rhs.difference;
    }

    struct TransformMatchResult {
        float positionDifference = FLT_MAX;
        float orientationDifference = FLT_MAX;
        float screenSpaceDifference = FLT_MAX;
        bool valid = false;

        float computeDifference() const {
            if (!valid) {
                return FLT_MAX;
            }

            float totalDiff = 0.0f;

            const float PositionDiffScale = 1.0f;
            if (positionDifference < FLT_MAX) {
                totalDiff += positionDifference * PositionDiffScale;
            }

            const float OrientationDiffScale = 1.0f;
            if (orientationDifference < FLT_MAX) {
                totalDiff += orientationDifference * OrientationDiffScale;
            }

            const float ScreenSpaceDiffScale = 1.0f;
            if (screenSpaceDifference < FLT_MAX) {
                totalDiff += screenSpaceDifference * ScreenSpaceDiffScale;
            }

            return totalDiff;
        }
    };

    TransformMatchResult computeTransformMatch(const hlslpp::float4x4 &curTransform, const hlslpp::float4x4 &curViewProj, const hlslpp::float4x4 &prevTransform, const hlslpp::float4x4 &prevViewProj, const RigidBody *prevRigidBody) {
        TransformMatchResult matchResult;

        // Do not accept a match between these transforms if the determinant is different, which indicates they're mirrored from each other.
        const float m0det = hlslpp::determinant(extract3x3(prevTransform));
        const float m1det = hlslpp::determinant(extract3x3(curTransform));
        if ((m0det * m1det) < 0.0f) {
            return matchResult;
        }

        // Compute the difference between the translation components of the 4x4 matrices.
        const hlslpp::float3 curPos = curTransform[3].xyz;
        hlslpp::float3 prevPos = prevTransform[3].xyz;
        if (prevRigidBody != nullptr) {
            prevPos += prevRigidBody->linearVelocity;
        }

        matchResult.positionDifference = hlslpp::length(curPos - prevPos);

        // Compute the dot product difference between the normalized XYZ vectors of the 3x3 matrices.
        matchResult.orientationDifference =
            (1.0f - hlslpp::dot(hlslpp::normalize(curTransform[0].xyz), hlslpp::normalize(prevTransform[0].xyz))) +
            (1.0f - hlslpp::dot(hlslpp::normalize(curTransform[1].xyz), hlslpp::normalize(prevTransform[1].xyz))) +
            (1.0f - hlslpp::dot(hlslpp::normalize(curTransform[2].xyz), hlslpp::normalize(prevTransform[2].xyz)));

        // Compute the difference between the screen-space position of both transforms on their respective projections.
        hlslpp::float4 prevScreenPos = hlslpp::mul(prevTransform[3], prevViewProj);
        hlslpp::float4 curScreenPos = hlslpp::mul(curTransform[3], curViewProj);
        prevScreenPos = (fabs(prevScreenPos.w) < 1e-6f) ? prevScreenPos : prevScreenPos / prevScreenPos.w;
        curScreenPos = (fabs(curScreenPos.w) < 1e-6f) ? curScreenPos : curScreenPos / curScreenPos.w;
        matchResult.screenSpaceDifference = hlslpp::length(curScreenPos.xyz - prevScreenPos.xyz);

        matchResult.valid = true;
        return matchResult;
    }


    // Pokemon Snap port: pair this frame's screen-space rectangles with the
    // previous frame's.
    //
    // Nothing else in the renderer can do it for them. A rectangle draw carries
    // no transform -- RDP::drawRect pins its world matrix range to zero and
    // stores the finished screen box on the call -- so there is no identity to
    // pair by and the scene matcher never visits them. The game's particle
    // effects are all drawn this way, which is why sand kicked up by a Doduo and
    // leaves out of the tall grass held one screen position for a whole game
    // frame while everything around them was interpolated.
    //
    // ORDER is the identity, not position. The first version of this picked the
    // nearest previous rectangle of the same material, and that cannot survive a
    // camera pan: when the whole field of sprites shifts one direction by more
    // than half their spacing, every sprite's nearest neighbour becomes the
    // sprite NEXT to the one it actually was. That is still a consistent
    // one-to-one mapping, so neither a mutual-nearest test nor a
    // motion-consensus test can detect it -- they all agree on the wrong answer.
    // On screen each sprite then slides backwards by one spacing, which is the
    // smearing reported when panning left to right.
    //
    // The game emits these from lists it only ever pushes onto the head of, so
    // between two frames the survivors keep their relative order and anything
    // new appears at one end. Aligning the two sequences from the END therefore
    // pairs like with like regardless of how far the camera moved, and position
    // stops being the selector. It is still used, but only to VALIDATE: a pair
    // whose size or distance is implausible is dropped rather than blended, and
    // a dropped rectangle simply draws the way it did before any of this, which
    // is the honest fallback.
    void GameFrame::matchRects(Workload &curWorkload, const Workload &prevWorkload) {
        const bool timing = snapdiag::statsEnabled();
        const auto matchStart = timing ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        struct MatchTimer {
            bool on;
            std::chrono::steady_clock::time_point start;
            ~MatchTimer() {
                if (on) {
                    snapdiag::rectMatchNanos().fetch_add(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now() - start).count(),
                        std::memory_order_relaxed);
                }
            }
        } matchTimer{timing, matchStart};

        struct RectRef {
            uint32_t callIndex = 0;
            FixedRect rect;
            // Identity, not merely appearance. A shader description is the
            // colour combiner, the other mode and the render flags and
            // nothing else, so sand, leaves, splash and a HUD icon can all
            // share one hash -- and then their orders interleave in a single
            // group and aligning that group pairs sand with leaves. The
            // texture and the buffer being drawn into are already on the
            // call and separate them for nothing.
            uint64_t groupKey = 0;
            float cx = 0.0f;
            float cy = 0.0f;
        };

        thread_local std::vector<RectRef> curRects;
        thread_local std::vector<RectRef> prevRects;
        curRects.clear();
        prevRects.clear();

        const auto gather = [](const Workload &workload, std::vector<RectRef> &rects) {
            for (uint32_t f = 0; f < workload.fbPairCount; f++) {
                const FramebufferPair &fbPair = workload.fbPairs[f];
                for (uint32_t p = 0; p < fbPair.projectionCount; p++) {
                    const Projection &proj = fbPair.projections[p];
                    if (proj.type != Projection::Type::Rectangle) {
                        continue;
                    }

                    for (uint32_t d = 0; d < proj.gameCallCount; d++) {
                        const GameCall &call = proj.gameCalls[d];
                        // The material alone. Two finer identities were tried and
                        // both are wrong for the same reason: they change between
                        // frames, so a sprite stops matching ITSELF.
                        //
                        // The texture changes because these sprites animate -- a
                        // particle steps through its frames as it lives. The
                        // colour buffer changes because the game double buffers,
                        // so consecutive frames are drawn into different
                        // addresses. Either one in the key took pairing from
                        // ninety-nine percent to thirty-six.
                        //
                        // A key may only contain things that are stable across
                        // the pair being matched. That leaves the material, and
                        // order sorts out the rest.
                        const uint64_t key = call.shaderDesc.hash();

                        RectRef ref;
                        ref.callIndex = call.callDesc.callIndex;
                        ref.rect = call.callDesc.rect;
                        ref.groupKey = key;
                        ref.cx = float(call.callDesc.rect.ulx + call.callDesc.rect.lrx) * 0.5f;
                        ref.cy = float(call.callDesc.rect.uly + call.callDesc.rect.lry) * 0.5f;
                        rects.emplace_back(ref);
                    }
                }
            }
        };

        gather(curWorkload, curRects);

        // Sized for every call in the frame and cleared each time, so a pairing
        // can never outlive the frame that made it.
        curWorkload.drawData.rectPairs.assign(curWorkload.gameCallCount, RectPair());

        if (curRects.empty()) {
            return;
        }

        gather(prevWorkload, prevRects);
        if (prevRects.empty()) {
            return;
        }

        // A sprite grows and shrinks as it approaches but not by much in one
        // frame, and it does not teleport. These are validators for a pairing
        // that order already chose, not a search radius, so they can be strict
        // without costing coverage.
        constexpr float SizeTolerance = 0.35f;
        constexpr float TravelLimit = 4.0f;

        const auto plausible = [&](const FixedRect &from, const FixedRect &to) {
            const float fromWidth = float(from.lrx - from.ulx);
            const float fromHeight = float(from.lry - from.uly);
            const float toWidth = float(to.lrx - to.ulx);
            const float toHeight = float(to.lry - to.uly);
            if ((fromWidth <= 0.0f) || (fromHeight <= 0.0f) || (toWidth <= 0.0f) || (toHeight <= 0.0f)) {
                return false;
            }

            if ((std::abs(fromWidth - toWidth) > (toWidth * SizeTolerance)) ||
                (std::abs(fromHeight - toHeight) > (toHeight * SizeTolerance))) {
                return false;
            }

            const float dx = (float(from.ulx) + (fromWidth * 0.5f)) - (float(to.ulx) + (toWidth * 0.5f));
            const float dy = (float(from.uly) + (fromHeight * 0.5f)) - (float(to.uly) + (toHeight * 0.5f));
            const float limit = std::max(toWidth, toHeight) * TravelLimit;
            return ((dx * dx) + (dy * dy)) <= (limit * limit);
        };

        // Grouped by material first: two different effects have nothing to say
        // about each other's order, and grouping also keeps the alignment below
        // linear rather than quadratic across the whole frame.
        thread_local std::unordered_map<uint64_t, std::vector<uint32_t>> curByShader;
        thread_local std::unordered_map<uint64_t, std::vector<uint32_t>> prevByShader;
        curByShader.clear();
        prevByShader.clear();
        for (uint32_t i = 0; i < uint32_t(curRects.size()); i++) {
            curByShader[curRects[i].groupKey].push_back(i);
        }

        for (uint32_t i = 0; i < uint32_t(prevRects.size()); i++) {
            prevByShader[prevRects[i].groupKey].push_back(i);
        }

        // A motion-consensus check was tried here and removed. It rejects a
        // pair whose movement disagrees with the median of its group, which
        // is sound for things carried along by the camera and wrong for
        // these: the particles of one effect do not move together at all --
        // sand flies outward, leaves drift apart -- so there is no agreed
        // motion to measure against, and it threw away two thirds of the
        // pairs it saw. Order chooses the pair; size and travel say whether
        // it is believable. Nothing else here is honest.
        uint32_t pairedCount = 0;
        for (const auto &group : curByShader) {
            const auto prevGroupIt = prevByShader.find(group.first);
            if (prevGroupIt == prevByShader.end()) {
                continue;
            }

            const std::vector<uint32_t> &curGroup = group.second;
            const std::vector<uint32_t> &prevGroup = prevGroupIt->second;

            // Only a group whose membership did not change at all.
            //
            // Order is a sound way to choose a pair only while the two lists
            // hold the same things. The moment one gains or loses a member,
            // every position after it shifts, and the walk that absorbed the
            // difference by stepping one side on was guessing which end the
            // change happened at. Guessing wrong pairs a sprite with its
            // neighbour, which on screen is the leaves out of the tall grass
            // tearing into each other as they are emitted -- and emission is
            // exactly when the count changes.
            //
            // Equal counts, aligned one to one, or nothing. A group that is
            // gaining or losing members simply does not interpolate for that
            // frame and draws where the game put it, which is what it did
            // before any of this and is never wrong -- only less smooth.
            if (curGroup.size() != prevGroup.size()) {
                if (snapdiag::statsEnabled()) {
                    snapdiag::rectGroupSkipCounter().fetch_add(1, std::memory_order_relaxed);
                }

                continue;
            }

            for (size_t k = 0; k < curGroup.size(); k++) {
                const RectRef &cur = curRects[curGroup[k]];
                const RectRef &prev = prevRects[prevGroup[k]];
                if (!plausible(prev.rect, cur.rect)) {
                    continue;
                }

                if (cur.callIndex < curWorkload.drawData.rectPairs.size()) {
                    RectPair &pair = curWorkload.drawData.rectPairs[cur.callIndex];
                    pair.prevRect = prev.rect;
                    pair.paired = true;
                    pairedCount++;
                }
            }
        }

        if (snapdiag::statsEnabled()) {
            snapdiag::rectDrawCounter().fetch_add(uint32_t(curRects.size()), std::memory_order_relaxed);
            snapdiag::rectsPairedCounter().fetch_add(pairedCount, std::memory_order_relaxed);
        }
    }

    void GameFrame::match(RenderWorker *worker, WorkloadQueue &workloadQueue, const GameFrame &prevFrame, BufferUploader *velocityUploader, bool &velocityUploaderUsed, bool &tileInterpolationUsed, bool &lookAtInterpolationUsed) {
        tileInterpolationUsed = false;
        lookAtInterpolationUsed = false;
        matched = true;

        // Pokemon Snap port: the game moves its world origin as the rail crosses
        // between blocks. enterNextBlock translates the camera and everything in
        // the world by the difference between the two blocks' positions, which is
        // a couple of thousand units across a corner. Both frames are correct and
        // describe the same scene; they are measured about different origins, so
        // there is no motion between them to interpolate.
        //
        // With the distance recorded here, the previous frame's transforms and
        // view can be read in the new origin, so everything that survived the
        // transition interpolates as it does on any other frame. The camera is
        // tagged with its own matrix group (src/matrix_tags.cpp), whose skip
        // components already snap the view on the frame the game's camera data
        // jumped -- which a rebase always is -- so nothing here decides cuts.
        snapRebaseFrame = false;
        snapOriginDelta = hlslpp::float3(0.0f, 0.0f, 0.0f);
        for (uint32_t w : workloads) {
            Workload &rebaseWorkload = workloadQueue.workloads[w];
            if (rebaseWorkload.snapOriginRebased) {
                rebaseWorkload.snapOriginRebased = false;
                snapRebaseFrame = true;
                snapOriginDelta = rebaseWorkload.snapOriginDelta;
            }
        }

        // With the distance known, the previous frame can be moved into this
        // frame's origin instead of being held: expressed the same way, the two
        // differ only by whatever genuinely moved, and everything interpolates
        // as it does on any other frame. Each candidate is checked individually
        // and only taken when it lands nearer than the unmoved one, so a delta
        // that is wrong, stale, or absent simply is not used.

        thread_local std::unordered_map<uint32_t, ModifiedBuffers> workloadsModified;
        workloadsModified.clear();

        for (uint32_t w = 0; w < workloads.size(); w++) {
            if (w >= prevFrame.workloads.size()) {
                continue;
            }

            // We assume the workloads will be detected in the same order between frames.
            GameFrameMap::WorkloadMap &workloadMap = frameMap.workloads[workloads[w]];
            workloadMap.prevWorkloadIndex = prevFrame.workloads[w];
            workloadMap.mapped = true;

            Workload &curWorkload = workloadQueue.workloads[workloads[w]];
            const Workload &prevWorkload = workloadQueue.workloads[workloadMap.prevWorkloadIndex];
            workloadMap.viewProjections.clear();
            workloadMap.viewProjections.resize(curWorkload.drawData.viewProjTransforms.size());
            workloadMap.transforms.clear();
            workloadMap.transforms.resize(curWorkload.drawData.worldTransforms.size());
            workloadMap.tiles.clear();
            workloadMap.tiles.resize(curWorkload.drawData.rdpTiles.size());
            workloadMap.lookAt.clear();
            workloadMap.lookAt.resize(curWorkload.drawData.rspLookAt.size());
            workloadMap.prevTransformsMapped.clear();
            workloadMap.prevTransformsMapped.resize(prevWorkload.drawData.worldTransforms.size());
            workloadMap.prevTilesMapped.clear();
            workloadMap.prevTilesMapped.resize(prevWorkload.drawData.rdpTiles.size());
            workloadMap.prevLookAtMapped.clear();
            workloadMap.prevLookAtMapped.resize(prevWorkload.drawData.rspLookAt.size());

            buildTransformIdMap(curWorkload, curWorkload.transformIdMap, curWorkload.transformIgnoredIds);

            // Retrieve the matching maps for the current and previous workload.
            GameFrameMap::WorkloadMap &curWorkloadMap = frameMap.workloads[workloads[w]];
            const GameFrameMap::WorkloadMap *prevWorkloadMap = nullptr;
            if (prevFrame.matched && prevFrame.frameMap.workloads[workloadMap.prevWorkloadIndex].mapped) {
                prevWorkloadMap = &prevFrame.frameMap.workloads[workloadMap.prevWorkloadIndex];
            }

            // Match the transforms linearly in the order they were submitted.
            ModifiedBuffers modifiedBuffers;
            auto curIt = curWorkload.transformIdMap.begin();
            auto prevIt = prevWorkload.transformIdMap.begin();
            bool modifiedVelocityBuffer = false;
            while ((curIt != curWorkload.transformIdMap.end()) && (prevIt != prevWorkload.transformIdMap.end())) {
                if (curIt->first < prevIt->first) {
                    curIt++;
                }
                else if (curIt->first > prevIt->first) {
                    prevIt++;
                }
                else {
                    matchTransform(curWorkload, prevWorkload, curWorkloadMap, prevWorkloadMap, curIt->second, prevIt->second, modifiedBuffers, true);
                    curIt++;
                    prevIt++;
                }
            }

            if (!modifiedBuffers.empty()) {
                workloadsModified[workloads[w]].merge(modifiedBuffers);
            }

            // Only when this frame is a continuation of the last one. The
            // rectangles carry no identity of their own, so the whole method
            // rests on the two frames being the same scene a moment apart;
            // across a scene change the previous frame is a different screen
            // entirely and its interface elements pair happily with the new
            // one's, which is a menu smearing into a course as it loads.
            //
            // The 3D pairing rate is the test, because it is measured on
            // identity rather than guessed at: it sits at 96 to 98 percent
            // through ordinary play and collapses to under 40 at a
            // transition. If the world did not carry over, neither did the
            // things drawn on top of it.
            {
                uint32_t seen = 0;
                uint32_t paired = 0;
                for (const auto &tm : curWorkloadMap.transforms) {
                    seen++;
                    if (tm.mapped) {
                        paired++;
                    }
                }

                if ((seen == 0) || ((paired * 2) >= seen)) {
                    matchRects(curWorkload, prevWorkload);
                }
                else if (snapdiag::statsEnabled()) {
                    snapdiag::rectSceneSkipCounter().fetch_add(1, std::memory_order_relaxed);
                }
            }

            if (snapdiag::statsEnabled()) {
                uint32_t seen = 0, paired = 0;
                for (const auto &tm : curWorkloadMap.transforms) {
                    seen++;
                    if (tm.mapped) {
                        paired++;
                    }
                }

                snapdiag::transformsSeenCounter().fetch_add(seen, std::memory_order_relaxed);
                snapdiag::transformsPairedCounter().fetch_add(paired, std::memory_order_relaxed);
            }

            // Any transforms tagged with the empty ID will be instantly marked as used and skipped.
            for (uint32_t curIt : curWorkload.transformIgnoredIds) {
                GameFrameMap::TransformMap &curTransformMap = curWorkloadMap.transforms[curIt];
                curTransformMap.rigidBody = RigidBody();
                curTransformMap.prevTransformIndex = 0;
                curTransformMap.mapped = false;
            }

        }

        thread_local std::vector<MatchCandidate> matchCandidates;
        thread_local std::vector<bool> curScenesMatched;
        thread_local std::vector<bool> prevScenesMatched;
        auto matchScenes = [&](const std::vector<GameScene> &curScenes, const std::vector<GameScene> &prevScenes) {
            // Find the scenes that are the closest match possible.
            matchCandidates.clear();
            for (uint32_t i = 0; i < curScenes.size(); i++) {
                const GameIndices::Projection curFirstProj = curScenes[i].projections[0];
                const Workload &curWorkload = workloadQueue.workloads[curFirstProj.workloadIndex];
                const FramebufferPair &curFbPair = curWorkload.fbPairs[curFirstProj.fbPairIndex];
                const Projection &curProj = curFbPair.projections[curFirstProj.projectionIndex];
                const hlslpp::float4x4 &curViewTransform = curWorkload.drawData.viewTransforms[curProj.transformsIndex];
                const hlslpp::float4x4 &curProjTransform = curWorkload.drawData.projTransforms[curProj.transformsIndex];
                for (uint32_t j = 0; j < prevScenes.size(); j++) {
                    const GameIndices::Projection prevFirstProj = prevScenes[j].projections[0];
                    const Workload &prevWorkload = workloadQueue.workloads[prevFirstProj.workloadIndex];
                    const FramebufferPair &prevFbPair = prevWorkload.fbPairs[prevFirstProj.fbPairIndex];
                    const Projection &prevProj = prevFbPair.projections[prevFirstProj.projectionIndex];
                    const hlslpp::float4x4 &prevViewTransform = prevWorkload.drawData.viewTransforms[prevProj.transformsIndex];
                    const hlslpp::float4x4 &prevProjTransform = prevWorkload.drawData.projTransforms[prevProj.transformsIndex];
                    matchCandidates.emplace_back(i, j, matrixDifference(curViewTransform, prevViewTransform) + matrixDifference(curProjTransform, prevProjTransform));
                }
            }

            curScenesMatched.clear();
            prevScenesMatched.clear();
            curScenesMatched.resize(curScenes.size());
            prevScenesMatched.resize(prevScenes.size());
            std::stable_sort(matchCandidates.begin(), matchCandidates.end());
            for (const MatchCandidate &candidate : matchCandidates) {
                if (curScenesMatched[candidate.curIndex]) {
                    continue;
                }

                if (prevScenesMatched[candidate.prevIndex]) {
                    continue;
                }

                matchScene(workloadQueue, prevFrame, curScenes[candidate.curIndex], prevScenes[candidate.prevIndex], workloadsModified, tileInterpolationUsed, lookAtInterpolationUsed);
                curScenesMatched[candidate.curIndex] = true;
                prevScenesMatched[candidate.prevIndex] = true;
            }
        };

        matchScenes(perspectiveScenes, prevFrame.perspectiveScenes);
        matchScenes(orthographicScenes, prevFrame.orthographicScenes);

        if (!workloadsModified.empty()) {
            thread_local std::vector<BufferUploader::Upload> uploads;
            uploads.clear();

            for (auto &it : workloadsModified) {
                Workload &workload = workloadQueue.workloads[it.first];
                if (it.second.positionVelocity) {
                    uploads.emplace_back(BufferUploader::Upload{ workload.drawData.velFloats.data(), { 0, workload.drawData.velFloats.size() }, sizeof(float), RenderBufferFlag::FORMATTED, { RenderFormat::R32_FLOAT }, &workload.drawBuffers.velocityBuffer });
                }

                if (it.second.texcoordVelocity) {
                    uploads.emplace_back(BufferUploader::Upload{ workload.drawData.tcVelFloats.data(), { 0, workload.drawData.tcVelFloats.size() }, sizeof(float), RenderBufferFlag::FORMATTED, { RenderFormat::R32_FLOAT }, &workload.drawBuffers.texcoordVelocityBuffer });
                }
            }

            velocityUploader->submit(worker, uploads);
            velocityUploaderUsed = true;
        }
        else {
            velocityUploaderUsed = false;
        }

        // Pokemon Snap port: an object is judged as an object. The automatic
        // pose guard judges each matrix alone, on its own velocity, and a
        // model's matrices never move alike -- a limb swinging up from rest
        // crosses the guard's threshold on a tick the torso carrying it does
        // not. The guard then snaps that one matrix to its new pose while the
        // rest of the model blends towards it, and the model comes apart for
        // the frame: the camera prop rising out of Todd's hand, a walking
        // model ghosting against itself. Measured, this is the common case,
        // not the rare one -- a handful of matrices out of a hundred and
        // fifty, several times a second.
        //
        // A matrix cannot be judged alone because it is not alone: it is one
        // joint of one object, and the object either jumped or it did not.
        // The game names the object each matrix belongs to
        // (patches/src/render_patch.c) and the verdict is taken across it:
        // when most of an object's matrices call it a teleport the object
        // really did move somewhere else and all of it snaps, and when only a
        // few do they are the guard misreading fast animation, so all of it
        // blends. Either way every part of the object agrees, which is the
        // only way it can look like one object.
        {
            struct CoherenceTally {
                uint32_t total = 0;
                uint32_t rejected = 0;
                bool snap = false;
            };
            thread_local std::unordered_map<uint32_t, CoherenceTally> coherenceTallies;
            for (uint32_t w : workloads) {
                Workload &workload = workloadQueue.workloads[w];
                GameFrameMap::WorkloadMap &wm = frameMap.workloads[w];
                if (!wm.mapped) {
                    continue;
                }
                const auto &groupIndices = workload.drawData.worldTransformGroups;
                const size_t transformCount = std::min(wm.transforms.size(), groupIndices.size());
                coherenceTallies.clear();
                for (size_t t = 0; t < transformCount; t++) {
                    const uint32_t coherenceId = workload.drawData.transformGroups[groupIndices[t]].coherenceId;
                    const GameFrameMap::TransformMap &tm = wm.transforms[t];
                    if ((coherenceId == 0) || !tm.mapped) {
                        continue;
                    }
                    CoherenceTally &tally = coherenceTallies[coherenceId];
                    tally.total++;
                    if (tm.rigidBody.autoRejectedTranslation) {
                        tally.rejected++;
                    }
                }
                uint32_t snappedObjects = 0, freedObjects = 0, freedMatrices = 0;
                float coherenceLiftedWorst = 0.0f;
                // An object the game's own animation says stepped to this pose
                // snaps whole, whatever the distance. The three defences below
                // all ask how MUCH moved, and a step can be small: the camera
                // in Todd's hand moves two and a half units and is unmistakable
                // because it leaves his hand. Magnitude was never the question;
                // the game already knows the answer and says so in the display
                // list.
                uint32_t steppedMatched = 0;
                for (auto &it : coherenceTallies) {
                    if (workload.snapHasSteppedId(it.first)) {
                        it.second.rejected = it.second.total;
                        steppedMatched++;
                    }
                }
                if ((workload.snapSteppedIdCount > 0) && (steppedMatched == 0) &&
                    (snapdiag::diagEnabled() || snapdiag::statsEnabled())) {
                    // The game named an object that stepped and the renderer is
                    // tracking no transform belonging to it. The verdict is
                    // correct and lands on nothing, which is worth saying out
                    // loud rather than looking like a fix that works.
                    fprintf(stdout, "[SNAP-STEP] verdict for %u object(s) matched no tracked transform (%u groups tracked)\n",
                        workload.snapSteppedIdCount, uint32_t(coherenceTallies.size()));
                    fflush(stdout);
                }
                for (auto &it : coherenceTallies) {
                    // The line sits high on purpose. A world transform is
                    // composed down the object's hierarchy, so an object that
                    // truly moved elsewhere carries the jump into every matrix
                    // beneath the one that moved -- a real teleport rejects
                    // nearly all of them, and the measured staging ticks do
                    // (a hundred and twenty to a hundred and seventy of a
                    // hundred and fifty). Anything short of that is the guard
                    // misreading animation, and snapping a whole object on a
                    // minority of votes would step objects that never moved.
                    it.second.snap = (it.second.rejected * 4) >= (it.second.total * 3);
                    if (it.second.rejected == 0) {
                        continue;
                    }
                    if (it.second.snap) {
                        snappedObjects++;
                    }
                    else {
                        freedObjects++;
                        freedMatrices += it.second.rejected;
                    }
                }
                if ((snappedObjects == 0) && (freedObjects == 0)) {
                    continue;
                }
                for (size_t t = 0; t < transformCount; t++) {
                    const uint32_t curGroupIndex = groupIndices[t];
                    const TransformGroup &curGroup = workload.drawData.transformGroups[curGroupIndex];
                    if (curGroup.coherenceId == 0) {
                        continue;
                    }
                    GameFrameMap::TransformMap &tm = wm.transforms[t];
                    if (!tm.mapped) {
                        continue;
                    }
                    const auto tallyIt = coherenceTallies.find(curGroup.coherenceId);
                    if (tallyIt == coherenceTallies.end()) {
                        continue;
                    }
                    RigidBody &rigidBody = tm.rigidBody;
                    if (tallyIt->second.snap) {
                        rigidBody.autoRejectedTranslation = true;
                        rigidBody.lerpTranslation = false;
                        rigidBody.lerpRotation = false;
                        rigidBody.lerpScale = false;
                        rigidBody.lerpSkew = false;
                        rigidBody.lerpPerspective = false;
                    }
                    else if (rigidBody.autoRejectedTranslation) {
                        // Lifting a rejection asserts that the guard misread
                        // fast animation, and that only holds for a pair that
                        // is a plausible step of one object. A pair further
                        // apart than anything this game animates in a tick is
                        // not that: it is a matrix reissued to a new object,
                        // or two objects taken for each other. Blending such a
                        // pair draws the new arrival sliding in from wherever
                        // its predecessor stood -- a Pokemon visibly
                        // travelling to its spawn point, seen only with
                        // interpolation on, because only then is anything
                        // blended at all. Those stay snapped, which is what
                        // an object with no previous pose should do.
                        const float stepDistance = float(hlslpp::length(rigidBody.linearVelocity));
                        coherenceLiftedWorst = std::max(coherenceLiftedWorst, stepDistance);
                        if (stepDistance > 60.0f) {
                            continue;
                        }
                        // Restores exactly what the component modes ask for
                        // with the rejection lifted, which for this game's
                        // model tags (automatic position and rotation, scale
                        // and skew following rotation) is a plain blend.
                        const auto blends = [](uint8_t mode) {
                            return (mode == G_EX_COMPONENT_INTERPOLATE) || (mode == G_EX_COMPONENT_AUTO);
                        };
                        rigidBody.autoRejectedTranslation = false;
                        rigidBody.snapDiscontinuityLatch = false;
                        rigidBody.snapLatchAcceptStreak = 0;
                        rigidBody.lerpTranslation = blends(curGroup.positionInterpolation);
                        rigidBody.lerpRotation = blends(curGroup.rotationInterpolation);
                        rigidBody.lerpScale = blends(curGroup.scaleInterpolation);
                        rigidBody.lerpSkew = blends(curGroup.skewInterpolation);
                        rigidBody.lerpPerspective = (curGroup.perspectiveInterpolation == G_EX_COMPONENT_INTERPOLATE);
                    }
                }
                if ((snapdiag::diagEnabled() || snapdiag::statsEnabled()) && ((snappedObjects + freedObjects) > 0)) {
                    fprintf(stdout, "[SNAP-COH] objects snapped whole %u (game declared %u of them a step), rejections considered %u across %u objects, furthest %.1f\n",
                        snappedObjects, workload.snapSteppedIdCount, freedMatrices, freedObjects, coherenceLiftedWorst);
                }
            }
        }


    }

    void GameFrame::matchScene(WorkloadQueue &workloadQueue, const GameFrame &prevFrame, const GameScene &curScene, const GameScene &prevScene, std::unordered_map<uint32_t, ModifiedBuffers> &workloadsModified, bool &tileInterpolationUsed, bool &lookAtInterpolationUsed) {
        if (curScene.projections.empty() || prevScene.projections.empty()) {
            return;
        }

        thread_local std::multimap<uint64_t, GameCallMap> curCallHashMap;
        thread_local std::multimap<uint64_t, GameCallMap> prevCallHashMap;
        curCallHashMap.clear();
        prevCallHashMap.clear();

        // Pick the first projection for reference of the scene.
        const GameIndices::Projection &firstCurProjIndices = curScene.projections[0];
        const GameIndices::Projection &firstPrevProjIndices = prevScene.projections[0];
        GameFrameMap::WorkloadMap &firstCurWorkloadMap = frameMap.workloads[firstCurProjIndices.workloadIndex];
        Workload &firstCurWorkload = workloadQueue.workloads[firstCurProjIndices.workloadIndex];
        const FramebufferPair &firstCurFbPair = firstCurWorkload.fbPairs[firstCurProjIndices.fbPairIndex];
        const Projection &firstCurProj = firstCurFbPair.projections[firstCurProjIndices.projectionIndex];
        const Workload &firstPrevWorkload = workloadQueue.workloads[firstPrevProjIndices.workloadIndex];
        const FramebufferPair &firstPrevFbPair = firstPrevWorkload.fbPairs[firstPrevProjIndices.fbPairIndex];
        const Projection &firstPrevProj = firstPrevFbPair.projections[firstPrevProjIndices.projectionIndex];
        const GameFrameMap::WorkloadMap *firstPrevWorkloadMap = nullptr;
        if (prevFrame.matched && prevFrame.frameMap.workloads[firstPrevProjIndices.workloadIndex].mapped) {
            firstPrevWorkloadMap = &prevFrame.frameMap.workloads[firstPrevProjIndices.workloadIndex];
        }

        // Build a multimap with all potential compatibilities between draw calls in the projections.
        uint32_t mappedViewProjIndex = UINT32_MAX;
        for (uint32_t p = 0; p < curScene.projections.size(); p++) {
            const GameIndices::Projection &curProjIndices = curScene.projections[p];
            Workload &curWorkload = workloadQueue.workloads[curProjIndices.workloadIndex];
            const FramebufferPair &curFbPair = curWorkload.fbPairs[curProjIndices.fbPairIndex];
            const Projection &curProj = curFbPair.projections[curProjIndices.projectionIndex];
            buildCallHashMap(p, curWorkload, curProj, curCallHashMap);

            // Projection for the entire scene has been matched already, copy the mapping.
            if (mappedViewProjIndex < UINT32_MAX) {
                firstCurWorkloadMap.viewProjections[curProj.transformsIndex] = firstCurWorkloadMap.viewProjections[mappedViewProjIndex];
            }

            // Can't map to a previous projection.
            if (p >= prevScene.projections.size()) {
                continue;
            }

            if (mappedViewProjIndex == UINT32_MAX) {
                GameFrameMap::ViewProjectionMap &viewProjMap = firstCurWorkloadMap.viewProjections[curProj.transformsIndex];
                if (viewProjMap.mapped) {
                    mappedViewProjIndex = curProj.transformsIndex;
                    continue;
                }

                const GameIndices::Projection &prevProjIndices = prevScene.projections[p];
                const Workload &prevWorkload = workloadQueue.workloads[prevProjIndices.workloadIndex];
                const FramebufferPair &prevFbPair = prevWorkload.fbPairs[prevProjIndices.fbPairIndex];
                const Projection &prevProj = prevFbPair.projections[prevProjIndices.projectionIndex];
                const hlslpp::float4x4 &curView = curWorkload.drawData.viewTransforms[curProj.transformsIndex];
                const hlslpp::float4x4 &prevView = prevWorkload.drawData.viewTransforms[prevProj.transformsIndex];
                const uint32_t curProjGroupIndex = curWorkload.drawData.viewProjTransformGroups[curProj.transformsIndex];
                const uint32_t prevProjGroupIndex = prevWorkload.drawData.viewProjTransformGroups[prevProj.transformsIndex];
                const TransformGroup &curProjGroup = curWorkload.drawData.transformGroups[curProjGroupIndex];
                const TransformGroup &prevProjGroup = prevWorkload.drawData.transformGroups[prevProjGroupIndex];

                // Cameras usually look better with simple interpolation, so default decomposition to off.
                bool projectionDecompose = false;
                uint8_t projectionLinearComponent = G_EX_COMPONENT_INTERPOLATE;
                uint8_t projectionAngularComponent = G_EX_COMPONENT_INTERPOLATE;
                uint8_t projectionScaleComponent = G_EX_COMPONENT_INTERPOLATE;
                uint8_t projectionSkewComponent = G_EX_COMPONENT_INTERPOLATE;
                uint8_t projectionPerspectiveComponent = G_EX_COMPONENT_INTERPOLATE;
                if ((curProjGroup.matrixId != G_EX_ID_IGNORE) && (curProjGroup.matrixId != G_EX_ID_AUTO)) {
                    projectionDecompose = curProjGroup.decompose;
                    projectionLinearComponent = curProjGroup.positionInterpolation;
                    projectionAngularComponent = curProjGroup.rotationInterpolation;
                    projectionScaleComponent = curProjGroup.scaleInterpolation;
                    projectionSkewComponent = curProjGroup.skewInterpolation;
                    projectionPerspectiveComponent = curProjGroup.perspectiveInterpolation;
                    viewProjMap.mapped = (curProjGroup.matrixId == prevProjGroup.matrixId);
                }
                else {
                    viewProjMap.mapped = (curProjGroup.matrixId == G_EX_ID_AUTO);
                }

                // On the frame the origin moves without a usable delta, let the
                // camera's translation be judged the way every tagged matrix
                // already judges its own, so it declines the shift instead of
                // sliding across it. Applied after the group modes: the camera
                // is tagged now, and its group cannot know about the rebase.
                if (snapRebaseFrame && !snapRebaseUsable()) {
                    projectionLinearComponent = G_EX_COMPONENT_AUTO;
                }

                if (viewProjMap.mapped) {
                    if (firstPrevWorkloadMap != nullptr) {
                        const GameFrameMap::ViewProjectionMap &prevViewProjMap = firstPrevWorkloadMap->viewProjections[prevProj.transformsIndex];
                        viewProjMap.rigidBody = prevViewProjMap.rigidBody;
                    }

                    // The camera was translated along with everything else, so
                    // its previous position has to be read in the new origin too.
                    // A point given in the new origin is the old one plus the
                    // delta, so undoing it before the old view is what lines the
                    // two frames up.
                    hlslpp::float4x4 rebasedPrevView;
                    const hlslpp::float4x4 *effectivePrevView = &prevView;
                    if (snapRebaseUsable()) {
                        rebasedPrevView = hlslpp::mul(matrixTranslation(-snapOriginDelta), prevView);

                        // The translation row of a view matrix is the eye
                        // position already rotated through the view, so
                        // distances taken on it mix the rotation in and say
                        // nothing about how far the camera moved. Inverting
                        // gives the eye itself, which is what the rebase
                        // shifted; the rebased view's eye is by construction
                        // the old eye plus the delta.
                        const hlslpp::float3 curEye = hlslpp::inverse(curView)[3].xyz;
                        const hlslpp::float3 prevEye = hlslpp::inverse(prevView)[3].xyz;
                        const hlslpp::float3 rebasedPrevEye = prevEye + snapOriginDelta;
                        if (float(hlslpp::length(curEye - rebasedPrevEye)) <
                            float(hlslpp::length(curEye - prevEye))) {
                            effectivePrevView = &rebasedPrevView;
                            viewProjMap.snapRebasedPrev = true;
                        }
                    }

                    // At a block transition the camera's own numbers jump by
                    // the origin delta, so the game-side hook tags this frame
                    // as a cut -- it cannot know the shift is a bookkeeping
                    // move. Here the rebased previous view has already been
                    // accepted as lining up with the current one, so the jump
                    // the hook measured no longer exists in what
                    // interpolation sees: blend, and the ride glides through
                    // the corner. Genuine cuts never rebase, so their skip
                    // stands.
                    if (viewProjMap.snapRebasedPrev) {
                        if (projectionLinearComponent == G_EX_COMPONENT_SKIP) {
                            projectionLinearComponent = G_EX_COMPONENT_INTERPOLATE;
                        }
                        if (projectionAngularComponent == G_EX_COMPONENT_SKIP) {
                            projectionAngularComponent = G_EX_COMPONENT_INTERPOLATE;
                        }
                    }


                    viewProjMap.rigidBody.updateLinear(*effectivePrevView, curView, projectionLinearComponent);
                    viewProjMap.rigidBody.updateAngular(*effectivePrevView, curView, projectionAngularComponent, projectionScaleComponent, projectionSkewComponent);
                    viewProjMap.rigidBody.updateDecomposition(*effectivePrevView, curView, projectionDecompose);
                    viewProjMap.prevTransformIndex = prevProj.transformsIndex;
                }
                else {
                    viewProjMap.rigidBody = RigidBody();
                }

                if (viewProjMap.mapped) {
                    mappedViewProjIndex = curProj.transformsIndex;
                }
            }
        }

        for (uint32_t p = 0; p < prevScene.projections.size(); p++) {
            const GameIndices::Projection &prevProjIndices = prevScene.projections[p];
            const Workload &prevWorkload = workloadQueue.workloads[prevProjIndices.workloadIndex];
            const FramebufferPair &prevFbPair = prevWorkload.fbPairs[prevProjIndices.fbPairIndex];
            const Projection &prevProj = prevFbPair.projections[prevProjIndices.projectionIndex];
            buildCallHashMap(p, prevWorkload, prevProj, prevCallHashMap);
        }

        // FIXME: Transform set needs to be done per unique workload detected.
        thread_local std::set<IndexPair> transformCheckSet;
        thread_local std::set<IndexPair> tileCheckSet;
        thread_local std::set<IndexPair> lookAtCheckSet;
        transformCheckSet.clear();
        tileCheckSet.clear();
        lookAtCheckSet.clear();

        // Traverse the map and fill the set with all the combinations of transforms to check.
        for (std::pair<uint64_t, GameCallMap> curIt : curCallHashMap) {
            auto prevRange = prevCallHashMap.equal_range(curIt.first);
            for (auto prevIt = prevRange.first; prevIt != prevRange.second; prevIt++) {
                const GameIndices::Projection &curProjIndices = curScene.projections[curIt.second.sceneProjIndex];
                const Workload &curWorkload = workloadQueue.workloads[curProjIndices.workloadIndex];
                const FramebufferPair &curFbPair = curWorkload.fbPairs[curProjIndices.fbPairIndex];
                const Projection &curProj = curFbPair.projections[curProjIndices.projectionIndex];
                const GameCall &curCall = curProj.gameCalls[curIt.second.callIndex];
                const GameIndices::Projection &prevProjIndices = prevScene.projections[prevIt->second.sceneProjIndex];
                const Workload &prevWorkload = workloadQueue.workloads[prevProjIndices.workloadIndex];
                const FramebufferPair &prevFbPair = prevWorkload.fbPairs[prevProjIndices.fbPairIndex];
                const Projection &prevProj = prevFbPair.projections[prevProjIndices.projectionIndex];
                const GameCall &prevCall = prevProj.gameCalls[prevIt->second.callIndex];
                const uint32_t curWorldMatrixCount = (curCall.callDesc.maxWorldMatrix - curCall.callDesc.minWorldMatrix) + 1;
                const uint32_t prevWorldMatrixCount = (prevCall.callDesc.maxWorldMatrix - prevCall.callDesc.minWorldMatrix) + 1;
                if ((curWorldMatrixCount == prevWorldMatrixCount) && curIt.second.doTransformMatching && prevIt->second.doTransformMatching) {
                    for (uint32_t w = 0; w < curWorldMatrixCount; w++) {
                        const uint32_t curWorldMatrix = curCall.callDesc.minWorldMatrix + w;
                        const uint32_t curGroupIndex = curWorkload.drawData.worldTransformGroups[curWorldMatrix];
                        const TransformGroup &curGroup = curWorkload.drawData.transformGroups[curGroupIndex];
                        const uint32_t prevWorldMatrix = prevCall.callDesc.minWorldMatrix + w;
                        const uint32_t prevGroupIndex = prevWorkload.drawData.worldTransformGroups[prevWorldMatrix];
                        const TransformGroup &prevGroup = prevWorkload.drawData.transformGroups[prevGroupIndex];
                        if ((curGroup.matrixId == prevGroup.matrixId) && ((curGroup.matrixId == G_EX_ID_AUTO) || ((curGroup.matrixId != G_EX_ID_IGNORE) && (curGroup.ordering == G_EX_ORDER_AUTO)))) {
                            transformCheckSet.emplace(curWorldMatrix, prevWorldMatrix);
                        }
                    }
                }

                if ((curCall.callDesc.tileCount == prevCall.callDesc.tileCount) && (curIt.second.doTileInterpolation && prevIt->second.doTileInterpolation)) {
                    for (uint32_t t = 0; t < curCall.callDesc.tileCount; t++) {
                        const DrawCallTile &curCallTile = curWorkload.drawData.callTiles[curCall.callDesc.tileIndex + t];
                        const DrawCallTile &prevCallTile = prevWorkload.drawData.callTiles[prevCall.callDesc.tileIndex + t];
                        bool doTileMatching = curIt.second.doTileMatching && prevIt->second.doTileMatching;
                        if (doTileMatching && (curCallTile.tmemHashOrID != prevCallTile.tmemHashOrID)) {
                            continue;
                        }

                        tileCheckSet.emplace(curCall.callDesc.tileIndex + t, prevCall.callDesc.tileIndex + t);
                    }
                }

                const uint32_t textureGenMask = G_LIGHTING | G_TEXTURE_GEN;
                const bool curUsesTextureGen = (curCall.callDesc.geometryMode & textureGenMask) == textureGenMask;
                const bool prevUsesTextureGen = (prevCall.callDesc.geometryMode & textureGenMask) == textureGenMask;
                if (curUsesTextureGen && prevUsesTextureGen && (true && true)) { // TODO: Do look at matching condition.
                    // FIXME: We assume the same look at is used throughout the entire draw call, so we only check the first vertex.
                    const uint32_t curVertexIndex = curWorkload.drawData.faceIndices[curCall.meshDesc.faceIndicesStart];
                    const uint32_t prevVertexIndex = prevWorkload.drawData.faceIndices[prevCall.meshDesc.faceIndicesStart];
                    const uint32_t curLookAtIndex = curWorkload.drawData.lookAtIndices[curVertexIndex] >> RSP_LOOKAT_INDEX_SHIFT;
                    const uint32_t prevLookAtIndex = prevWorkload.drawData.lookAtIndices[prevVertexIndex] >> RSP_LOOKAT_INDEX_SHIFT;
                    lookAtCheckSet.emplace(curLookAtIndex, prevLookAtIndex);
                }
            }
        }

        // Compute all the differences between transforms and insert them into a vector that will be sorted according to the differences.
        thread_local std::vector<MatchCandidate> matchCandidates;
        matchCandidates.clear();

        const RigidBody *prevRigidBody;
        const hlslpp::float4x4 &firstCurViewProj = firstCurWorkload.drawData.viewProjTransforms[firstCurProj.transformsIndex];
        const hlslpp::float4x4 &firstPrevViewProj = firstPrevWorkload.drawData.viewProjTransforms[firstPrevProj.transformsIndex];

        // On the frame the world origin moves, candidates have to be judged with
        // the previous frame read in the new origin, or every distance carries
        // the whole shift and the nearest previous transform to anything is the
        // wrong one: with the world moved further than the spacing between
        // repeated objects, a fence post's best raw match is a different post.
        // The per-pair check in matchTransform re-decides against the raw data,
        // so this only makes selection see the same geometry it will.
        hlslpp::float4x4 rebasedFirstPrevViewProj;
        const hlslpp::float4x4 *effectivePrevViewProj = &firstPrevViewProj;
        const bool rebasingCandidates = snapRebaseUsable();
        if (rebasingCandidates) {
            rebasedFirstPrevViewProj = hlslpp::mul(matrixTranslation(-snapOriginDelta), firstPrevViewProj);
            effectivePrevViewProj = &rebasedFirstPrevViewProj;
        }

        for (const IndexPair &indices : transformCheckSet) {
            const hlslpp::float4x4 &curTransform = firstCurWorkload.drawData.worldTransforms[indices.first];
            hlslpp::float4x4 prevTransform = firstPrevWorkload.drawData.worldTransforms[indices.second];
            if (rebasingCandidates) {
                prevTransform[3].xyz = prevTransform[3].xyz + snapOriginDelta;
            }
            prevRigidBody = (firstPrevWorkloadMap != nullptr) ? &firstPrevWorkloadMap->transforms[indices.second].rigidBody : nullptr;

            TransformMatchResult matchResult = computeTransformMatch(curTransform, firstCurViewProj, prevTransform, *effectivePrevViewProj, prevRigidBody);
            if (matchResult.valid) {
                matchCandidates.emplace_back(indices.first, indices.second, matchResult.computeDifference());
            }
        }

        ModifiedBuffers modifiedBuffers;
        std::stable_sort(matchCandidates.begin(), matchCandidates.end());
        for (const MatchCandidate candidate : matchCandidates) {
            if (firstCurWorkloadMap.transforms[candidate.curIndex].mapped) {
                continue;
            }

            if (firstCurWorkloadMap.prevTransformsMapped[candidate.prevIndex]) {
                continue;
            }

            matchTransform(firstCurWorkload, firstPrevWorkload, firstCurWorkloadMap, firstPrevWorkloadMap, candidate.curIndex, candidate.prevIndex, modifiedBuffers, true);
        }

        if (!modifiedBuffers.empty()) {
            workloadsModified[firstCurProjIndices.workloadIndex].merge(modifiedBuffers);
        }

        // Check for tile matches.
        for (const IndexPair &indices : tileCheckSet) {
            if (firstCurWorkloadMap.tiles[indices.first].mapped) {
                continue;
            }

            if (firstCurWorkloadMap.prevTilesMapped[indices.second]) {
                continue;
            }

            // Check for tile compatibility.
            const interop::RDPTile &curTile = firstCurWorkload.drawData.rdpTiles[indices.first];
            const interop::RDPTile &prevTile = firstPrevWorkload.drawData.rdpTiles[indices.second];
            if ((curTile.fmt != prevTile.fmt) ||
                (curTile.siz != prevTile.siz) ||
                (curTile.stride != prevTile.stride) ||
                (curTile.masks != prevTile.masks) ||
                (curTile.maskt != prevTile.maskt) ||
                (curTile.shifts != prevTile.shifts) ||
                (curTile.shiftt != prevTile.shiftt) ||
                (curTile.cms != prevTile.cms) ||
                (curTile.cmt != prevTile.cmt))
            {
                continue;
            }

            GameFrameMap::TileMap &curTileMap = firstCurWorkloadMap.tiles[indices.first];
            if (firstPrevWorkloadMap != nullptr) {
                const GameFrameMap::TileMap &prevTileMap = firstPrevWorkloadMap->tiles[indices.second];
                curTileMap = prevTileMap;
            }
            
            auto modulo = [](int a, int b) {
                if (b != 0) {
                    int r = a % b;
                    return r < 0 ? r + b : r;
                }
                else {
                    return a;
                }
            };

            const float deltaUls = curTile.uls - prevTile.uls;
            const float deltaUlt = curTile.ult - prevTile.ult;
            const float deltaLrs = curTile.lrs - prevTile.lrs;
            const float deltaLrt = curTile.lrt - prevTile.lrt;
            const bool tileScrolled = (deltaUls != 0.0f) || (deltaUlt != 0.0f) || (deltaLrs != 0.0f) || (deltaLrt != 0.0f);
            const int integerUls = std::lround(curTile.uls);
            const int integerUlt = std::lround(curTile.ult);
            const int integerLrs = std::lround(curTile.lrs);
            const int integerLrt = std::lround(curTile.lrt);
            const int expectedUls = std::lround(prevTile.uls + curTileMap.deltaUls);
            const int expectedUlt = std::lround(prevTile.ult + curTileMap.deltaUlt);
            const int expectedLrs = std::lround(prevTile.lrs + curTileMap.deltaLrs);
            const int expectedLrt = std::lround(prevTile.lrt + curTileMap.deltaLrt);
            const bool wrappedUls = (curTile.cms == G_TX_WRAP) && (modulo(integerUls, curTile.masks * 4) == modulo(expectedUls, curTile.masks * 4));
            const bool wrappedUlt = (curTile.cmt == G_TX_WRAP) && (modulo(integerUlt, curTile.maskt * 4) == modulo(expectedUlt, curTile.maskt * 4));
            const bool wrappedLrs = (curTile.cms == G_TX_WRAP) && (modulo(integerLrs, curTile.masks * 4) == modulo(expectedLrs, curTile.masks * 4));
            const bool wrappedLrt = (curTile.cmt == G_TX_WRAP) && (modulo(integerLrt, curTile.maskt * 4) == modulo(expectedLrt, curTile.maskt * 4));
            curTileMap.prevUls = curTile.uls - curTileMap.deltaUls;
            curTileMap.prevUlt = curTile.ult - curTileMap.deltaUlt;
            curTileMap.prevLrs = curTile.lrs - curTileMap.deltaLrs;
            curTileMap.prevLrt = curTile.lrt - curTileMap.deltaLrt;
            curTileMap.deltaUls = wrappedUls || (abs(deltaUls) >= curTile.masks * 2) ? curTileMap.deltaUls : deltaUls;
            curTileMap.deltaUlt = wrappedUlt || (abs(deltaUlt) >= curTile.maskt * 2) ? curTileMap.deltaUlt : deltaUlt;
            curTileMap.deltaLrs = wrappedLrs || (abs(deltaLrs) >= curTile.masks * 2) ? curTileMap.deltaLrs : deltaLrs;
            curTileMap.deltaLrt = wrappedLrt || (abs(deltaLrt) >= curTile.maskt * 2) ? curTileMap.deltaLrt : deltaLrt;
            curTileMap.mapped = true;
            firstCurWorkloadMap.prevTilesMapped[indices.second] = true;
            tileInterpolationUsed = tileInterpolationUsed || tileScrolled;
        }

        // Check for look at matches.
        for (const IndexPair &indices : lookAtCheckSet) {
            if (firstCurWorkloadMap.lookAt[indices.first].mapped) {
                continue;
            }

            if (firstCurWorkloadMap.prevLookAtMapped[indices.second]) {
                continue;
            }

            GameFrameMap::LookAtMap &curLookAtMap = firstCurWorkloadMap.lookAt[indices.first];
            if (firstPrevWorkloadMap != nullptr) {
                const GameFrameMap::LookAtMap &prevLookAtMap = firstPrevWorkloadMap->lookAt[indices.second];
                curLookAtMap = prevLookAtMap;
            }

            const interop::RSPLookAt &curLookAt = firstCurWorkload.drawData.rspLookAt[indices.first];
            const interop::RSPLookAt &prevLookAt = firstPrevWorkload.drawData.rspLookAt[indices.second];
            bool lookAtMoved = true;
            curLookAtMap.mapped = true;
            curLookAtMap.deltaX = hlslpp::float3(curLookAt.x) - hlslpp::float3(prevLookAt.x);
            curLookAtMap.deltaY = hlslpp::float3(curLookAt.y) - hlslpp::float3(prevLookAt.y);
            firstCurWorkloadMap.prevLookAtMapped[indices.second] = true;
            lookAtInterpolationUsed = lookAtInterpolationUsed || lookAtMoved;
        }
    }

    void GameFrame::matchTransform(Workload &curWorkload, const Workload &prevWorkload, GameFrameMap::WorkloadMap &curWorkloadMap, const GameFrameMap::WorkloadMap *prevWorkloadMap, uint32_t curTransformIndex, uint32_t prevTransformIndex, ModifiedBuffers &modifiedBuffers, bool computeVelocities) {
        GameFrameMap::TransformMap &curTransformMap = curWorkloadMap.transforms[curTransformIndex];
        if (prevWorkloadMap != nullptr) {
            curTransformMap.rigidBody = prevWorkloadMap->transforms[prevTransformIndex].rigidBody;
        }

        const hlslpp::float4x4 &curTransform = curWorkload.drawData.worldTransforms[curTransformIndex];
        const hlslpp::float4x4 &rawPrevTransform = prevWorkload.drawData.worldTransforms[prevTransformIndex];

        // Object matrices carry positions in the world's own frame, so moving
        // one into this frame's origin is just adding the delta to it. Taken
        // only when it lands nearer than leaving it alone, which is what keeps a
        // wrong delta from doing damage.
        hlslpp::float4x4 rebasedPrevTransform;
        const hlslpp::float4x4 *effectivePrev = &rawPrevTransform;
        if (snapRebaseUsable()) {
            rebasedPrevTransform = rawPrevTransform;
            rebasedPrevTransform[3].xyz = rebasedPrevTransform[3].xyz + snapOriginDelta;
            if (float(hlslpp::length(curTransform[3].xyz - rebasedPrevTransform[3].xyz)) <
                float(hlslpp::length(curTransform[3].xyz - rawPrevTransform[3].xyz))) {
                effectivePrev = &rebasedPrevTransform;
                curTransformMap.snapRebasedPrev = true;
            }
        }

        const hlslpp::float4x4 &prevTransform = *effectivePrev;
        const uint32_t curGroupIndex = curWorkload.drawData.worldTransformGroups[curTransformIndex];
        const TransformGroup &curGroup = curWorkload.drawData.transformGroups[curGroupIndex];
        curTransformMap.rigidBody.updateLinear(prevTransform, curTransform, curGroup.positionInterpolation);
        curTransformMap.rigidBody.updateAngular(prevTransform, curTransform, curGroup.rotationInterpolation, curGroup.scaleInterpolation, curGroup.skewInterpolation);
        curTransformMap.rigidBody.updatePerspective(prevTransform, curTransform, curGroup.perspectiveInterpolation);
        curTransformMap.rigidBody.updateDecomposition(prevTransform, curTransform, curGroup.decompose);
        curTransformMap.prevTransformIndex = prevTransformIndex;
        curTransformMap.mapped = true;
        curWorkloadMap.prevTransformsMapped[prevTransformIndex] = true;

        // Pokemon Snap port: per-vertex velocities are skipped entirely. The
        // game rebuilds its vertex heap every frame, so equal-count vertex
        // ranges can pair up misaligned geometry and fling vertices across
        // the screen on interpolated frames (black needle spikes).
        if (!computeVelocities) {
            return;
        }

        uint64_t curVertexHash = 0;
        uint64_t prevVertexHash = 0;
        uint32_t curVertexIndex = curWorkload.drawData.worldTransformVertexIndices[curTransformIndex];
        uint32_t curVertexCount = curWorkload.drawData.worldTransformVertexCount(curTransformIndex);
        uint32_t prevVertexIndex = prevWorkload.drawData.worldTransformVertexIndices[prevTransformIndex];
        uint32_t prevVertexCount = prevWorkload.drawData.worldTransformVertexCount(prevTransformIndex);
        if (((curGroup.vertexInterpolation != G_EX_COMPONENT_SKIP) || (curGroup.texcoordInterpolation == G_EX_COMPONENT_AUTO)) && (curVertexCount == prevVertexCount)) {
            const std::vector<float> &curPosFloats = curWorkload.drawData.posFloats;
            const std::vector<float> &prevPosFloats = prevWorkload.drawData.posFloats;
            std::vector<float> &curVelFloats = curWorkload.drawData.velFloats;
            curVertexHash = XXH3_64bits(&curPosFloats[curVertexIndex * 3], curVertexCount * 3 * sizeof(float));
            prevVertexHash = XXH3_64bits(&prevPosFloats[prevVertexIndex * 3], prevVertexCount * 3 * sizeof(float));

            if ((curGroup.vertexInterpolation != G_EX_COMPONENT_SKIP) && (curVertexHash != prevVertexHash)) {
                const float *curPosFloatsRef = &curPosFloats[curVertexIndex * 3];
                const float *prevPosFloatsRef = &prevPosFloats[prevVertexIndex * 3];
                float *curVelFloatsRef = &curVelFloats[curVertexIndex * 3];
                for (uint32_t i = 0; i < curVertexCount; i++) {
                    for (uint32_t j = 0; j < 3; j++) {
                        curVelFloatsRef[i * 3 + j] = (curPosFloatsRef[i * 3 + j] - prevPosFloatsRef[i * 3 + j]);
                    }
                }

                modifiedBuffers.positionVelocity = true;
            }
        }

        if ((curGroup.texcoordInterpolation != G_EX_COMPONENT_SKIP) && (curVertexCount == prevVertexCount)) {
            const std::vector<float> &curTcFloats = curWorkload.drawData.tcFloats;
            const std::vector<float> &prevTcFloats = prevWorkload.drawData.tcFloats;
            std::vector<float> &curVelFloats = curWorkload.drawData.tcVelFloats;
            const hlslpp::float2 WrappingModulo = curWorkload.extended.texcoordWrapPoint;
            const hlslpp::float2 WrappingModuloHalf = WrappingModulo / 2.0f;
            uint64_t curTcHash = XXH3_64bits(&curTcFloats[curVertexIndex * 2], curVertexCount * 2 * sizeof(float));
            uint64_t prevTcHash = XXH3_64bits(&prevTcFloats[prevVertexIndex * 2], prevVertexCount * 2 * sizeof(float));
            if (((curGroup.texcoordInterpolation != G_EX_COMPONENT_AUTO) || (curVertexHash == prevVertexHash)) && (curTcHash != prevTcHash)) {
                const float *curTcFloatsRef = &curTcFloats[curVertexIndex * 2];
                const float *prevTcFloatsRef = &prevTcFloats[prevVertexIndex * 2];
                float *curVelFloatsRef = &curVelFloats[curVertexIndex * 2];
                for (uint32_t i = 0; i < curVertexCount; i++) {
                    curVelFloatsRef[i * 2 + 0] = (curTcFloatsRef[i * 2 + 0] - prevTcFloatsRef[i * 2 + 0]);
                    curVelFloatsRef[i * 2 + 1] = (curTcFloatsRef[i * 2 + 1] - prevTcFloatsRef[i * 2 + 1]);

                    if (fabsf(curVelFloatsRef[i * 2]) > WrappingModuloHalf[0]) {
                        curVelFloatsRef[i * 2] -= lround(curVelFloatsRef[i * 2] / WrappingModulo[0]) * WrappingModulo[0];
                    }

                    if (fabsf(curVelFloatsRef[i * 2 + 1]) > WrappingModuloHalf[1]) {
                        curVelFloatsRef[i * 2 + 1] -= lround(curVelFloatsRef[i * 2 + 1] / WrappingModulo[1]) * WrappingModulo[1];
                    }
                }
            }

            modifiedBuffers.texcoordVelocity = true;
        }
    }
    
    void GameFrame::buildCallHashMap(uint32_t sceneProjIndex, const Workload &workload, const Projection &proj, std::multimap<uint64_t, GameCallMap> &hashMap) const {
        for (uint32_t c = 0; c < proj.gameCallCount; c++) {
            const GameCall &call = proj.gameCalls[c];
            uint32_t matrixIdHash = 0;
            bool doTransformMatching = false;
            bool doTileInterpolation = false;
            bool doTileMatching = false;
            for (uint32_t m = call.callDesc.minWorldMatrix; m <= call.callDesc.maxWorldMatrix; m++) {
                const uint32_t groupIndex = workload.drawData.worldTransformGroups[m];
                const TransformGroup &group = workload.drawData.transformGroups[groupIndex];
                matrixIdHash = matrixIdHash * 33 ^ group.matrixId;

                const bool usesIdWithAutoOrdering = (group.matrixId != G_EX_ID_AUTO) && (group.matrixId != G_EX_ID_IGNORE) && (group.ordering == G_EX_ORDER_AUTO);
                doTransformMatching = doTransformMatching || (group.matrixId == G_EX_ID_AUTO) || usesIdWithAutoOrdering;
                doTileInterpolation = doTileInterpolation || (group.tileInterpolation != G_EX_COMPONENT_SKIP);
                doTileMatching = doTileMatching || (group.tileInterpolation == G_EX_COMPONENT_AUTO);
            }

            hashMap.emplace(hashFromCall(call, matrixIdHash), GameCallMap{ sceneProjIndex, c, doTransformMatching, doTileInterpolation, doTileMatching });
        }
    }

    void GameFrame::buildTransformIdMap(const Workload &workload, std::multimap<uint32_t, uint32_t> &idMap, std::vector<uint32_t> &ignoredIdVector) const {
        idMap.clear();
        ignoredIdVector.clear();

        uint32_t transformCount = uint32_t(workload.drawData.worldTransformGroups.size());
        for (uint32_t i = 0; i < transformCount; i++) {
            const uint32_t groupIndex = workload.drawData.worldTransformGroups[i];
            const TransformGroup &group = workload.drawData.transformGroups[groupIndex];
            if (group.matrixId == G_EX_ID_AUTO) {
                continue;
            }
            else if (group.matrixId == G_EX_ID_IGNORE) {
                ignoredIdVector.emplace_back(i);
            }
            else if (group.ordering == G_EX_ORDER_LINEAR) {
                idMap.emplace(group.matrixId, i);
            }
        }
    }

    uint64_t GameFrame::hashFromCall(const GameCall &call, uint32_t matrixIdHash) const {
        struct CallMatchKey {
            interop::ColorCombiner colorCombiner;
            interop::OtherMode otherMode;
            uint32_t geometryMode;
            uint32_t triangleCount;
            uint32_t matrixIdHash;
        };

        CallMatchKey key;
        key.colorCombiner = call.callDesc.colorCombiner;
        key.otherMode = call.callDesc.otherMode;
        key.geometryMode = call.callDesc.geometryMode;
        key.triangleCount = call.callDesc.triangleCount;
        key.matrixIdHash = matrixIdHash;
        return XXH3_64bits(&key, sizeof(CallMatchKey));
    }

    bool GameFrame::isDebuggerCameraEnabled(const WorkloadQueue &workloadQueue) {
        for (uint32_t w : workloads) {
            if (workloadQueue.workloads[w].debuggerCamera.enabled) {
                return true;
            }
        }

        return false;
    }

    /*
    void resetTransformMap(GameTransformMap &transformMap, const GameFrame &gameFrame) {
        transformMap.fbPairs.resize(gameFrame.fbPairCount);
        for (uint32_t f = 0; f < gameFrame.fbPairCount; f++) {
            const auto &gameFbPair = gameFrame.fbPairs[f];
            auto &mapFbPair = transformMap.fbPairs[f];
            mapFbPair.projections.resize(gameFbPair.projectionCount, { });
            for (uint32_t p = 0; p < gameFbPair.projectionCount; p++) {
                mapFbPair.projections[p].drawCalls.clear();
                mapFbPair.projections[p].drawCalls.resize(gameFbPair.projections[p].drawCallCount, { });
                mapFbPair.projections[p].transformCallMap.clear();
            }

            mapFbPair.prevFbPairIndex = 0;
            mapFbPair.mapped = false;
        }

        transformMap.transforms.clear();
        transformMap.transforms.resize(gameFrame.drawData.worldTransforms.size(), { });
    }

    void makeTransformCallMap(GameProjection &gameProj, std::multimap<uint32_t, uint32_t> &callMap) {
        for (uint32_t d = 0; d < gameProj.drawCallCount; d++) {
            const auto &curCall = gameProj.drawCalls[d];
            const uint32_t startIndex = std::max(curCall.callDesc.minWorldMatrix, static_cast<uint16_t>(1));
            for (uint32_t t = startIndex; t <= curCall.callDesc.maxWorldMatrix; t++) {
                callMap.emplace(t, d);
            }
        }
    }

    void GameFrame::matchPreviousFrame(GameFrame &prevFrame, RenderWorker *worker, BufferUploader *bufferUploader) {
        assert(worker != nullptr);
        assert(bufferUploader != nullptr);
        transformMap.submissionFrame = prevFrame.submissionFrame;
        resetTransformMap(transformMap, *this);
        transformMap.mapped = true;

        static std::vector<bool> prevTransformMapped;
        static std::vector<TransformMatchCandidate> matchCandidates;
        bool uploadVelBuffer = false;
        const auto &prevDrawData = prevFrame.drawData;
        prevTransformMapped.clear();
        prevTransformMapped.resize(prevDrawData.worldTransforms.size(), false);
        for (uint32_t f = 0; f < fbPairCount; f++) {
            if (f >= prevFrame.fbPairCount) {
                continue;
            }

            // Check if the framebuffer pairs are actually compatible.
            auto &curFbPair = fbPairs[f];
            auto &prevFbPair = prevFrame.fbPairs[f];
            if ((curFbPair.colorImage.width != prevFbPair.colorImage.width) || (curFbPair.colorImage.fmt != prevFbPair.colorImage.fmt) || (curFbPair.colorImage.siz != prevFbPair.colorImage.siz)) {
                continue;
            }

            auto &fbPairMap = transformMap.fbPairs[f];
            fbPairMap.mapped = true;
            fbPairMap.prevFbPairIndex = f;

            for (uint32_t p = 0; p < curFbPair.projectionCount; p++) {
                if (p >= prevFbPair.projectionCount) {
                    continue;
                }

                // Check if the projections are actually compatible.
                auto &curProj = curFbPair.projections[p];
                auto &prevProj = prevFbPair.projections[p];
                if (curProj.type != prevProj.type) {
                    continue;
                }

                // TODO: Support more than just perspective projections.
                if (curProj.type != GameProjection::Type::Perspective) {
                    continue;
                }

                auto &projMap = fbPairMap.projections[p];
                auto &callMap = projMap.transformCallMap;
                projMap.mapped = true;
                projMap.prevProjectionIndex = p;
                makeTransformCallMap(curProj, callMap);

                // Make a map for the previous frame if it's empty.
                auto &prevFbMap = prevFrame.transformMap.fbPairs[fbPairMap.prevFbPairIndex];
                auto &prevCallMap = prevFbMap.projections[projMap.prevProjectionIndex].transformCallMap;
                if (prevCallMap.empty()) {
                    makeTransformCallMap(prevProj, prevCallMap);
                }

                // Find all candidates for all transforms.
                // TODO: This double loop explodes in complexity. Consider building a lookup table based on the
                // compatibility of the calls to reduce the iteration time drastically. These could be combiner
                // and other mode flags.
                matchCandidates.clear();
                const hlslpp::float4x4 &curViewProj = drawData.viewProjTransforms[curProj.transformsIndex];
                const hlslpp::float4x4 &prevViewProj = drawData.viewProjTransforms[prevProj.transformsIndex];
                for (auto curIt = callMap.begin(); curIt != callMap.end();) {
                    const uint32_t curTransform = curIt->first;
                    if (!transformMap.transforms[curTransform].mapped) {
                        const auto &curMatrix = drawData.worldTransforms[curTransform];
                        for (auto prevIt = prevCallMap.begin(); prevIt != prevCallMap.end();) {
                            const uint32_t prevTransform = prevIt->first;
                            if (!prevTransformMapped[prevTransform]) {
                                const auto prevMatrix = prevDrawData.worldTransforms[prevTransform];
                                if (isCallCompatible(curProj.drawCalls[curIt->second], drawData, prevProj.drawCalls[prevIt->second], prevDrawData)) {
                                    const TransformMatchResult matchResult = computeTransformMatch(curMatrix, curViewProj, prevMatrix, prevViewProj, prevFrame.transformMap.transforms[prevTransform].rigidBody);
                                    if (matchResult.valid) {
                                        matchCandidates.push_back({ curTransform, prevTransform, matchResult.computeDifference() });
                                    }
                                }
                            }

                            do { ++prevIt; } while (prevIt != prevCallMap.end() && (prevTransform == prevIt->first));
                        }
                    }

                    do { ++curIt; } while (curIt != callMap.end() && (curTransform == curIt->first));
                }

                // Sort all the candidates and assign them.
                std::stable_sort(matchCandidates.begin(), matchCandidates.end());
                for (const TransformMatchCandidate candidate : matchCandidates) {
                    if (transformMap.transforms[candidate.curTransformIndex].mapped) {
                        continue;
                    }

                    if (prevTransformMapped[candidate.prevTransformIndex]) {
                        continue;
                    }

                    auto &curTransformMap = transformMap.transforms[candidate.curTransformIndex];
                    const hlslpp::float4x4 &prevMatrix = prevDrawData.worldTransforms[candidate.prevTransformIndex];
                    const hlslpp::float4x4 &curMatrix = drawData.worldTransforms[candidate.curTransformIndex];
                    curTransformMap.rigidBody = prevFrame.transformMap.transforms[candidate.prevTransformIndex].rigidBody;
                    curTransformMap.rigidBody.updateLinear(prevMatrix, curMatrix);
                    curTransformMap.rigidBody.updateAngular(prevMatrix, curMatrix);
                    curTransformMap.rigidBody.updateDecomposition(curMatrix);
                    curTransformMap.prevTransformIndex = candidate.prevTransformIndex;
                    curTransformMap.mapped = true;
                    prevTransformMapped[candidate.prevTransformIndex] = true;

                    const auto prevRange = prevCallMap.equal_range(candidate.prevTransformIndex);
                    const auto curRange = callMap.equal_range(candidate.curTransformIndex);
                    auto prevRangeIt = prevRange.first;
                    auto curRangeIt = curRange.first;
                    while ((prevRangeIt != prevRange.second) && (curRangeIt != curRange.second)) {
                        // Check if any material defined a custom match callback for this call.
                        GameDrawCall *curCall = &curProj.drawCalls[curRangeIt->second];
                        GameDrawCall *prevCall = &prevProj.drawCalls[prevRangeIt->second];
                        if (curCall->lerpDesc.matchCallback != nullptr) {
                            curCall->lerpDesc.matchCallback(this, curCall, &prevFrame, prevCall);
                        }

                        // Check if the mesh positions are different and compute the velocity buffer if required.
                        // TODO: Compute the velocity buffer and compute the hash some way.
                        const bool meshHashDifference = false;
                        uploadVelBuffer = uploadVelBuffer || meshHashDifference;

                        // Check if the texture hashes are different to mark it as an animated texture material.
                        bool tmemHashDifference = false;
                        if (curCall->callDesc.tileCount == prevCall->callDesc.tileCount) {
                            const uint32_t curTileBase = curCall->callDesc.tileIndex;
                            const uint32_t prevTileBase = prevCall->callDesc.tileIndex;
                            for (uint32_t t = 0; t < curCall->callDesc.tileCount && !tmemHashDifference; t++) {
                                const auto &curTile = drawData.callTiles[curTileBase + t];
                                const auto &prevTile = prevDrawData.callTiles[prevTileBase + t];
                                tmemHashDifference = (curTile.tmemHashOrID != prevTile.tmemHashOrID);
                            }
                        }

                        // TODO: Reimplement.
                        //curCall->materialDesc.lockMask = tmemHashDifference ? 1.0f : 0.0f;

                        auto &drawCall = projMap.drawCalls[curRangeIt->second];
                        drawCall.prevCallIndex = prevRangeIt->second;
                        drawCall.mapped = true;
                        prevRangeIt++;
                        curRangeIt++;
                    }
                }
            }
        }
    }
    */
};