﻿#include "../shared/light_transport_common.h"

namespace vlr {
    using namespace shared;

    static constexpr int32_t debugPathLength = 0;

    CUDA_DEVICE_FUNCTION bool onProbePixel() {
        return optixGetLaunchIndex().x == plp.probePixX && optixGetLaunchIndex().y == plp.probePixY;
    }



    // Common Any Hit Program for All Primitive Types and Materials for non-shadow rays
    CUDA_DEVICE_KERNEL void RT_AH_NAME(pathTracingAnyHitWithAlpha)() {
        PTReadOnlyPayload* roPayload;
        PTReadWritePayload* rwPayload;
        PTPayloadSignature::get(&roPayload, nullptr, &rwPayload, nullptr);

        float alpha = getAlpha(roPayload->wls);

        // Stochastic Alpha Test
        if (rwPayload->rng.getFloat0cTo1o() >= alpha)
            optixIgnoreIntersection();
    }



    // Common Ray Generation Program for All Camera Types
    CUDA_DEVICE_KERNEL void RT_RG_NAME(pathTracing)() {
        uint2 launchIndex = make_uint2(optixGetLaunchIndex().x, optixGetLaunchIndex().y);

        KernelRNG rng = plp.rngBuffer.read(launchIndex);

        float2 p = make_float2(launchIndex.x + rng.getFloat0cTo1o(),
                               launchIndex.y + rng.getFloat0cTo1o());

        float selectWLPDF;
        WavelengthSamples wls = WavelengthSamples::createWithEqualOffsets(rng.getFloat0cTo1o(), rng.getFloat0cTo1o(), &selectWLPDF);

        Camera camera(static_cast<ProgSigCamera_sample>(plp.progSampleLensPosition));
        LensPosSample We0Sample(rng.getFloat0cTo1o(), rng.getFloat0cTo1o());
        LensPosQueryResult We0Result;
        camera.sample(We0Sample, &We0Result);

        IDF idf(plp.cameraDescriptor, We0Result.surfPt, wls);

        SampledSpectrum We0 = idf.evaluateSpatialImportance();

        IDFSample We1Sample(p.x / plp.imageSize.x, p.y / plp.imageSize.y);
        IDFQueryResult We1Result;
        SampledSpectrum We1 = idf.sample(IDFQuery(), We1Sample, &We1Result);
        We1Result.dirPDF *= plp.imageSize.x * plp.imageSize.y;

        Point3D rayOrg = We0Result.surfPt.position;
        Vector3D rayDir = We0Result.surfPt.fromLocal(We1Result.dirLocal);
        SampledSpectrum alpha = (We0 * We1) * (We0Result.surfPt.calcCosTerm(rayDir) /
                                               (We0Result.areaPDF * We1Result.dirPDF * selectWLPDF));

        PTReadOnlyPayload roPayload = {};
        roPayload.initImportance = alpha.importance(wls.selectedLambdaIndex());
        roPayload.wls = wls;
        roPayload.prevDirPDF = We1Result.dirPDF;
        roPayload.prevSampledType = We1Result.sampledType;
        roPayload.pathLength = 0;
        roPayload.maxLengthTerminate = false;
        PTWriteOnlyPayload woPayload = {};
        PTReadWritePayload rwPayload = {};
        rwPayload.rng = rng;
        rwPayload.alpha = alpha;
        rwPayload.contribution = SampledSpectrum::Zero();
        PTExtraPayload exPayload = {};
        PTReadOnlyPayload* roPayloadPtr = &roPayload;
        PTWriteOnlyPayload* woPayloadPtr = &woPayload;
        PTReadWritePayload* rwPayloadPtr = &rwPayload;
        PTExtraPayload* exPayloadPtr = &exPayload;

        const uint32_t MaxPathLength = 25;
        while (true) {
            woPayload.terminate = true;
            ++roPayload.pathLength;
            if (roPayload.pathLength >= MaxPathLength)
                roPayload.maxLengthTerminate = true;

            if (debugPathLength != 0 &&
                roPayload.pathLength > debugPathLength)
                break;

            optixu::trace<PTPayloadSignature>(
                plp.topGroup, asOptiXType(rayOrg), asOptiXType(rayDir), 0.0f, FLT_MAX, 0.0f,
                VisibilityGroup_Everything, OPTIX_RAY_FLAG_NONE,
                PTRayType::Closest, MaxNumRayTypes, PTRayType::Closest,
                roPayloadPtr, woPayloadPtr, rwPayloadPtr, exPayloadPtr);

            if (roPayload.pathLength == 1) {
                uint32_t linearIndex = launchIndex.y * plp.imageStrideInPixels + launchIndex.x;
                DiscretizedSpectrum &accumAlbedo = plp.accumAlbedoBuffer[linearIndex];
                Normal3D &accumNormal = plp.accumNormalBuffer[linearIndex];
                if (plp.numAccumFrames == 1) {
                    accumAlbedo = DiscretizedSpectrum::Zero();
                    accumNormal = Normal3D(0.0f, 0.0f, 0.0f);
                }
                TripletSpectrum whitePoint = createTripletSpectrum(SpectrumType::LightSource, ColorSpace::Rec709_D65,
                                                                   1, 1, 1);
                accumAlbedo += DiscretizedSpectrum(wls, exPayload.firstHitAlbedo * whitePoint.evaluate(wls) / selectWLPDF);
                accumNormal += exPayload.firstHitNormal;
                exPayloadPtr = nullptr;
            }

            if (woPayload.terminate)
                break;
            VLRAssert(roPayload.pathLength < MaxPathLength, "Path should be terminated... Something went wrong...");

            rayOrg = woPayload.nextOrigin;
            rayDir = woPayload.nextDirection;
            roPayload.prevDirPDF = woPayload.dirPDF;
            roPayload.prevSampledType = woPayload.sampledType;
        }
        plp.rngBuffer.write(launchIndex, rwPayload.rng);
        if (!rwPayload.contribution.allFinite()) {
            vlrprintf("Pass %u, (%u, %u): Not a finite value.\n", plp.numAccumFrames, launchIndex.x, launchIndex.y);
            return;
        }

        if (plp.numAccumFrames == 1)
            plp.accumBuffer[launchIndex].reset();
        plp.accumBuffer[launchIndex].add(wls, rwPayload.contribution);
    }



