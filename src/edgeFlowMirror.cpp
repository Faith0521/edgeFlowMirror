///=====================================================================
/// edgeFlowMirror 3.4 C++ Port
/// Original (c) 2007 - 2016 by Thomas Bittner
///
/// This tool mirrors mainly skinweights, geometry and component selection.
/// Opposite Mirror-points are found per edgeflow, and NOT via position.
/// This means, that only the way the edges are connected together is relevant,
/// and the actual positions of the vertices does not matter.
/// one of the inputs the tool requires, is one of the middleedges
/// The character can even be in a different pose and most parts of the tool will
/// still work.
///=====================================================================

#include "edgeFlowMirror.h"

#include <maya/MFnPlugin.h>
#include <maya/MFnMesh.h>
#include <maya/MFnSingleIndexedComponent.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnDagNode.h>
#include <maya/MItSelectionList.h>
#include <maya/MItMeshVertex.h>
#include <maya/MItMeshEdge.h>
#include <maya/MItMeshPolygon.h>
#include <maya/MItDependencyNodes.h>
#include <maya/MArgDatabase.h>
#include <maya/MGlobal.h>
#include <maya/MSelectionList.h>
#include <maya/MFnSkinCluster.h>
#include <maya/MPoint.h>
#include <maya/MFloatVector.h>
#include <maya/MTransformationMatrix.h>
#include <maya/MDagPathArray.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>

// ------------------------------------------------------------
// Constants
// ------------------------------------------------------------
static const char *const kPluginCmdName = "edgeFlowMirror";

static const char *const kTaskFlag = "-t";
static const char *const kTaskFlagLong = "-task";
static const char *const kDirectionFlag = "-d";
static const char *const kDirectionFlagLong = "-direction";
static const char *const kMiddleEdgeFlag = "-me";
static const char *const kMiddleEdgeFlagLong = "-middleEdge";
static const char *const kBaseObjectFlag = "-bo";
static const char *const kBaseObjectFlagLong = "-baseObject";
static const char *const kBaseVertexSpaceFlag = "-bv";
static const char *const kBaseVertexSpaceFlagLong = "-baseVertexSpace";
static const char *const kLeftJointsPrefixFlag = "-ljp";
static const char *const kLeftJointsPrefixFlagLong = "-leftJointsPrefix";
static const char *const kRightJointsPrefixFlag = "-rjp";
static const char *const kRightJointsPrefixFlagLong = "-rightJointsPrefix";
static const char *const kOptimizeFlag = "-o";
static const char *const kOptimizeFlagLong = "-optimize";

static const double kEpsilon = 1e-5;

// Global cache for optimized re-computation
MIntArray EdgeFlowMirrorCommand::sSavedMapArray;
MIntArray EdgeFlowMirrorCommand::sSavedSideArray;
MString EdgeFlowMirrorCommand::sSavedBaseObjectName;

// ------------------------------------------------------------
// Constructor / Destructor
// ------------------------------------------------------------
EdgeFlowMirrorCommand::EdgeFlowMirrorCommand()
    : mInfCount(0)
{
}

EdgeFlowMirrorCommand::~EdgeFlowMirrorCommand() = default;

// ------------------------------------------------------------
// Creator / Syntax
// ------------------------------------------------------------
void *EdgeFlowMirrorCommand::creator()
{
    return new EdgeFlowMirrorCommand();
}

MSyntax EdgeFlowMirrorCommand::newSyntax()
{
    MSyntax syntax;
    syntax.addFlag(kTaskFlag, kTaskFlagLong, MSyntax::kString);
    syntax.addFlag(kDirectionFlag, kDirectionFlagLong, MSyntax::kDouble);
    syntax.addFlag(kMiddleEdgeFlag, kMiddleEdgeFlagLong, MSyntax::kString);
    syntax.addFlag(kBaseObjectFlag, kBaseObjectFlagLong, MSyntax::kString);
    syntax.addFlag(kBaseVertexSpaceFlag, kBaseVertexSpaceFlagLong, MSyntax::kBoolean);
    syntax.addFlag(kLeftJointsPrefixFlag, kLeftJointsPrefixFlagLong, MSyntax::kString);
    syntax.addFlag(kRightJointsPrefixFlag, kRightJointsPrefixFlagLong, MSyntax::kString);
    syntax.addFlag(kOptimizeFlag, kOptimizeFlagLong, MSyntax::kBoolean);
    return syntax;
}

// ------------------------------------------------------------
// Helper: int array -> std::vector<int>
// ------------------------------------------------------------
static std::vector<int> intArrayToVector(const MIntArray &arr)
{
    std::vector<int> v;
    v.reserve(arr.length());
    for (unsigned i = 0; i < arr.length(); ++i)
        v.push_back(arr[i]);
    return v;
}

// ------------------------------------------------------------
// Helper: decompose namespace + short name
// ------------------------------------------------------------
static MString stripNamespace(const MString &full, MString &outNs)
{
    int colonPos = full.rindexW(':');
    if (colonPos < 0)
    {
        outNs = MString();
        return full;
    }
    outNs = full.substring(0, colonPos) + ":";
    return full.substringW(colonPos + 1, full.length() - 1);
}

