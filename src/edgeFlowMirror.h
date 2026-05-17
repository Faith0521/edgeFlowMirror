#ifndef EDGE_FLOW_MIRROR_H
#define EDGE_FLOW_MIRROR_H

#include <maya/MPxCommand.h>
#include <maya/MSyntax.h>
#include <maya/MDagPath.h>
#include <maya/MObject.h>
#include <maya/MPointArray.h>
#include <maya/MFloatVectorArray.h>
#include <maya/MDoubleArray.h>
#include <maya/MIntArray.h>
#include <maya/MMatrix.h>
#include <maya/MString.h>
#include <vector>

class EdgeFlowMirrorCommand : public MPxCommand
{
public:
    EdgeFlowMirrorCommand();
    ~EdgeFlowMirrorCommand() override;

    MStatus doIt(const MArgList &args) override;
    MStatus redoIt() override;
    MStatus undoIt() override;
    bool isUndoable() const override;

    static void *creator();
    static MSyntax newSyntax();

private:
    // Core topology analysis
    MStatus analyzeTopology(const MString &edge,
                            MIntArray &outMapArray,
                            MIntArray &outSideArray,
                            MString &outBaseObjectName);

    // Geometry / skin manipulation
    void manipulateObject(bool redo);

    // Cached data for undo/redo
    MFloatVectorArray mOldTargetPoints;
    MFloatVectorArray mNewTargetPoints;
    MDagPath mTargetObj;

    MDoubleArray mWeightArray;
    MDoubleArray mOldWeightArray;
    int mInfCount;
    MObject mVtxComponents;
    MObject mSkinCluster;

    MString mTask;

    // Global cached topology (persists between calls when optimize=true)
    static MIntArray sSavedMapArray;
    static MIntArray sSavedSideArray;
    static MString sSavedBaseObjectName;
};

// ============================================================
// Helper functions
// ============================================================

MMatrix createMatrixFromPos(const MPoint &pos);
MMatrix createMatrixFromList(const double matList[16]);

#endif // EDGE_FLOW_MIRROR_H
