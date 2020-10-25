﻿#include "kernel_common.cuh"

namespace VLR {
    RT_CALLABLE_PROGRAM void decodeHitPointForTriangle(const HitPointParameter &param, SurfacePoint* surfPt, float* hypAreaPDF) {
        const auto &sbtr = HitGroupSBTRecordData::get();

        const Triangle &triangle = sbtr.geomInst.asTriMesh.triangleBuffer[param.primIndex];
        const Vertex &v0 = sbtr.geomInst.asTriMesh.vertexBuffer[triangle.index0];
        const Vertex &v1 = sbtr.geomInst.asTriMesh.vertexBuffer[triangle.index1];
        const Vertex &v2 = sbtr.geomInst.asTriMesh.vertexBuffer[triangle.index2];

        Vector3D e1 = v1.position - v0.position;
        Vector3D e2 = v2.position - v0.position;
        Normal3D geometricNormal = cross(e1, e2);
        float area = geometricNormal.length() / 2; // TODO: スケーリングの考慮。
        geometricNormal /= 2 * area;

        // JP: プログラムがこの点を光源としてサンプルする場合の面積に関する(仮想的な)PDFを求める。
        // EN: calculate a hypothetical area PDF value in the case where the program sample this point as light.
        float probLightPrim = area / sbtr.geomInst.asTriMesh.primDistribution.integral();
        *hypAreaPDF = probLightPrim / area;

        float b0 = 1 - param.b1 - param.b2, b1 = param.b1, b2 = param.b2;
        Point3D position = b0 * v0.position + b1 * v1.position + b2 * v2.position;
        Normal3D shadingNormal = normalize(b0 * v0.normal + b1 * v1.normal + b2 * v2.normal);
        Vector3D tc0Direction = b0 * v0.tc0Direction + b1 * v1.tc0Direction + b2 * v2.tc0Direction;
        TexCoord2D texCoord = b0 * v0.texCoord + b1 * v1.texCoord + b2 * v2.texCoord;

        // JP: 法線と接線が直交することを保証する。
        //     直交性の消失は重心座標補間によっておこる？
        // EN: guarantee the orthogonality between the normal and tangent.
        //     Orthogonality break might be caused by barycentric interpolation?
        float dotNT = dot(shadingNormal, tc0Direction);
        tc0Direction = tc0Direction - dotNT * shadingNormal;

        surfPt->position = position;
        surfPt->shadingFrame.x = tc0Direction;
        surfPt->shadingFrame.z = shadingNormal;
        surfPt->isPoint = false;
        surfPt->atInfinity = false;
        surfPt->geometricNormal = geometricNormal;
        surfPt->u = b0;
        surfPt->v = b1;
        surfPt->texCoord = texCoord;
    }



    RT_CALLABLE_PROGRAM void sampleTriangleMesh(const Instance &inst, const GeometryInstance &geomInst,
                                                const SurfaceLightPosSample &sample, SurfaceLightPosQueryResult* result) {
        float primProb;
        uint32_t primIdx = geomInst.asTriMesh.primDistribution.sample(sample.uElem, &primProb);

        const Triangle &triangle = geomInst.asTriMesh.triangleBuffer[primIdx];
        const Vertex &v0 = geomInst.asTriMesh.vertexBuffer[triangle.index0];
        const Vertex &v1 = geomInst.asTriMesh.vertexBuffer[triangle.index1];
        const Vertex &v2 = geomInst.asTriMesh.vertexBuffer[triangle.index2];

        const StaticTransform &transform = inst.transform;

        Vector3D e1 = transform * (v1.position - v0.position);
        Vector3D e2 = transform * (v2.position - v0.position);
        Normal3D geometricNormal = cross(e1, e2);
        float area = geometricNormal.length() / 2;
        geometricNormal /= 2 * area;

        result->areaPDF = primProb / area;
        result->posType = DirectionType::Emission() | DirectionType::LowFreq();
        result->materialIndex = geomInst.materialIndex;

        float b0, b1, b2;
        uniformSampleTriangle(sample.uPos[0], sample.uPos[1], &b0, &b1);
        b2 = 1.0f - b0 - b1;

        Point3D position = b0 * v0.position + b1 * v1.position + b2 * v2.position;
        Normal3D shadingNormal = b0 * v0.normal + b1 * v1.normal + b2 * v2.normal;
        Vector3D tc0Direction = b0 * v0.tc0Direction + b1 * v1.tc0Direction + b2 * v2.tc0Direction;
        TexCoord2D texCoord = b0 * v0.texCoord + b1 * v1.texCoord + b2 * v2.texCoord;

        position = transform * position;
        shadingNormal = normalize(transform * shadingNormal);
        tc0Direction = transform * tc0Direction;

        // JP: 法線と接線が直交することを保証する。
        //     直交性の消失は重心座標補間によっておこる？
        // EN: guarantee the orthogonality between the normal and tangent.
        //     Orthogonality break might be caused by barycentric interpolation?
        float dotNT = dot(shadingNormal, tc0Direction);
        tc0Direction = normalize(tc0Direction - dotNT * shadingNormal);

        SurfacePoint &surfPt = result->surfPt;

        surfPt.position = position;
        surfPt.shadingFrame = ReferenceFrame(tc0Direction, shadingNormal);
        surfPt.isPoint = false;
        surfPt.atInfinity = false;
        surfPt.geometricNormal = geometricNormal;
        surfPt.u = b0;
        surfPt.v = b1;
        surfPt.texCoord = texCoord;
    }
}