// ------------------------------------------------------------
// doIt - main entry point
// ------------------------------------------------------------
MStatus EdgeFlowMirrorCommand::doIt(const MArgList &args)
{
    int direction = 1;
    MString searchString = "L_ Left left";
    MString replaceString = "R_ Right right";
    MString middleEdge;
    mTask.clear();
    MString baseObjectName;
    bool doVertexSpace = false;
    bool doOptimize = false;

    // ---- parse arguments ----
    MArgDatabase argData(newSyntax(), args);

    if (argData.isFlagSet(kTaskFlag))
        mTask = argData.flagArgumentString(kTaskFlag, 0);

    if (argData.isFlagSet(kDirectionFlag))
        direction = static_cast<int>(argData.flagArgumentDouble(kDirectionFlag, 0));

    if (argData.isFlagSet(kMiddleEdgeFlag))
        middleEdge = argData.flagArgumentString(kMiddleEdgeFlag, 0);

    if (argData.isFlagSet(kBaseObjectFlag))
        baseObjectName = argData.flagArgumentString(kBaseObjectFlag, 0);

    if (argData.isFlagSet(kBaseVertexSpaceFlag))
        doVertexSpace = argData.flagArgumentBool(kBaseVertexSpaceFlag, 0);

    if (argData.isFlagSet(kLeftJointsPrefixFlag))
        searchString = argData.flagArgumentString(kLeftJointsPrefixFlag, 0);

    if (argData.isFlagSet(kRightJointsPrefixFlag))
        replaceString = argData.flagArgumentString(kRightJointsPrefixFlag, 0);

    if (argData.isFlagSet(kOptimizeFlag))
        doOptimize = argData.flagArgumentBool(kOptimizeFlag, 0);

    // ---- get selection ----
    MSelectionList selection;
    MGlobal::getActiveSelectionList(selection);
    if (selection.isEmpty())
    {
        MGlobal::displayError("Please select some vertices.");
        return MS::kFailure;
    }

    MDagPath dagPathSelShape;
    MObject component;
    MItSelectionList selIter(selection);
    selIter.getDagPath(dagPathSelShape, component);
    dagPathSelShape.extendToShape();

    MItMeshVertex selectedPoints(dagPathSelShape, component);
    MFnMesh fnMesh(dagPathSelShape);
    int pointCount = fnMesh.numVertices();
    MString objectName = fnMesh.partialPathName();

    if (pointCount == 0)
    {
        MGlobal::displayError("point count is 0");
        return MS::kFailure;
    }

    if (middleEdge.length() == 0)
    {
        MGlobal::displayError("Please specify a middle edge.");
        return MS::kFailure;
    }

    // ---- topology analysis (with optional cache) ----
    MIntArray mapArray;
    MIntArray sideArray;
    MString newBaseObjectName;

    if (doOptimize && sSavedMapArray.length() > 0 && mTask != MString("compute"))
    {
        mapArray = sSavedMapArray;
        sideArray = sSavedSideArray;
        newBaseObjectName = sSavedBaseObjectName;
    }
    else
    {
        MStatus stat = analyzeTopology(middleEdge, mapArray, sideArray, newBaseObjectName);
        if (stat != MS::kSuccess)
            return stat;
        sSavedMapArray = mapArray;
        sSavedSideArray = sideArray;
        sSavedBaseObjectName = newBaseObjectName;
    }

    // ---- early returns for query tasks ----
    if (mTask == "getMapArray")
    {
        setResult(mapArray);
        return MS::kSuccess;
    }

    if (mTask == "getSideArray")
    {
        setResult(sideArray);
        return MS::kSuccess;
    }

    if (mTask == "getMapSideArray")
    {
        MIntArray combined;
        combined.setLength(mapArray.length() + sideArray.length());
        unsigned idx = 0;
        for (unsigned i = 0; i < mapArray.length(); ++i, ++idx)
            combined[idx] = mapArray[i];
        for (unsigned i = 0; i < sideArray.length(); ++i, ++idx)
            combined[idx] = sideArray[i];
        setResult(combined);
        return MS::kSuccess;
    }

    if (mTask == "compute")
        return MS::kSuccess;

    if (mTask == "geometrySymmetry" && newBaseObjectName.length() > 0)
        baseObjectName = newBaseObjectName;

    // ---- build bothIndexes and allPointsArray ----
    MIntArray bothIndexes;
    MIntArray allPointsArray;
    allPointsArray.setLength(pointCount);
    for (int i = 0; i < pointCount; ++i)
        allPointsArray[i] = -1;

    int counter = 0;
    selectedPoints.reset();
    for (; !selectedPoints.isDone(); selectedPoints.next())
    {
        int idx = selectedPoints.index();
        if (mapArray[idx] != -1)
            bothIndexes.append(idx);

        allPointsArray[idx] = counter;
        ++counter;

        if (sideArray[idx] != 0 && sideArray[idx] != -1)
        {
            bothIndexes.append(mapArray[idx]);
            allPointsArray[mapArray[idx]] = counter;
            ++counter;
        }
    }

    // reorder bothIndexes (remove duplicates, keep unique ordered)
    {
        MIntArray ordered;
        for (int i = 0; i < pointCount; ++i)
        {
            if (allPointsArray[i] != -1)
                ordered.append(i);
        }
        bothIndexes = ordered;
    }

    // rebuild allPointsArray
    allPointsArray.setLength(pointCount);
    for (int i = 0; i < pointCount; ++i)
        allPointsArray[i] = -1;
    for (unsigned i = 0; i < bothIndexes.length(); ++i)
        allPointsArray[bothIndexes[i]] = (int)i;

    // ---- vertex component ----
    MFnSingleIndexedComponent fnVtxComp;
    mVtxComponents = fnVtxComp.create(MFn::kMeshVertComponent);
    for (unsigned i = 0; i < bothIndexes.length(); ++i)
        fnVtxComp.addElement(bothIndexes[i]);

    // ================================================================
    // TASK: skinCluster
    // ================================================================
    if (mTask == "skinCluster")
    {
        bool foundSkin = false;
        MDagPath skinPath;

        MItDependencyNodes depIter(MFn::kSkinClusterFilter);
        for (; !depIter.isDone(); depIter.next())
        {
            MFnSkinCluster fnSkinCluster(depIter.item());
            fnSkinCluster.getPathAtIndex(0, skinPath);
            if (MFnDagNode(skinPath.node()).partialPathName() == objectName)
            {
                mSkinCluster = depIter.item();
                foundSkin = true;
                break;
            }
        }

        if (!foundSkin)
        {
            MGlobal::displayError("No skinCluster found on geometry.");
            return MS::kFailure;
        }

        // ---- collect joints ----
        MFnSkinCluster fnSkinCluster(mSkinCluster);
        MDagPathArray influenceArray;
        fnSkinCluster.influenceObjects(influenceArray);
        unsigned numInfs = influenceArray.length();
        mInfCount = (int)numInfs;

        std::vector<MString> jointNames(numInfs);
        MIntArray jointMapArray;
        for (unsigned i = 0; i < numInfs; ++i)
        {
            jointMapArray.append((int)i);
            jointNames[i] = MFnDagNode(influenceArray[i]).name();
        }

        // ---- build joint mirror map using search/replace ----
        MStringArray searchTokens, replaceTokens;
        searchString.split(' ', searchTokens);
        replaceString.split(' ', replaceTokens);
        unsigned numTokens = std::min(searchTokens.length(), replaceTokens.length());

        for (unsigned i = 0; i < numInfs; ++i)
        {
            if (jointMapArray[i] != (int)i)
                continue;

            MString ns;
            MString shortName = stripNamespace(jointNames[i], ns);

            for (unsigned t = 0; t < numTokens; ++t)
                shortName = shortName.substitute(searchTokens[t], replaceTokens[t]);

            MString fullDest = ns + shortName;

            // find matching joint and swap
            for (unsigned j = i + 1; j < numInfs; ++j)
            {
                if (jointNames[j] == fullDest && jointMapArray[j] == (int)j)
                {
                    jointMapArray[i] = (int)j;
                    jointMapArray[j] = (int)i;
                    break;
                }
            }
        }

        // ---- get weights ----
        fnSkinCluster.getPathAtIndex(fnSkinCluster.indexForOutputConnection(0), skinPath);

        {
            unsigned int tmpInfCount = 0;
            fnSkinCluster.getWeights(skinPath, mVtxComponents, mWeightArray, tmpInfCount);
        }

        mOldWeightArray = mWeightArray; // copy for undo

        // ---- mirror weights ----
        MDoubleArray averageWeights(numInfs, 0.0);

        unsigned numBI = bothIndexes.length();
        for (unsigned i = 0; i < numBI; ++i)
        {
            int idx = bothIndexes[i];
            unsigned startB = i * numInfs;

            // not middle vertex
            if (sideArray[idx] == direction && mapArray[idx] != idx)
            {
                int oppIdx = allPointsArray[mapArray[bothIndexes[i]]];
                unsigned startA = (unsigned)oppIdx * numInfs;

                for (unsigned k = 0; k < numInfs; ++k)
                    mWeightArray[startA + k] = mWeightArray[startB + (unsigned)jointMapArray[k]];
            }
            // middle vertex
            else if (mapArray[idx] == idx)
            {
                for (unsigned k = 0; k < numInfs; ++k)
                    averageWeights[k] = (mWeightArray[startB + k] + mWeightArray[startB + (unsigned)jointMapArray[k]]) * 0.5;
                for (unsigned k = 0; k < numInfs; ++k)
                    mWeightArray[startB + k] = averageWeights[k];
            }
        }

        MGlobal::displayInfo("[edgeFlowMirror] skinCluster mirroring complete.");
    }

    // ================================================================
    // TASKS: geometryFlip / geometryMirror / geometrySymmetry
    // ================================================================
    if (mTask == "geometryFlip" || mTask == "geometryMirror" || mTask == "geometrySymmetry")
    {
        int mirrorInt = 0;
        if (mTask == "geometryMirror")
            mirrorInt = 1;
        if (mTask == "geometrySymmetry")
            mirrorInt = 2;

        // ---- get base object ----
        MSelectionList baseSel;
        baseSel.add(baseObjectName);
        MDagPath baseObj;
        baseSel.getDagPath(0, baseObj, component);
        baseObj.extendToShape();

        int vertexCount = MFnMesh(baseObj).numVertices();
        if (vertexCount == 0)
        {
            MGlobal::displayError("No vertices found.");
            return MS::kFailure;
        }

        mTargetObj = dagPathSelShape;
        MFnMesh fnBaseMesh(baseObj);
        MPointArray basePoints;
        fnBaseMesh.getPoints(basePoints);

        mOldTargetPoints.clear();
        mNewTargetPoints.clear();

        MFnMesh fnTargetMesh(mTargetObj);
        MPointArray targetPoints;
        fnTargetMesh.getPoints(targetPoints, MSpace::kObject);

        unsigned nTarget = targetPoints.length();
        mOldTargetPoints.setLength(nTarget);
        mNewTargetPoints.setLength(nTarget);
        for (unsigned i = 0; i < nTarget; ++i)
        {
            mOldTargetPoints[i] = MFloatVector((float)targetPoints[i].x,
                                                (float)targetPoints[i].y,
                                                (float)targetPoints[i].z);
            mNewTargetPoints[i] = MFloatVector((float)targetPoints[i].x,
                                                (float)targetPoints[i].y,
                                                (float)targetPoints[i].z);
        }

        // ---------- Flip / Mirror (not symmetry shape) ----------
        if (mirrorInt != 2)
        {
            if (!doVertexSpace)
            {
                // Simple offset-based mirror
                for (unsigned i = 0; i < bothIndexes.length(); ++i)
                {
                    int oppIdx = bothIndexes[i];
                    if (mirrorInt == 0 || direction == sideArray[oppIdx])
                    {
                        int oppMap = mapArray[oppIdx];
                        MVector offset = targetPoints[oppIdx] - basePoints[oppIdx];

                        MPoint newPos(basePoints[oppMap].x - offset.x,
                                      basePoints[oppMap].y + offset.y,
                                      basePoints[oppMap].z + offset.z);
                        mNewTargetPoints[oppMap] = MFloatVector((float)newPos.x,
                                                                 (float)newPos.y,
                                                                 (float)newPos.z);
                    }
                }
            }
            else
            {
                // ---- Vertex-Space Mirror ----
                int totalVerts = fnBaseMesh.numVertices();
                int totalFaces = fnBaseMesh.numPolygons();

        // Build connectivity data
        MIntArray connectedVerts, connectedFaces;
        std::vector<std::vector<int>> conVerts(totalVerts);
        std::vector<std::vector<int>> conFaces(totalVerts);
        std::vector<MVector> baseNormals(totalVerts);
        std::vector<MVector> targetNormals(totalVerts);

        MItMeshVertex baseVertexIter(baseObj);
        MItMeshVertex targetVertexIter(mTargetObj);

        int prevIndex = 0;
        for (int i = 0; i < totalVerts; ++i)
        {
            baseVertexIter.setIndex(i, prevIndex);
            baseVertexIter.getConnectedVertices(connectedVerts);
            baseVertexIter.getConnectedFaces(connectedFaces);
            conVerts[i] = intArrayToVector(connectedVerts);
            conFaces[i] = intArrayToVector(connectedFaces);

            MVector n;
            baseVertexIter.getNormal(n);
            n.normalize();
            baseNormals[i] = n;

            targetVertexIter.getNormal(n);
            n.normalize();
            targetNormals[i] = n;
        }

                // Build face-vertex lists
                MItMeshPolygon faceIter(baseObj);
                std::vector<std::vector<int>> vertsOnFaces(totalFaces);
                {
                    int prevIdx2 = 0;
                    for (int i = 0; i < totalFaces; ++i)
                    {
                        faceIter.setIndex(i, prevIdx2);
                        faceIter.getVertices(connectedVerts);
                        vertsOnFaces[i] = intArrayToVector(connectedVerts);
                    }
                }

                // Data arrays for local frames
                std::vector<std::vector<int>> xIds(totalVerts);
                std::vector<std::vector<int>> zIds(totalVerts);
                std::vector<std::vector<bool>> xNegs(totalVerts);
                std::vector<std::vector<bool>> zNegs(totalVerts);
                std::vector<bool> skips(totalVerts, false);

                // ===== Build frames for left/middle side =====
                for (unsigned i = 0; i < bothIndexes.length(); ++i)
                {
                    int bIdx = bothIndexes[i];
                    int sideIdx = sideArray[bIdx];

                    if (sideIdx == 1)
                        continue; // right side, will be mirrored later

                    // Check if this vertex can be skipped
                    double ptThreshold = (basePoints[bIdx] - targetPoints[bIdx]).length();
                    double mapThreshold = (basePoints[mapArray[bIdx]] - targetPoints[mapArray[bIdx]]).length();

                    if (ptThreshold < kEpsilon && mapThreshold < kEpsilon)
                    {
                        skips[bIdx] = true;
                        continue;
                    }

                    // Average neighbor position
                    double avgX = 0.0, avgY = 0.0, avgZ = 0.0;
                    int nbrCount = (int)conVerts[bIdx].size();
                    for (int j = 0; j < nbrCount; ++j)
                    {
                        int vId = conVerts[bIdx][j];
                        avgX += basePoints[vId].x;
                        avgY += basePoints[vId].y;
                        avgZ += basePoints[vId].z;
                    }
                    if (nbrCount > 0)
                    {
                        avgX /= (double)nbrCount;
                        avgY /= (double)nbrCount;
                        avgZ /= (double)nbrCount;
                    }
                    MPoint avgPos(avgX, avgY, avgZ);

                    // Build per-face X/Z axis candidates
                    MVector firstVecs[2];
                    bool firstVecSet = false;

                    int nFaces = (int)conFaces[bIdx].size();
                    for (int f = 0; f < nFaces; ++f)
                    {
                        int faceId = conFaces[bIdx][f];
                        int twoDots[2] = {-1, -1};
                        MVector twoVecs[2];
                        int found = 0;

                        for (int vert : vertsOnFaces[faceId])
                        {
                            if (vert == bIdx) continue;
                            for (int cv : conVerts[bIdx])
                            {
                                if (vert == cv)
                                {
                                    twoDots[found] = vert;
                                    twoVecs[found] = MVector(basePoints[vert] - avgPos);
                                    ++found;
                                    break;
                                }
                            }
                            if (found >= 2) break;
                        }

                        if (found < 2) continue;

                        // Skip middle-vertex face that connects across middle
                        if (sideIdx == 0 && sideArray[twoDots[0]] != 0 && sideArray[twoDots[1]] != 0)
                            continue;

                        if (f == 0)
                        {
                            firstVecs[0] = twoVecs[0];
                            firstVecs[1] = twoVecs[1];
                            firstVecSet = true;

                            xIds[bIdx].push_back(twoDots[0]);
                            zIds[bIdx].push_back(twoDots[1]);
                            xNegs[bIdx].push_back(false);
                            zNegs[bIdx].push_back(false);
                        }
                        else
                        {
                            // Compare angles with first face
                            double a0 = twoVecs[0].angle(firstVecs[0]);
                            double a1 = twoVecs[1].angle(firstVecs[0]);
                            bool a0neg = (a0 > M_PI * 0.5);
                            bool a1neg = (a1 > M_PI * 0.5);
                            if (a0neg) a0 = M_PI - a0;
                            if (a1neg) a1 = M_PI - a1;

                            if (a0 < a1)
                            {
                                xIds[bIdx].push_back(twoDots[0]);
                                zIds[bIdx].push_back(twoDots[1]);
                                xNegs[bIdx].push_back(a0neg);
                                zNegs[bIdx].push_back(twoVecs[1].angle(firstVecs[1]) > M_PI * 0.5);
                            }
                            else
                            {
                                xIds[bIdx].push_back(twoDots[1]);
                                zIds[bIdx].push_back(twoDots[0]);
                                xNegs[bIdx].push_back(a1neg);
                                zNegs[bIdx].push_back(twoVecs[0].angle(firstVecs[1]) > M_PI * 0.5);
                            }
                        }
                    }
                }

                // ===== Mirror frames to right side =====
                for (unsigned i = 0; i < bothIndexes.length(); ++i)
                {
                    int bIdx = bothIndexes[i];
                    if (sideArray[bIdx] != 1)
                        continue;

                    int mirrorIdx = mapArray[bIdx];
                    if (mirrorIdx < 0 || mirrorIdx >= totalVerts)
                        continue;

                    if (skips[mirrorIdx])
                    {
                        skips[bIdx] = true;
                        continue;
                    }

                    const auto &srcX = xIds[mirrorIdx];
                    const auto &srcZ = zIds[mirrorIdx];
                    xIds[bIdx].resize(srcX.size());
                    zIds[bIdx].resize(srcZ.size());
                    for (unsigned k = 0; k < srcX.size(); ++k)
                    {
                        xIds[bIdx][k] = mapArray[srcX[k]];
                        zIds[bIdx][k] = mapArray[srcZ[k]];
                    }
                    xNegs[bIdx] = xNegs[mirrorIdx];
                    zNegs[bIdx] = zNegs[mirrorIdx];
                }

                // ===== Apply transforms =====
                unsigned numBI = bothIndexes.length();
                for (unsigned i = 0; i < numBI; ++i)
                {
                    int bIdx = bothIndexes[i];
                    if (mapArray[bIdx] == -2)
                        continue;
                    if (skips[bIdx])
                        continue;

                    if (mirrorInt == 0 || sideArray[bIdx] == direction || sideArray[bIdx] == 0)
                    {
                        int idsCount = (int)xIds[bIdx].size();
                        if (idsCount == 0)
                            continue;

                        MVector baseX(0, 0, 0);
                        MVector baseZ(0, 0, 0);

                        for (int k = 0; k < idsCount; ++k)
                        {
                            double xm = xNegs[bIdx][k] ? -1.0 : 1.0;
                            double zm = zNegs[bIdx][k] ? -1.0 : 1.0;
                            baseX += MVector(basePoints[xIds[bIdx][k]] - basePoints[bIdx]) * xm;
                            baseZ += MVector(basePoints[zIds[bIdx][k]] - basePoints[bIdx]) * zm;
                        }
                        baseX /= (double)idsCount;
                        baseZ /= (double)idsCount;
                        baseX.normalize();
                        baseZ.normalize();

                        double bm[16] = {
                            baseX.x, baseX.y, baseX.z, 0,
                            baseNormals[bIdx].x, baseNormals[bIdx].y, baseNormals[bIdx].z, 0,
                            baseZ.x, baseZ.y, baseZ.z, 0,
                            basePoints[bIdx].x, basePoints[bIdx].y, basePoints[bIdx].z, 1
                        };
                        MMatrix baseMat = createMatrixFromList(bm);
                        MMatrix changeWorld = createMatrixFromPos(targetPoints[bIdx]);
                        MMatrix changeLocal = changeWorld * baseMat.inverse();

                        int mappedBIdx = bIdx;
                        int targetMappedIdx = bIdx;

                        if (sideArray[bIdx] == 0)
                        {
                            // ---- middle vertex ----
                            int middleId = -1;
                            for (int nid : xIds[bIdx])
                            {
                                if (sideArray[nid] == 0) { middleId = nid; break; }
                            }
                            if (middleId == -1)
                            {
                                for (int nid : zIds[bIdx])
                                {
                                    if (sideArray[nid] == 0) { middleId = nid; break; }
                                }
                            }

                            if (middleId >= 0)
                            {
                                MVector midVec(basePoints[middleId] - basePoints[bIdx]);
                                double ax = midVec.angle(baseX);
                                double az = midVec.angle(baseZ);
                                if (ax > M_PI * 0.5) ax = M_PI - ax;
                                if (az > M_PI * 0.5) az = M_PI - az;

                                MPoint centerPt;
                                if (ax < az)
                                    centerPt = MPoint(changeLocal(3, 0), changeLocal(3, 1), -changeLocal(3, 2));
                                else
                                    centerPt = MPoint(-changeLocal(3, 0), changeLocal(3, 1), changeLocal(3, 2));

                                MMatrix centeredLocal = createMatrixFromPos(centerPt);
                                MMatrix changeTarget = centeredLocal * baseMat;

                                mNewTargetPoints[bIdx] = MFloatVector((float)changeTarget(3, 0),
                                                                       (float)changeTarget(3, 1),
                                                                       (float)changeTarget(3, 2));
                            }
                        }
                        else
                        {
                            // ---- non-middle vertex ----
                            mappedBIdx = mapArray[bIdx];
                            targetMappedIdx = mappedBIdx;

                            MVector targetX(0, 0, 0);
                            MVector targetZ(0, 0, 0);
                            int midCnt = (int)xIds[mappedBIdx].size();

                            for (int k = 0; k < midCnt; ++k)
                            {
                                double xm = xNegs[mappedBIdx][k] ? -1.0 : 1.0;
                                double zm = zNegs[mappedBIdx][k] ? -1.0 : 1.0;
                                targetX += MVector(basePoints[xIds[mappedBIdx][k]] - basePoints[mappedBIdx]) * xm;
                                targetZ += MVector(basePoints[zIds[mappedBIdx][k]] - basePoints[mappedBIdx]) * zm;
                            }
                            targetX /= (double)midCnt;
                            targetZ /= (double)midCnt;
                            targetX.normalize();
                            targetZ.normalize();

                            double tm[16] = {
                                targetX.x, targetX.y, targetX.z, 0,
                                baseNormals[mappedBIdx].x, baseNormals[mappedBIdx].y, baseNormals[mappedBIdx].z, 0,
                                targetZ.x, targetZ.y, targetZ.z, 0,
                                basePoints[mappedBIdx].x, basePoints[mappedBIdx].y, basePoints[mappedBIdx].z, 1
                            };
                            MMatrix targetMat = createMatrixFromList(tm);
                            MMatrix changeTarget = changeLocal * targetMat;

                            mNewTargetPoints[targetMappedIdx] = MFloatVector((float)changeTarget(3, 0),
                                                                              (float)changeTarget(3, 1),
                                                                              (float)changeTarget(3, 2));
                        }
                    }
                }
            }
        }
        // ---------- Create Symmetry Shape ----------
        else if (mirrorInt == 2)
        {
            // Step 1: flip X on right side
            for (unsigned i = 0; i < bothIndexes.length(); ++i)
            {
                int bIdx = bothIndexes[i];
                if (sideArray[bIdx] == 1 && mapArray[bIdx] != -1)
                    targetPoints[bIdx].x = -targetPoints[bIdx].x;
            }

            // Step 2: average pairs
            for (unsigned i = 0; i < bothIndexes.length(); ++i)
            {
                int bIdx = bothIndexes[i];
                int mIdx = mapArray[bIdx];
                if (mIdx != -1)
                {
                    double avgX = (targetPoints[bIdx].x + targetPoints[mIdx].x) * 0.5;
                    double avgY = (targetPoints[bIdx].y + targetPoints[mIdx].y) * 0.5;
                    double avgZ = (targetPoints[bIdx].z + targetPoints[mIdx].z) * 0.5;
                    targetPoints[bIdx] = MPoint(avgX, avgY, avgZ);
                    targetPoints[mIdx] = MPoint(avgX, avgY, avgZ);

                    if (mIdx == bIdx)
                        targetPoints[bIdx].x = 0.0;
                }
            }

            // Step 3: flip back right side
            for (unsigned i = 0; i < bothIndexes.length(); ++i)
            {
                int bIdx = bothIndexes[i];
                int mIdx = mapArray[bIdx];
                if (mIdx != -1)
                {
                    if (sideArray[bIdx] == 1)
                        targetPoints[bIdx].x = -targetPoints[bIdx].x;

                    mNewTargetPoints[bIdx] = MFloatVector((float)targetPoints[bIdx].x,
                                                           (float)targetPoints[bIdx].y,
                                                           (float)targetPoints[bIdx].z);
                }
            }

            // Report missing mirror points
            MFnSingleIndexedComponent selComp;
            MObject selCompObj = selComp.create(MFn::kMeshVertComponent);
            bool missingPoints = false;
            selectedPoints.reset();
            for (; !selectedPoints.isDone(); selectedPoints.next())
            {
                int idx = selectedPoints.index();
                if (mapArray[idx] == -1)
                {
                    selComp.addElement(idx);
                    missingPoints = true;
                }
            }

            if (missingPoints)
            {
                MSelectionList selList;
                selList.add(dagPathSelShape, selCompObj);
                MGlobal::displayWarning("Some points couldn't be mirrored (see selection)");
                MGlobal::setActiveSelectionList(selList);
            }
            else
            {
                MGlobal::displayInfo("Found mirrorPoint for each selected point");
            }
        }
    }

    return redoIt();
}