    // Common Closest Hit Program for All Primitive Types and Materials
    CUDA_DEVICE_KERNEL void RT_CH_NAME(pathTracingIteration)() {
        const auto hp = HitPointParameter::get();

        PTReadOnlyPayload* roPayload;
        PTWriteOnlyPayload* woPayload;
        PTReadWritePayload* rwPayload;
        PTExtraPayload* exPayload;
        PTPayloadSignature::get(&roPayload, &woPayload, &rwPayload, &exPayload);

        KernelRNG &rng = rwPayload->rng;
        WavelengthSamples &wls = roPayload->wls;

        SurfacePoint surfPt;
        float hypAreaPDF;
        calcSurfacePoint(hp, wls, &surfPt, &hypAreaPDF);

        const SurfaceMaterialDescriptor &matDesc = plp.materialDescriptorBuffer[hp.sbtr->geomInst.materialIndex];
        constexpr TransportMode transportMode = TransportMode::Radiance;
        BSDF<transportMode> bsdf(matDesc, surfPt, wls);
        EDF edf(matDesc, surfPt, wls);

        if (exPayload) {
            exPayload->firstHitAlbedo = bsdf.getBaseColor();
            exPayload->firstHitNormal = surfPt.shadingFrame.z;
        }

        Vector3D dirOutLocal = surfPt.shadingFrame.toLocal(-asVector3D(optixGetWorldRayDirection()));

        // implicit light sampling
        SampledSpectrum spEmittance = edf.evaluateEmittance();
        if ((debugPathLength == 0 || roPayload->pathLength == debugPathLength) &&
            spEmittance.hasNonZero()) {
            EDFQuery feQuery(DirectionType::All(), wls);
            SampledSpectrum Le = spEmittance * edf.evaluate(feQuery, dirOutLocal);

            float MISWeight = 1.0f;
            if (!roPayload->prevSampledType.isDelta() && roPayload->pathLength > 1) {
                const Instance &inst = plp.instBuffer[surfPt.instIndex];
                float instProb = inst.lightGeomInstDistribution.integral() / plp.lightInstDist.integral();
                float geomInstProb = hp.sbtr->geomInst.importance / inst.lightGeomInstDistribution.integral();

                float bsdfPDF = roPayload->prevDirPDF;
                float dist2 = surfPt.calcSquaredDistance(asPoint3D(optixGetWorldRayOrigin()));
                float lightPDF = instProb * geomInstProb * hypAreaPDF * dist2 / std::fabs(dirOutLocal.z);
                MISWeight = (bsdfPDF * bsdfPDF) / (lightPDF * lightPDF + bsdfPDF * bsdfPDF);
            }

            rwPayload->contribution += rwPayload->alpha * Le * MISWeight;
        }
        if (surfPt.atInfinity || roPayload->maxLengthTerminate)
            return;

        // Russian roulette
        float continueProb = std::fmin(rwPayload->alpha.importance(wls.selectedLambdaIndex()) / roPayload->initImportance, 1.0f);
        if (rng.getFloat0cTo1o() >= continueProb)
            return;
        rwPayload->alpha /= continueProb;

        Normal3D geomNormalLocal = surfPt.shadingFrame.toLocal(surfPt.geometricNormal);
        BSDFQuery fsQuery(dirOutLocal, geomNormalLocal, transportMode, DirectionType::All(), wls);

        // Next Event Estimation (explicit light sampling)
        if ((debugPathLength == 0 || (roPayload->pathLength + 1) == debugPathLength) &&
            bsdf.hasNonDelta()) {
            float uLight = rng.getFloat0cTo1o();
            SurfaceLight light;
            float lightProb;
            float uPrim;
            selectSurfaceLight(uLight, &light, &lightProb, &uPrim);

            SurfaceLightPosSample lpSample(uPrim, rng.getFloat0cTo1o(), rng.getFloat0cTo1o());
            SurfaceLightPosQueryResult lpResult;
            light.sample(lpSample, surfPt.position, &lpResult);

            const SurfaceMaterialDescriptor &lightMatDesc = plp.materialDescriptorBuffer[lpResult.materialIndex];
            EDF ledf(lightMatDesc, lpResult.surfPt, wls);
            SampledSpectrum M = ledf.evaluateEmittance();

            Vector3D shadowRayDir;
            float squaredDistance;
            float fractionalVisibility;
            if (M.hasNonZero() &&
                testVisibility<PTRayType::Shadow>(
                    surfPt, lpResult.surfPt, wls, &shadowRayDir, &squaredDistance,
                    &fractionalVisibility)) {
                float recSquaredDistance = 1.0f / squaredDistance;
                Vector3D shadowRayDir_l = lpResult.surfPt.toLocal(-shadowRayDir);
                Vector3D shadowRayDir_sn = surfPt.toLocal(shadowRayDir);

                EDFQuery feQuery(DirectionType::All(), wls);
                SampledSpectrum Le = M * ledf.evaluate(feQuery, shadowRayDir_l);
                float lightPDF = lightProb * lpResult.areaPDF;

                SampledSpectrum fs = bsdf.evaluate(fsQuery, shadowRayDir_sn);
                float cosLight = lpResult.surfPt.calcCosTerm(-shadowRayDir);
                float bsdfPDF = bsdf.evaluatePDF(fsQuery, shadowRayDir_sn) * cosLight * recSquaredDistance;

                float MISWeight = 1.0f;
                if (!lpResult.posType.isDelta() && !::vlr::isinf(lightPDF))
                    MISWeight = (lightPDF * lightPDF) / (lightPDF * lightPDF + bsdfPDF * bsdfPDF);

                float G = fractionalVisibility * absDot(shadowRayDir_sn, geomNormalLocal) * cosLight * recSquaredDistance;
                float scalarCoeff = G * MISWeight / lightPDF; // 直接contributionの計算式に入れるとCUDAのバグなのかおかしな結果になる。
                rwPayload->contribution += rwPayload->alpha * Le * fs * scalarCoeff;
            }
        }

        BSDFSample sample(rng.getFloat0cTo1o(), rng.getFloat0cTo1o(), rng.getFloat0cTo1o());
        BSDFQueryResult fsResult;
        SampledSpectrum fs = bsdf.sample(fsQuery, sample, &fsResult);
        if (fs == SampledSpectrum::Zero() || fsResult.dirPDF == 0.0f)
            return;
        if (fsResult.sampledType.isDispersive() && !wls.singleIsSelected()) {
            fsResult.dirPDF /= SampledSpectrum::NumComponents();
            wls.setSingleIsSelected();
        }

        float cosFactor = dot(fsResult.dirLocal, geomNormalLocal);
        rwPayload->alpha *= fs * (std::fabs(cosFactor) / fsResult.dirPDF);

        Vector3D dirIn = surfPt.fromLocal(fsResult.dirLocal);
        woPayload->nextOrigin = offsetRayOrigin(surfPt.position, cosFactor > 0.0f ? surfPt.geometricNormal : -surfPt.geometricNormal);
        woPayload->nextDirection = dirIn;
        woPayload->dirPDF = fsResult.dirPDF;
        woPayload->sampledType = fsResult.sampledType;
        woPayload->terminate = false;
    }