// ------------------------------------------------------------
// redoIt / undoIt / isUndoable
// ------------------------------------------------------------
MStatus EdgeFlowMirrorCommand::redoIt()
{
    manipulateObject(true);
    return MS::kSuccess;
}

MStatus EdgeFlowMirrorCommand::undoIt()
{
    manipulateObject(false);
    return MS::kSuccess;
}

bool EdgeFlowMirrorCommand::isUndoable() const
{
    return true;
}

// ------------------------------------------------------------
// manipulateObject - apply / revert geometry or skin changes
// ------------------------------------------------------------
void EdgeFlowMirrorCommand::manipulateObject(bool redo)
{
    if (mTask.length() >= 8 && mTask.substring(0, 7) == "geometry")
    {
        MFnMesh fnTargetMesh(mTargetObj);
        MPointArray pts;
        fnTargetMesh.getPoints(pts);

        unsigned count = std::min(pts.length(), static_cast<unsigned>(mNewTargetPoints.length()));
        const MFloatVectorArray &src = redo ? mNewTargetPoints : mOldTargetPoints;

        for (unsigned i = 0; i < count; ++i)
            pts.set(MPoint(src[i].x, src[i].y, src[i].z), (int)i);

        fnTargetMesh.setPoints(pts, MSpace::kObject);
        fnTargetMesh.updateSurface();
    }

    if (mTask == "skinCluster")
    {
        MFnSkinCluster fnSkinCluster(mSkinCluster);
        MDagPath skinPath;
        fnSkinCluster.getPathAtIndex(0, skinPath);

        MIntArray infIndices;
        for (int i = 0; i < mInfCount; ++i)
            infIndices.append(i);

        // get non-const copy for setWeights
        MDoubleArray wCopy(redo ? mWeightArray : mOldWeightArray);
        fnSkinCluster.setWeights(skinPath, mVtxComponents, infIndices, wCopy, false);
    }
}

// ------------------------------------------------------------
// analyzeTopology - core edge-flow based topology walk
// ------------------------------------------------------------
MStatus EdgeFlowMirrorCommand::analyzeTopology(const MString &edge,
                                                MIntArray &outMapArray,
                                                MIntArray &outSideArray,
                                                MString &outBaseObjectName)
{
    MSelectionList selection;
    selection.add(edge);
    MDagPath dagPath;
    MObject component;
    MItSelectionList selIter(selection);
    selIter.getDagPath(dagPath, component);
    dagPath.extendToShape();

    MItMeshEdge selectedEdges(dagPath, component);
    MFnMesh fnMesh(dagPath);

    int pointCount = fnMesh.numVertices();
    int edgeCount = fnMesh.numEdges();
    int polyCount = fnMesh.numPolygons();
    int prevIndex = 0; // for setIndex() calls
    outBaseObjectName = fnMesh.name();

    // Data structures
    MIntArray checkedV; checkedV.setLength(pointCount);
    MIntArray sideV; sideV.setLength(pointCount);
    MIntArray checkedE; checkedE.setLength(edgeCount);
    MIntArray checkedP; checkedP.setLength(polyCount);
    for (int i = 0; i < pointCount; ++i) { checkedV[i] = -1; sideV[i] = -1; }
    for (int i = 0; i < edgeCount; ++i) checkedE[i] = -1;
    for (int i = 0; i < polyCount; ++i) checkedP[i] = -1;

    // Cache connected edges per face for fast lookup
    MItMeshPolygon polyIter(dagPath);
    std::vector<std::vector<int>> faceEdgesCache(polyCount);
    MIntArray edgeList;
    int prevIdx = 0;
    for (int i = 0; i < polyCount; ++i)
    {
        polyIter.setIndex(i, prevIdx);
        polyIter.getEdges(edgeList);
        std::vector<int> ev;
        ev.reserve(edgeList.length());
        for (unsigned ei = 0; ei < edgeList.length(); ++ei)
            ev.push_back(edgeList[ei]);
        faceEdgesCache[i] = ev;
    }

    selectedEdges.reset();
    int firstEdge = selectedEdges.index();

    MIntArray lFaceList, rFaceList;
    MIntArray lEdgeQueue, rEdgeQueue;
    lEdgeQueue.append(firstEdge);
    rEdgeQueue.append(firstEdge);

    MItMeshEdge edgeIter(dagPath);

    // ---- main walking loop ----
    while (lEdgeQueue.length() > 0)
    {
        int lCurrE = lEdgeQueue[0];
        int rCurrE = rEdgeQueue[0];
        lEdgeQueue.remove(0);
        rEdgeQueue.remove(0);

        checkedE[lCurrE] = rCurrE;
        checkedE[rCurrE] = lCurrE;

        if (lCurrE == rCurrE && lCurrE != firstEdge)
            continue;

        // ---- left face ----
        int lCurrP = -1;
        edgeIter.setIndex(lCurrE, prevIndex);
        edgeIter.getConnectedFaces(lFaceList);

        if (lFaceList.length() == 1)
        {
            lCurrP = lFaceList[0];
        }
        else if (checkedP[lFaceList[0]] == -1 && checkedP[lFaceList[1]] != -1)
        {
            lCurrP = lFaceList[0];
        }
        else if (checkedP[lFaceList[1]] == -1 && checkedP[lFaceList[0]] != -1)
        {
            lCurrP = lFaceList[1];
        }
        else if (checkedP[lFaceList[0]] == -1 && checkedP[lFaceList[1]] == -1)
        {
            lCurrP = lFaceList[0];
            checkedP[lCurrP] = -2;
        }

        // ---- right face ----
        int rCurrP = -1;
        edgeIter.setIndex(rCurrE, prevIndex);
        edgeIter.getConnectedFaces(rFaceList);

        if (rFaceList.length() == 1)
        {
            rCurrP = rFaceList[0];
        }
        else if (checkedP[rFaceList[0]] == -1 && checkedP[rFaceList[1]] != -1)
        {
            rCurrP = rFaceList[0];
        }
        else if (checkedP[rFaceList[1]] == -1 && checkedP[rFaceList[0]] != -1)
        {
            rCurrP = rFaceList[1];
        }
        else if (checkedP[rFaceList[0]] == -1 && checkedP[rFaceList[1]] == -1)
        {
            return MS::kFailure;
        }
        else if (checkedP[rFaceList[0]] != -1 && checkedP[rFaceList[1]] != -1)
        {
            continue;
        }

        checkedP[rCurrP] = lCurrP;
        checkedP[lCurrP] = rCurrP;

        // ---- edge vertex pairs (via MItMeshEdge for compatibility) ----
        int lV0 = 0, lV1 = 0, rV0 = 0, rV1 = 0;
        edgeIter.setIndex(lCurrE, prevIndex);
        lV0 = edgeIter.index(0);
        lV1 = edgeIter.index(1);
        edgeIter.setIndex(rCurrE, prevIndex);
        rV0 = edgeIter.index(0);
        rV1 = edgeIter.index(1);

        if (lCurrE == firstEdge)
        {
            checkedV[lV0] = rV0;
            checkedV[lV1] = rV1;
            checkedV[rV0] = lV0;
            checkedV[rV1] = lV1;
        }
        else
        {
            // Match the Python logic: 4 independent pairing attempts
            if (checkedV[lV0] == -1 && checkedV[rV0] == -1)
            {
                checkedV[lV0] = rV0; checkedV[rV0] = lV0;
            }
            if (checkedV[lV1] == -1 && checkedV[rV1] == -1)
            {
                checkedV[lV1] = rV1; checkedV[rV1] = lV1;
            }
            if (checkedV[lV0] == -1 && checkedV[rV1] == -1)
            {
                checkedV[lV0] = rV1; checkedV[rV1] = lV0;
            }
            if (checkedV[lV1] == -1 && checkedV[rV0] == -1)
            {
                checkedV[lV1] = rV0; checkedV[rV0] = lV1;
            }
        }

        sideV[lV0] = 2;
        sideV[lV1] = 2;
        sideV[rV0] = 1;
        sideV[rV1] = 1;

        // ---- walk to neighboring edges ----
        const auto &lFaceEdges = faceEdgesCache[lCurrP];
        const auto &rFaceEdges = faceEdgesCache[rCurrP];

        for (std::size_t li = 0; li < lFaceEdges.size(); ++li)
        {
            int lfe = lFaceEdges[li];
            if (checkedE[lfe] != -1)
                continue;

            edgeIter.setIndex(lCurrE, prevIndex);
            if (!edgeIter.connectedToEdge(lfe) || lCurrE == lfe)
                continue;

            int lCV0 = 0, lCV1 = 0;
            edgeIter.setIndex(lfe, prevIndex);
            lCV0 = edgeIter.index(0);
            lCV1 = edgeIter.index(1);

            int lCheckedV = -1;
            int lNonCheckedV = -1;
            if (lCV0 == lV0 || lCV0 == lV1)
            {
                lCheckedV = lCV0;
                lNonCheckedV = lCV1;
            }
            else if (lCV1 == lV0 || lCV1 == lV1)
            {
                lCheckedV = lCV1;
                lNonCheckedV = lCV0;
            }
            else
            {
                continue;
            }

            for (std::size_t ri = 0; ri < rFaceEdges.size(); ++ri)
            {
                int rfe = rFaceEdges[ri];

                edgeIter.setIndex(rCurrE, prevIndex);
                if (!edgeIter.connectedToEdge(rfe) || rCurrE == rfe)
                    continue;

                int rFV0 = 0, rFV1 = 0;
                edgeIter.setIndex(rfe, prevIndex);
                rFV0 = edgeIter.index(0);
                rFV1 = edgeIter.index(1);

                if (rFV0 == checkedV[lCheckedV])
                {
                    checkedV[lNonCheckedV] = rFV1;
                    checkedV[rFV1] = lNonCheckedV;
                    sideV[lNonCheckedV] = 2;
                    sideV[rFV1] = 1;
                    lEdgeQueue.append(lfe);
                    rEdgeQueue.append(rfe);
                }
                if (rFV1 == checkedV[lCheckedV])
                {
                    checkedV[lNonCheckedV] = rFV0;
                    checkedV[rFV0] = lNonCheckedV;
                    sideV[lNonCheckedV] = 2;
                    sideV[rFV0] = 1;
                    lEdgeQueue.append(lfe);
                    rEdgeQueue.append(rfe);
                }
            }
        }
    }

    // ---- determine left/right side by X position ----
    double xAvg2 = 0, xAvg1 = 0;
    int cnt2 = 0, cnt1 = 0;
    MPoint checkPos;

    for (int i = 0; i < pointCount; ++i)
    {
        if (checkedV[i] != i && checkedV[i] != -1)
        {
            fnMesh.getPoint(checkedV[i], checkPos);
            if (sideV[i] == 2) { xAvg2 += checkPos.x; ++cnt2; }
            if (sideV[i] == 1) { xAvg1 += checkPos.x; ++cnt1; }
        }
    }

    bool switchSide = (xAvg2 < xAvg1);

    // ---- build output ----
    outMapArray.setLength(pointCount);
    outSideArray.setLength(pointCount);

    for (int i = 0; i < pointCount; ++i)
    {
        outMapArray[i] = checkedV[i];

        if (checkedV[i] != i)
        {
            int s = sideV[i];
            outSideArray[i] = switchSide ? (s == 2 ? 1 : 2) : s;
        }
        else
        {
            outSideArray[i] = 0;
        }
    }

    for (int i = 0; i < pointCount; ++i)
    {
        if (outMapArray[i] == -1)
            outMapArray[i] = i;
    }

    return MS::kSuccess;
}