    // JP: 本当は無限大の球のIntersection/Bounding Box Programを使用して環境光に関する処理もClosest Hit Programで統一的に行いたい。
    //     が、OptiXのBVHビルダーがLBVHベースなので無限大のAABBを生成するのは危険。
    //     仕方なくMiss Programで環境光を処理する。
    CUDA_DEVICE_KERNEL void RT_MS_NAME(pathTracingMiss)() {
        PTReadOnlyPayload* roPayload;
        PTReadWritePayload* rwPayload;
        PTExtraPayload* exPayload;
        PTPayloadSignature::get(&roPayload, nullptr, &rwPayload, &exPayload);

        if (exPayload) {
            exPayload->firstHitAlbedo = SampledSpectrum::Zero();
            exPayload->firstHitNormal = Normal3D(0.0f, 0.0f, 0.0f);
        }

        const Instance &inst = plp.instBuffer[plp.envLightInstIndex];
        const GeometryInstance &geomInst = plp.geomInstBuffer[inst.geomInstIndices[0]];

        if (geomInst.importance == 0)
            return;

        Vector3D direction = asVector3D(optixGetWorldRayDirection());
        float phi, theta;
        direction.toPolarYUp(&theta, &phi);

        float sinPhi, cosPhi;
        ::vlr::sincos(phi, &sinPhi, &cosPhi);
        Vector3D texCoord0Dir = normalize(Vector3D(-cosPhi, 0.0f, -sinPhi));
        ReferenceFrame shadingFrame;
        shadingFrame.x = texCoord0Dir;
        shadingFrame.z = -direction;
        shadingFrame.y = cross(shadingFrame.z, shadingFrame.x);

        SurfacePoint surfPt;
        surfPt.position = Point3D(direction.x, direction.y, direction.z);
        surfPt.shadingFrame = shadingFrame;
        surfPt.isPoint = false;
        surfPt.atInfinity = true;

        surfPt.geometricNormal = -direction;
        surfPt.u = phi;
        surfPt.v = theta;
        phi += inst.rotationPhi;
        phi = phi - ::vlr::floor(phi / (2 * VLR_M_PI)) * 2 * VLR_M_PI;
        surfPt.texCoord = TexCoord2D(phi / (2 * VLR_M_PI), theta / VLR_M_PI);

        VLRAssert(vlr::isfinite(phi) && vlr::isfinite(theta), "\"phi\", \"theta\": Not finite values %g, %g.", phi, theta);
        float uvPDF = geomInst.asInfSphere.importanceMap.evaluatePDF(phi / (2 * VLR_M_PI), theta / VLR_M_PI);
        float hypAreaPDF = uvPDF / (2 * VLR_M_PI * VLR_M_PI * std::sin(theta));

        const SurfaceMaterialDescriptor &matDesc = plp.materialDescriptorBuffer[geomInst.materialIndex];
        EDF edf(matDesc, surfPt, roPayload->wls);

        Vector3D dirOutLocal = surfPt.shadingFrame.toLocal(-direction);

        // implicit light sampling
        SampledSpectrum spEmittance = edf.evaluateEmittance();
        if ((debugPathLength == 0 || roPayload->pathLength == debugPathLength) &&
            spEmittance.hasNonZero()) {
            EDFQuery feQuery(DirectionType::All(), roPayload->wls);
            SampledSpectrum Le = spEmittance * edf.evaluate(feQuery, dirOutLocal);

            float MISWeight = 1.0f;
            if (!roPayload->prevSampledType.isDelta() && roPayload->pathLength > 1) {
                float instProb = inst.lightGeomInstDistribution.integral() / plp.lightInstDist.integral();
                float geomInstProb = geomInst.importance / inst.lightGeomInstDistribution.integral();

                float bsdfPDF = roPayload->prevDirPDF;
                float dist2 = surfPt.calcSquaredDistance(asPoint3D(optixGetWorldRayOrigin()));
                float lightPDF = instProb * geomInstProb * hypAreaPDF * dist2 / std::fabs(dirOutLocal.z);
                MISWeight = (bsdfPDF * bsdfPDF) / (lightPDF * lightPDF + bsdfPDF * bsdfPDF);
            }

            rwPayload->contribution += rwPayload->alpha * Le * MISWeight;
        }
    }
}