// ============================================================
// Helper: createMatrixFromPos
// ============================================================
MMatrix createMatrixFromPos(const MPoint &pos)
{
    double data[4][4] = {
        {1, 0, 0, 0},
        {0, 1, 0, 0},
        {0, 0, 1, 0},
        {pos.x, pos.y, pos.z, 1}};
    return MMatrix(data);
}

// ============================================================
// Helper: createMatrixFromList
// ============================================================
MMatrix createMatrixFromList(const double matList[16])
{
    double data[4][4];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            data[r][c] = matList[r * 4 + c];
    return MMatrix(data);
}

// ============================================================
// Plugin entry points
// ============================================================
MStatus initializePlugin(MObject obj)
{
    MFnPlugin plugin(obj, "Thomas Bittner", "3.4", "Any");
    MStatus stat = plugin.registerCommand(kPluginCmdName,
                                           EdgeFlowMirrorCommand::creator,
                                           EdgeFlowMirrorCommand::newSyntax);
    if (!stat)
    {
        stat.perror("Failed to register command: edgeFlowMirror");
        return stat;
    }
    return MS::kSuccess;
}

MStatus uninitializePlugin(MObject obj)
{
    MFnPlugin plugin(obj);
    MStatus stat = plugin.deregisterCommand(kPluginCmdName);
    if (!stat)
    {
        stat.perror("Failed to unregister command: edgeFlowMirror");
        return stat;
    }
    return MS::kSuccess;
}
