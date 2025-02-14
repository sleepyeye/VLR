﻿#include "context.h"

#include <random>

#include "scene.h"

namespace vlr {
    cudau::BufferType g_bufferType = cudau::BufferType::Device;

    std::string readTxtFile(const std::filesystem::path& filepath) {
        std::ifstream ifs;
        ifs.open(filepath, std::ios::in);
        if (ifs.fail()) {
            vlrprintf("Failed to read the text file: %ls", filepath.c_str());
            return "";
        }

        std::stringstream sstream;
        sstream << ifs.rdbuf();

        return std::string(sstream.str());
    };



    Object::Object(Context &context) : m_context(context) {
    }

#define VLR_DEFINE_CLASS_ID(BaseType, Type) \
    const char* const Type::TypeName = #Type; \
    const ClassIdentifier Type::ClassID = ClassIdentifier(&BaseType::ClassID)

    const ClassIdentifier TypeAwareClass::ClassID = ClassIdentifier(nullptr);

    VLR_DEFINE_CLASS_ID(TypeAwareClass, Context);
    VLR_DEFINE_CLASS_ID(TypeAwareClass, Object);

    VLR_DEFINE_CLASS_ID(Object, Queryable);

    VLR_DEFINE_CLASS_ID(Queryable, Image2D);
    VLR_DEFINE_CLASS_ID(Image2D, LinearImage2D);
    VLR_DEFINE_CLASS_ID(Image2D, BlockCompressedImage2D);

    VLR_DEFINE_CLASS_ID(Queryable, ShaderNode);
    VLR_DEFINE_CLASS_ID(ShaderNode, GeometryShaderNode);
    VLR_DEFINE_CLASS_ID(ShaderNode, TangentShaderNode);
    VLR_DEFINE_CLASS_ID(ShaderNode, Float2ShaderNode);
    VLR_DEFINE_CLASS_ID(ShaderNode, Float3ShaderNode);
    VLR_DEFINE_CLASS_ID(ShaderNode, Float4ShaderNode);
    VLR_DEFINE_CLASS_ID(ShaderNode, ScaleAndOffsetFloatShaderNode);
    VLR_DEFINE_CLASS_ID(ShaderNode, TripletSpectrumShaderNode);
    VLR_DEFINE_CLASS_ID(ShaderNode, RegularSampledSpectrumShaderNode);
    VLR_DEFINE_CLASS_ID(ShaderNode, IrregularSampledSpectrumShaderNode);
    VLR_DEFINE_CLASS_ID(ShaderNode, Float3ToSpectrumShaderNode);
    VLR_DEFINE_CLASS_ID(ShaderNode, ScaleAndOffsetUVTextureMap2DShaderNode);
    VLR_DEFINE_CLASS_ID(ShaderNode, Image2DTextureShaderNode);
    VLR_DEFINE_CLASS_ID(ShaderNode, EnvironmentTextureShaderNode);

    VLR_DEFINE_CLASS_ID(Queryable, SurfaceMaterial);
    VLR_DEFINE_CLASS_ID(SurfaceMaterial, MatteSurfaceMaterial);
    VLR_DEFINE_CLASS_ID(SurfaceMaterial, SpecularReflectionSurfaceMaterial);
    VLR_DEFINE_CLASS_ID(SurfaceMaterial, SpecularScatteringSurfaceMaterial);
    VLR_DEFINE_CLASS_ID(SurfaceMaterial, MicrofacetReflectionSurfaceMaterial);
    VLR_DEFINE_CLASS_ID(SurfaceMaterial, MicrofacetScatteringSurfaceMaterial);
    VLR_DEFINE_CLASS_ID(SurfaceMaterial, LambertianScatteringSurfaceMaterial);
    VLR_DEFINE_CLASS_ID(SurfaceMaterial, UE4SurfaceMaterial);
    VLR_DEFINE_CLASS_ID(SurfaceMaterial, OldStyleSurfaceMaterial);
    VLR_DEFINE_CLASS_ID(SurfaceMaterial, DiffuseEmitterSurfaceMaterial);
    VLR_DEFINE_CLASS_ID(SurfaceMaterial, DirectionalEmitterSurfaceMaterial);
    VLR_DEFINE_CLASS_ID(SurfaceMaterial, PointEmitterSurfaceMaterial);
    VLR_DEFINE_CLASS_ID(SurfaceMaterial, MultiSurfaceMaterial);
    VLR_DEFINE_CLASS_ID(SurfaceMaterial, EnvironmentEmitterSurfaceMaterial);

    VLR_DEFINE_CLASS_ID(Object, Transform);
    VLR_DEFINE_CLASS_ID(Transform, StaticTransform);

    VLR_DEFINE_CLASS_ID(Object, Node);
    VLR_DEFINE_CLASS_ID(Node, SurfaceNode);
    VLR_DEFINE_CLASS_ID(SurfaceNode, TriangleMeshSurfaceNode);
    VLR_DEFINE_CLASS_ID(SurfaceNode, PointSurfaceNode);
    VLR_DEFINE_CLASS_ID(SurfaceNode, InfiniteSphereSurfaceNode);
    VLR_DEFINE_CLASS_ID(Node, ParentNode);
    VLR_DEFINE_CLASS_ID(ParentNode, InternalNode);
    VLR_DEFINE_CLASS_ID(ParentNode, Scene);

    VLR_DEFINE_CLASS_ID(Queryable, Camera);
    VLR_DEFINE_CLASS_ID(Camera, PerspectiveCamera);
    VLR_DEFINE_CLASS_ID(Camera, EquirectangularCamera);

#undef VLR_DEFINE_CLASS_ID



    struct EntryPoint {
        enum Value {
            PathTracing = 0,
            DebugRendering,
            NumEntryPoints
        } value;

        constexpr EntryPoint(Value v) : value(v) {}
    };



    uint32_t Context::NextID = 0;

    Context::Context(CUcontext cuContext, bool logging, uint32_t maxCallableDepth) {
        const std::filesystem::path exeDir = getExecutableDirectory();

        vlrprintf("Start initializing VLR ...");

        initializeColorSystem();

        m_ID = getInstanceID();

        m_cuContext = cuContext;

        m_optix = {};
        m_optix.nodeProcedureSetBuffer.initialize(m_cuContext, 256);
        m_optix.smallNodeDescriptorBuffer.initialize(m_cuContext, 8192);
        m_optix.mediumNodeDescriptorBuffer.initialize(m_cuContext, 8192);
        m_optix.largeNodeDescriptorBuffer.initialize(m_cuContext, 1024);
        m_optix.bsdfProcedureSetBuffer.initialize(m_cuContext, 64);
        m_optix.edfProcedureSetBuffer.initialize(m_cuContext, 64);
        m_optix.idfProcedureSetBuffer.initialize(m_cuContext, 8);
        m_optix.launchParams.nodeProcedureSetBuffer = m_optix.nodeProcedureSetBuffer.optixBuffer.getDevicePointer();
        m_optix.launchParams.smallNodeDescriptorBuffer = m_optix.smallNodeDescriptorBuffer.optixBuffer.getDevicePointer();
        m_optix.launchParams.mediumNodeDescriptorBuffer = m_optix.mediumNodeDescriptorBuffer.optixBuffer.getDevicePointer();
        m_optix.launchParams.largeNodeDescriptorBuffer = m_optix.largeNodeDescriptorBuffer.optixBuffer.getDevicePointer();
        m_optix.launchParams.bsdfProcedureSetBuffer = m_optix.bsdfProcedureSetBuffer.optixBuffer.getDevicePointer();
        m_optix.launchParams.edfProcedureSetBuffer = m_optix.edfProcedureSetBuffer.optixBuffer.getDevicePointer();
        m_optix.launchParams.idfProcedureSetBuffer = m_optix.idfProcedureSetBuffer.optixBuffer.getDevicePointer();

        m_optix.surfaceMaterialDescriptorBuffer.initialize(m_cuContext, 8192);
        m_optix.launchParams.materialDescriptorBuffer = m_optix.surfaceMaterialDescriptorBuffer.optixBuffer.getDevicePointer();

        m_optix.context = optixu::Context::create(cuContext/*, 4, true*/);

        m_optix.materialDefault = m_optix.context.createMaterial();
        m_optix.materialWithAlpha = m_optix.context.createMaterial();

        // Pipeline for Path Tracing
        {
            OptiX::PathTracing &p = m_optix.pathTracing;

            p.pipeline = m_optix.context.createPipeline();
            p.pipeline.setPipelineOptions(
                std::max(shared::PTPayloadSignature::numDwords,
                         shared::ShadowPayloadSignature::numDwords),
                static_cast<uint32_t>(optixu::calcSumDwords<float2>()),
                "plp", sizeof(shared::PipelineLaunchParameters),
                false,
                OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_LEVEL_INSTANCING,
                VLR_DEBUG_SELECT(OPTIX_EXCEPTION_FLAG_STACK_OVERFLOW |
                                 OPTIX_EXCEPTION_FLAG_TRACE_DEPTH |
                                 OPTIX_EXCEPTION_FLAG_DEBUG,
                                 OPTIX_EXCEPTION_FLAG_STACK_OVERFLOW),
                OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE);
            p.pipeline.setNumMissRayTypes(shared::PTRayType::NumTypes);

            p.modules.resize(NumOptiXModules);
            p.modules[OptiXModule_LightTransport] = p.pipeline.createModuleFromPTXString(
                readTxtFile(exeDir / "ptxes/path_tracing.ptx"),
                OPTIX_COMPILE_DEFAULT_MAX_REGISTER_COUNT,
                VLR_DEBUG_SELECT(OPTIX_COMPILE_OPTIMIZATION_LEVEL_0, OPTIX_COMPILE_OPTIMIZATION_LEVEL_3),
                VLR_DEBUG_SELECT(OPTIX_COMPILE_DEBUG_LEVEL_FULL, OPTIX_COMPILE_DEBUG_LEVEL_NONE));
            for (int i = 0; i < static_cast<int>(lengthof(commonModulePaths)); ++i) {
                p.modules[OptiXModule_ShaderNode + i] = p.pipeline.createModuleFromPTXString(
                    readTxtFile(exeDir / commonModulePaths[i]),
                    OPTIX_COMPILE_DEFAULT_MAX_REGISTER_COUNT,
                    VLR_DEBUG_SELECT(OPTIX_COMPILE_OPTIMIZATION_LEVEL_0, OPTIX_COMPILE_OPTIMIZATION_LEVEL_3),
                    VLR_DEBUG_SELECT(OPTIX_COMPILE_DEBUG_LEVEL_FULL, OPTIX_COMPILE_DEBUG_LEVEL_NONE));
            }

            p.rayGeneration = p.pipeline.createRayGenProgram(
                p.modules[OptiXModule_LightTransport], RT_RG_NAME_STR("pathTracing"));

            p.miss = p.pipeline.createMissProgram(
                p.modules[OptiXModule_LightTransport], RT_MS_NAME_STR("pathTracingMiss"));
            p.shadowMiss = p.pipeline.createMissProgram(
                optixu::Module(), nullptr);

            p.hitGroupDefault = p.pipeline.createHitProgramGroupForTriangleIS(
                p.modules[OptiXModule_LightTransport], RT_CH_NAME_STR("pathTracingIteration"),
                optixu::Module(), nullptr);
            p.hitGroupWithAlpha = p.pipeline.createHitProgramGroupForTriangleIS(
                p.modules[OptiXModule_LightTransport], RT_CH_NAME_STR("pathTracingIteration"),
                p.modules[OptiXModule_LightTransport], RT_AH_NAME_STR("pathTracingAnyHitWithAlpha"));
            p.hitGroupShadowDefault = p.pipeline.createHitProgramGroupForTriangleIS(
                optixu::Module(), nullptr,
                p.modules[OptiXModule_LightTransport], RT_AH_NAME_STR("shadowAnyHitDefault"));
            p.hitGroupShadowWithAlpha = p.pipeline.createHitProgramGroupForTriangleIS(
                optixu::Module(), nullptr,
                p.modules[OptiXModule_LightTransport], RT_AH_NAME_STR("shadowAnyHitWithAlpha"));
            p.emptyHitGroup = p.pipeline.createEmptyHitProgramGroup();

            p.pipeline.setRayGenerationProgram(p.rayGeneration);
            p.pipeline.setMissProgram(shared::PTRayType::Closest, p.miss);
            p.pipeline.setMissProgram(shared::PTRayType::Shadow, p.shadowMiss);

            for (int rIdx = 0; rIdx < shared::MaxNumRayTypes; ++rIdx) {
                m_optix.materialDefault.setHitGroup(rIdx, p.emptyHitGroup);
                m_optix.materialWithAlpha.setHitGroup(rIdx, p.emptyHitGroup);
            }
            m_optix.materialDefault.setHitGroup(shared::PTRayType::Closest, p.hitGroupDefault);
            m_optix.materialWithAlpha.setHitGroup(shared::PTRayType::Closest, p.hitGroupWithAlpha);
            m_optix.materialDefault.setHitGroup(shared::PTRayType::Shadow, p.hitGroupShadowDefault);
            m_optix.materialWithAlpha.setHitGroup(shared::PTRayType::Shadow, p.hitGroupShadowWithAlpha);
        }

        // Pipeline for Light Tracing
        {
            OptiX::LightTracing &p = m_optix.lightTracing;

            p.pipeline = m_optix.context.createPipeline();
            p.pipeline.setPipelineOptions(
                std::max(shared::LTPayloadSignature::numDwords,
                         shared::ShadowPayloadSignature::numDwords),
                static_cast<uint32_t>(optixu::calcSumDwords<float2>()),
                "plp", sizeof(shared::PipelineLaunchParameters),
                false,
                OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_LEVEL_INSTANCING,
                VLR_DEBUG_SELECT(OPTIX_EXCEPTION_FLAG_STACK_OVERFLOW |
                                 OPTIX_EXCEPTION_FLAG_TRACE_DEPTH |
                                 OPTIX_EXCEPTION_FLAG_DEBUG,
                                 OPTIX_EXCEPTION_FLAG_STACK_OVERFLOW),
                OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE);
            p.pipeline.setNumMissRayTypes(shared::LTRayType::NumTypes);

            p.modules.resize(NumOptiXModules);
            p.modules[OptiXModule_LightTransport] = p.pipeline.createModuleFromPTXString(
                readTxtFile(exeDir / "ptxes/light_tracing.ptx"),
                OPTIX_COMPILE_DEFAULT_MAX_REGISTER_COUNT,
                VLR_DEBUG_SELECT(OPTIX_COMPILE_OPTIMIZATION_LEVEL_0, OPTIX_COMPILE_OPTIMIZATION_LEVEL_3),
                VLR_DEBUG_SELECT(OPTIX_COMPILE_DEBUG_LEVEL_FULL, OPTIX_COMPILE_DEBUG_LEVEL_NONE));
            for (int i = 0; i < static_cast<int>(lengthof(commonModulePaths)); ++i) {
                p.modules[OptiXModule_ShaderNode + i] = p.pipeline.createModuleFromPTXString(
                    readTxtFile(exeDir / commonModulePaths[i]),
                    OPTIX_COMPILE_DEFAULT_MAX_REGISTER_COUNT,
                    VLR_DEBUG_SELECT(OPTIX_COMPILE_OPTIMIZATION_LEVEL_0, OPTIX_COMPILE_OPTIMIZATION_LEVEL_3),
                    VLR_DEBUG_SELECT(OPTIX_COMPILE_DEBUG_LEVEL_FULL, OPTIX_COMPILE_DEBUG_LEVEL_NONE));
            }

            p.rayGeneration = p.pipeline.createRayGenProgram(
                p.modules[OptiXModule_LightTransport], RT_RG_NAME_STR("lightTracing"));

            p.miss = p.pipeline.createMissProgram(
                optixu::Module(), nullptr);
            p.shadowMiss = p.pipeline.createMissProgram(
                optixu::Module(), nullptr);

            p.hitGroupDefault = p.pipeline.createHitProgramGroupForTriangleIS(
                p.modules[OptiXModule_LightTransport], RT_CH_NAME_STR("lightTracingIteration"),
                optixu::Module(), nullptr);
            p.hitGroupWithAlpha = p.pipeline.createHitProgramGroupForTriangleIS(
                p.modules[OptiXModule_LightTransport], RT_CH_NAME_STR("lightTracingIteration"),
                p.modules[OptiXModule_LightTransport], RT_AH_NAME_STR("lightTracingAnyHitWithAlpha"));
            p.hitGroupShadowDefault = p.pipeline.createHitProgramGroupForTriangleIS(
                optixu::Module(), nullptr,
                p.modules[OptiXModule_LightTransport], RT_AH_NAME_STR("shadowAnyHitDefault"));
            p.hitGroupShadowWithAlpha = p.pipeline.createHitProgramGroupForTriangleIS(
                optixu::Module(), nullptr,
                p.modules[OptiXModule_LightTransport], RT_AH_NAME_STR("shadowAnyHitWithAlpha"));
            p.emptyHitGroup = p.pipeline.createEmptyHitProgramGroup();

            p.pipeline.setRayGenerationProgram(p.rayGeneration);
            p.pipeline.setMissProgram(shared::LTRayType::Closest, p.miss);
            p.pipeline.setMissProgram(shared::LTRayType::Shadow, p.shadowMiss);

            for (int rIdx = 0; rIdx < shared::MaxNumRayTypes; ++rIdx) {
                m_optix.materialDefault.setHitGroup(rIdx, p.emptyHitGroup);
                m_optix.materialWithAlpha.setHitGroup(rIdx, p.emptyHitGroup);
            }
            m_optix.materialDefault.setHitGroup(shared::LTRayType::Closest, p.hitGroupDefault);
            m_optix.materialWithAlpha.setHitGroup(shared::LTRayType::Closest, p.hitGroupWithAlpha);
            m_optix.materialDefault.setHitGroup(shared::LTRayType::Shadow, p.hitGroupShadowDefault);
            m_optix.materialWithAlpha.setHitGroup(shared::LTRayType::Shadow, p.hitGroupShadowWithAlpha);
        }

        // Pipeline for LVC-BPT
        {
            OptiX::LVCBPT &p = m_optix.lvcbpt;

            p.pipeline = m_optix.context.createPipeline();
            p.pipeline.setPipelineOptions(
                std::max({
                    shared::LVCBPTLightPathPayloadSignature::numDwords,
                    shared::LVCBPTEyePathPayloadSignature::numDwords,
                    shared::ShadowPayloadSignature::numDwords
                         }),
                static_cast<uint32_t>(optixu::calcSumDwords<float2>()),
                "plp", sizeof(shared::PipelineLaunchParameters),
                false,
                OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_LEVEL_INSTANCING,
                VLR_DEBUG_SELECT(OPTIX_EXCEPTION_FLAG_STACK_OVERFLOW |
                                 OPTIX_EXCEPTION_FLAG_TRACE_DEPTH |
                                 OPTIX_EXCEPTION_FLAG_DEBUG,
                                 OPTIX_EXCEPTION_FLAG_STACK_OVERFLOW),
                OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE);
            p.pipeline.setNumMissRayTypes(shared::LVCBPTRayType::NumTypes);

            p.modules.resize(NumOptiXModules);
            p.modules[OptiXModule_LightTransport] = p.pipeline.createModuleFromPTXString(
                readTxtFile(exeDir / "ptxes/lvc_bpt.ptx"),
                OPTIX_COMPILE_DEFAULT_MAX_REGISTER_COUNT,
                VLR_DEBUG_SELECT(OPTIX_COMPILE_OPTIMIZATION_LEVEL_0, OPTIX_COMPILE_OPTIMIZATION_LEVEL_3),
                VLR_DEBUG_SELECT(OPTIX_COMPILE_DEBUG_LEVEL_FULL, OPTIX_COMPILE_DEBUG_LEVEL_NONE));
            for (int i = 0; i < static_cast<int>(lengthof(commonModulePaths)); ++i) {
                p.modules[OptiXModule_ShaderNode + i] = p.pipeline.createModuleFromPTXString(
                    readTxtFile(exeDir / commonModulePaths[i]),
                    OPTIX_COMPILE_DEFAULT_MAX_REGISTER_COUNT,
                    VLR_DEBUG_SELECT(OPTIX_COMPILE_OPTIMIZATION_LEVEL_0, OPTIX_COMPILE_OPTIMIZATION_LEVEL_3),
                    VLR_DEBUG_SELECT(OPTIX_COMPILE_DEBUG_LEVEL_FULL, OPTIX_COMPILE_DEBUG_LEVEL_NONE));
            }

            p.lightPathRayGen = p.pipeline.createRayGenProgram(
                p.modules[OptiXModule_LightTransport], RT_RG_NAME_STR("lvcbptLightPath"));
            p.eyePathRayGen = p.pipeline.createRayGenProgram(
                p.modules[OptiXModule_LightTransport], RT_RG_NAME_STR("lvcbptEyePath"));

            p.lightPathMiss = p.pipeline.createMissProgram(
                optixu::Module(), nullptr);
            p.eyePathMiss = p.pipeline.createMissProgram(
                p.modules[OptiXModule_LightTransport], RT_MS_NAME_STR("lvcbptEyePath"));
            p.connectionMiss = p.pipeline.createMissProgram(
                optixu::Module(), nullptr);

            p.lightPathHitGroupDefault = p.pipeline.createHitProgramGroupForTriangleIS(
                p.modules[OptiXModule_LightTransport], RT_CH_NAME_STR("lvcbptLightPath"),
                optixu::Module(), nullptr);
            p.lightPathHitGroupWithAlpha = p.pipeline.createHitProgramGroupForTriangleIS(
                p.modules[OptiXModule_LightTransport], RT_CH_NAME_STR("lvcbptLightPath"),
                p.modules[OptiXModule_LightTransport], RT_AH_NAME_STR("lvcbptAnyHitWithAlpha"));
            p.eyePathHitGroupDefault = p.pipeline.createHitProgramGroupForTriangleIS(
                p.modules[OptiXModule_LightTransport], RT_CH_NAME_STR("lvcbptEyePath"),
                optixu::Module(), nullptr);
            p.eyePathHitGroupWithAlpha = p.pipeline.createHitProgramGroupForTriangleIS(
                p.modules[OptiXModule_LightTransport], RT_CH_NAME_STR("lvcbptEyePath"),
                p.modules[OptiXModule_LightTransport], RT_AH_NAME_STR("lvcbptAnyHitWithAlpha"));
            p.connectionHitGroupDefault = p.pipeline.createHitProgramGroupForTriangleIS(
                optixu::Module(), nullptr,
                p.modules[OptiXModule_LightTransport], RT_AH_NAME_STR("shadowAnyHitDefault"));
            p.connectionHitGroupWithAlpha = p.pipeline.createHitProgramGroupForTriangleIS(
                optixu::Module(), nullptr,
                p.modules[OptiXModule_LightTransport], RT_AH_NAME_STR("shadowAnyHitWithAlpha"));
            p.emptyHitGroup = p.pipeline.createEmptyHitProgramGroup();

            p.pipeline.setMissProgram(shared::LVCBPTRayType::LightPath, p.lightPathMiss);
            p.pipeline.setMissProgram(shared::LVCBPTRayType::EyePath, p.eyePathMiss);
            p.pipeline.setMissProgram(shared::LVCBPTRayType::Connection, p.connectionMiss);

            for (int rIdx = 0; rIdx < shared::MaxNumRayTypes; ++rIdx) {
                m_optix.materialDefault.setHitGroup(rIdx, p.emptyHitGroup);
                m_optix.materialWithAlpha.setHitGroup(rIdx, p.emptyHitGroup);
            }
            m_optix.materialDefault.setHitGroup(shared::LVCBPTRayType::LightPath, p.lightPathHitGroupDefault);
            m_optix.materialWithAlpha.setHitGroup(shared::LVCBPTRayType::LightPath, p.lightPathHitGroupWithAlpha);
            m_optix.materialDefault.setHitGroup(shared::LVCBPTRayType::EyePath, p.eyePathHitGroupDefault);
            m_optix.materialWithAlpha.setHitGroup(shared::LVCBPTRayType::EyePath, p.eyePathHitGroupWithAlpha);
            m_optix.materialDefault.setHitGroup(shared::LVCBPTRayType::Connection, p.connectionHitGroupDefault);
            m_optix.materialWithAlpha.setHitGroup(shared::LVCBPTRayType::Connection, p.connectionHitGroupWithAlpha);

            p.rng = std::mt19937(1731230721);
        }

        // Pipeline for Aux Buffer Generator
        {
            OptiX::AuxBufferGenerator &p = m_optix.auxBufferGenerator;

            p.pipeline = m_optix.context.createPipeline();
            p.pipeline.setPipelineOptions(
                shared::AuxBufGenPayloadSignature::numDwords,
                static_cast<uint32_t>(optixu::calcSumDwords<float2>()),
                "plp", sizeof(shared::PipelineLaunchParameters),
                false,
                OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_LEVEL_INSTANCING,
                VLR_DEBUG_SELECT(OPTIX_EXCEPTION_FLAG_STACK_OVERFLOW |
                                 OPTIX_EXCEPTION_FLAG_TRACE_DEPTH |
                                 OPTIX_EXCEPTION_FLAG_DEBUG,
                                 OPTIX_EXCEPTION_FLAG_STACK_OVERFLOW),
                OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE);
            p.pipeline.setNumMissRayTypes(shared::AuxBufGenRayType::NumTypes);

            p.modules.resize(NumOptiXModules);
            p.modules[OptiXModule_LightTransport] = p.pipeline.createModuleFromPTXString(
                readTxtFile(exeDir / "ptxes/aux_buffer_generator.ptx"),
                OPTIX_COMPILE_DEFAULT_MAX_REGISTER_COUNT,
                VLR_DEBUG_SELECT(OPTIX_COMPILE_OPTIMIZATION_LEVEL_0, OPTIX_COMPILE_OPTIMIZATION_LEVEL_3),
                VLR_DEBUG_SELECT(OPTIX_COMPILE_DEBUG_LEVEL_FULL, OPTIX_COMPILE_DEBUG_LEVEL_NONE));
            for (int i = 0; i < static_cast<int>(lengthof(commonModulePaths)); ++i) {
                p.modules[OptiXModule_ShaderNode + i] = p.pipeline.createModuleFromPTXString(
                    readTxtFile(exeDir / commonModulePaths[i]),
                    OPTIX_COMPILE_DEFAULT_MAX_REGISTER_COUNT,
                    VLR_DEBUG_SELECT(OPTIX_COMPILE_OPTIMIZATION_LEVEL_0, OPTIX_COMPILE_OPTIMIZATION_LEVEL_3),
                    VLR_DEBUG_SELECT(OPTIX_COMPILE_DEBUG_LEVEL_FULL, OPTIX_COMPILE_DEBUG_LEVEL_NONE));
            }

            p.emptyHitGroup = p.pipeline.createEmptyHitProgramGroup();

            p.rayGeneration = p.pipeline.createRayGenProgram(
                p.modules[OptiXModule_LightTransport], RT_RG_NAME_STR("auxBufferGenerator"));

            p.miss = p.pipeline.createMissProgram(
                p.modules[OptiXModule_LightTransport], RT_MS_NAME_STR("auxBufferGeneratorMiss"));

            p.hitGroupDefault = p.pipeline.createHitProgramGroupForTriangleIS(
                p.modules[OptiXModule_LightTransport], RT_CH_NAME_STR("auxBufferGeneratorFirstHit"),
                optixu::Module(), nullptr);
            p.hitGroupWithAlpha = p.pipeline.createHitProgramGroupForTriangleIS(
                p.modules[OptiXModule_LightTransport], RT_CH_NAME_STR("auxBufferGeneratorFirstHit"),
                p.modules[OptiXModule_LightTransport], RT_AH_NAME_STR("auxBufferGeneratorAnyHitWithAlpha"));

            p.pipeline.setRayGenerationProgram(p.rayGeneration);
            p.pipeline.setMissProgram(shared::AuxBufGenRayType::Primary, p.miss);

            for (int rIdx = 0; rIdx < shared::MaxNumRayTypes; ++rIdx) {
                m_optix.materialDefault.setHitGroup(rIdx, p.emptyHitGroup);
                m_optix.materialWithAlpha.setHitGroup(rIdx, p.emptyHitGroup);
            }
            m_optix.materialDefault.setHitGroup(shared::AuxBufGenRayType::Primary, p.hitGroupDefault);
            m_optix.materialWithAlpha.setHitGroup(shared::AuxBufGenRayType::Primary, p.hitGroupWithAlpha);
        }

        // Pipeline for Debug Rendering
        {
            OptiX::DebugRendering &p = m_optix.debugRendering;

            p.pipeline = m_optix.context.createPipeline();
            p.pipeline.setPipelineOptions(
                shared::DebugPayloadSignature::numDwords,
                static_cast<uint32_t>(optixu::calcSumDwords<float2>()),
                "plp", sizeof(shared::PipelineLaunchParameters),
                false,
                OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_LEVEL_INSTANCING,
                VLR_DEBUG_SELECT(OPTIX_EXCEPTION_FLAG_STACK_OVERFLOW |
                                 OPTIX_EXCEPTION_FLAG_TRACE_DEPTH |
                                 OPTIX_EXCEPTION_FLAG_DEBUG,
                                 OPTIX_EXCEPTION_FLAG_STACK_OVERFLOW),
                OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE);
            p.pipeline.setNumMissRayTypes(shared::DebugRayType::NumTypes);

            p.modules.resize(NumOptiXModules);
            p.modules[OptiXModule_LightTransport] = p.pipeline.createModuleFromPTXString(
                readTxtFile(exeDir / "ptxes/debug_rendering.ptx"),
                OPTIX_COMPILE_DEFAULT_MAX_REGISTER_COUNT,
                VLR_DEBUG_SELECT(OPTIX_COMPILE_OPTIMIZATION_LEVEL_0, OPTIX_COMPILE_OPTIMIZATION_LEVEL_3),
                VLR_DEBUG_SELECT(OPTIX_COMPILE_DEBUG_LEVEL_FULL, OPTIX_COMPILE_DEBUG_LEVEL_NONE));
            for (int i = 0; i < static_cast<int>(lengthof(commonModulePaths)); ++i) {
                p.modules[OptiXModule_ShaderNode + i] = p.pipeline.createModuleFromPTXString(
                    readTxtFile(exeDir / commonModulePaths[i]),
                    OPTIX_COMPILE_DEFAULT_MAX_REGISTER_COUNT,
                    VLR_DEBUG_SELECT(OPTIX_COMPILE_OPTIMIZATION_LEVEL_0, OPTIX_COMPILE_OPTIMIZATION_LEVEL_3),
                    VLR_DEBUG_SELECT(OPTIX_COMPILE_DEBUG_LEVEL_FULL, OPTIX_COMPILE_DEBUG_LEVEL_NONE));
            }

            p.emptyHitGroup = p.pipeline.createEmptyHitProgramGroup();

            p.rayGeneration = p.pipeline.createRayGenProgram(
                p.modules[OptiXModule_LightTransport], RT_RG_NAME_STR("debugRenderingRayGeneration"));

            p.miss = p.pipeline.createMissProgram(
                p.modules[OptiXModule_LightTransport], RT_MS_NAME_STR("debugRenderingMiss"));

            p.hitGroupDefault = p.pipeline.createHitProgramGroupForTriangleIS(
                p.modules[OptiXModule_LightTransport], RT_CH_NAME_STR("debugRenderingClosestHit"),
                optixu::Module(), nullptr);
            p.hitGroupWithAlpha = p.pipeline.createHitProgramGroupForTriangleIS(
                p.modules[OptiXModule_LightTransport], RT_CH_NAME_STR("debugRenderingClosestHit"),
                p.modules[OptiXModule_LightTransport], RT_AH_NAME_STR("debugRenderingAnyHitWithAlpha"));

            p.pipeline.setRayGenerationProgram(p.rayGeneration);
            p.pipeline.setMissProgram(shared::DebugRayType::Primary, p.miss);

            for (int rIdx = 0; rIdx < shared::MaxNumRayTypes; ++rIdx) {
                m_optix.materialDefault.setHitGroup(rIdx, p.emptyHitGroup);
                m_optix.materialWithAlpha.setHitGroup(rIdx, p.emptyHitGroup);
            }
            m_optix.materialDefault.setHitGroup(shared::DebugRayType::Primary, p.hitGroupDefault);
            m_optix.materialWithAlpha.setHitGroup(shared::DebugRayType::Primary, p.hitGroupWithAlpha);
        }



        // Null BSDF/EDF
        {
            m_optix.dcNullBSDF_setupBSDF = createDirectCallableProgram(
                OptiXModule_Material, RT_DC_NAME_STR("NullBSDF_setupBSDF"));

            m_optix.dcNullBSDF_getBaseColor = createDirectCallableProgram(
                OptiXModule_Material, RT_DC_NAME_STR("NullBSDF_getBaseColor"));
            m_optix.dcNullBSDF_matches = createDirectCallableProgram(
                OptiXModule_Material, RT_DC_NAME_STR("NullBSDF_matches"));
            m_optix.dcNullBSDF_sampleInternal = createDirectCallableProgram(
                OptiXModule_Material, RT_DC_NAME_STR("NullBSDF_sampleInternal"));
            m_optix.dcNullBSDF_sampleWithRevInternal = createDirectCallableProgram(
                OptiXModule_Material, RT_DC_NAME_STR("NullBSDF_sampleWithRevInternal"));
            m_optix.dcNullBSDF_evaluateInternal = createDirectCallableProgram(
                OptiXModule_Material, RT_DC_NAME_STR("NullBSDF_evaluateInternal"));
            m_optix.dcNullBSDF_evaluateWithRevInternal = createDirectCallableProgram(
                OptiXModule_Material, RT_DC_NAME_STR("NullBSDF_evaluateWithRevInternal"));
            m_optix.dcNullBSDF_evaluatePDFInternal = createDirectCallableProgram(
                OptiXModule_Material, RT_DC_NAME_STR("NullBSDF_evaluatePDFInternal"));
            m_optix.dcNullBSDF_evaluatePDFWithRevInternal = createDirectCallableProgram(
                OptiXModule_Material, RT_DC_NAME_STR("NullBSDF_evaluatePDFWithRevInternal"));
            m_optix.dcNullBSDF_weightInternal = createDirectCallableProgram(
                OptiXModule_Material, RT_DC_NAME_STR("NullBSDF_weightInternal"));

            shared::BSDFProcedureSet bsdfProcSet;
            {
                bsdfProcSet.progGetBaseColor = m_optix.dcNullBSDF_getBaseColor;
                bsdfProcSet.progMatches = m_optix.dcNullBSDF_matches;
                bsdfProcSet.progSampleInternal = m_optix.dcNullBSDF_sampleInternal;
                bsdfProcSet.progSampleWithRevInternal = m_optix.dcNullBSDF_sampleWithRevInternal;
                bsdfProcSet.progEvaluateInternal = m_optix.dcNullBSDF_evaluateInternal;
                bsdfProcSet.progEvaluateWithRevInternal = m_optix.dcNullBSDF_evaluateWithRevInternal;
                bsdfProcSet.progEvaluatePDFInternal = m_optix.dcNullBSDF_evaluatePDFInternal;
                bsdfProcSet.progEvaluatePDFWithRevInternal = m_optix.dcNullBSDF_evaluatePDFWithRevInternal;
                bsdfProcSet.progWeightInternal = m_optix.dcNullBSDF_weightInternal;
            }
            m_optix.nullBSDFProcedureSetIndex = allocateBSDFProcedureSet();
            updateBSDFProcedureSet(m_optix.nullBSDFProcedureSetIndex, bsdfProcSet, 0);
            VLRAssert(m_optix.nullBSDFProcedureSetIndex == 0,
                      "Index of the null BSDF procedure set is expected to be 0.");

            m_optix.dcNullEDF_setupEDF = createDirectCallableProgram(
                OptiXModule_Material, RT_DC_NAME_STR("NullEDF_setupEDF"));

            m_optix.dcNullEDF_matches = createDirectCallableProgram(
                OptiXModule_Material, RT_DC_NAME_STR("NullEDF_matches"));
            m_optix.dcNullEDF_sampleInternal = createDirectCallableProgram(
                OptiXModule_Material, RT_DC_NAME_STR("NullEDF_sampleInternal"));
            m_optix.dcNullEDF_evaluateEmittanceInternal = createDirectCallableProgram(
                OptiXModule_Material, RT_DC_NAME_STR("NullEDF_evaluateEmittanceInternal"));
            m_optix.dcNullEDF_evaluateInternal = createDirectCallableProgram(
                OptiXModule_Material, RT_DC_NAME_STR("NullEDF_evaluateInternal"));
            m_optix.dcNullEDF_evaluatePDFInternal = createDirectCallableProgram(
                OptiXModule_Material, RT_DC_NAME_STR("NullEDF_evaluatePDFInternal"));
            m_optix.dcNullEDF_weightInternal = createDirectCallableProgram(
                OptiXModule_Material, RT_DC_NAME_STR("NullEDF_weightInternal"));

            m_optix.dcNullEDF_as_BSDF_getBaseColor = createDirectCallableProgram(
                OptiXModule_Material, RT_DC_NAME_STR("NullEDF_as_BSDF_getBaseColor"));
            m_optix.dcNullEDF_as_BSDF_matches = createDirectCallableProgram(
                OptiXModule_Material, RT_DC_NAME_STR("NullEDF_as_BSDF_matches"));
            m_optix.dcNullEDF_as_BSDF_sampleInternal = createDirectCallableProgram(
                OptiXModule_Material, RT_DC_NAME_STR("NullEDF_as_BSDF_sampleInternal"));
            m_optix.dcNullEDF_as_BSDF_sampleWithRevInternal = createDirectCallableProgram(
                OptiXModule_Material, RT_DC_NAME_STR("NullEDF_as_BSDF_sampleWithRevInternal"));
            m_optix.dcNullEDF_as_BSDF_evaluateInternal = createDirectCallableProgram(
                OptiXModule_Material, RT_DC_NAME_STR("NullEDF_as_BSDF_evaluateInternal"));
            m_optix.dcNullEDF_as_BSDF_evaluateWithRevInternal = createDirectCallableProgram(
                OptiXModule_Material, RT_DC_NAME_STR("NullEDF_as_BSDF_evaluateWithRevInternal"));
            m_optix.dcNullEDF_as_BSDF_evaluatePDFInternal = createDirectCallableProgram(
                OptiXModule_Material, RT_DC_NAME_STR("NullEDF_as_BSDF_evaluatePDFInternal"));
            m_optix.dcNullEDF_as_BSDF_evaluatePDFWithRevInternal = createDirectCallableProgram(
                OptiXModule_Material, RT_DC_NAME_STR("NullEDF_as_BSDF_evaluatePDFWithRevInternal"));
            m_optix.dcNullEDF_as_BSDF_weightInternal = createDirectCallableProgram(
                OptiXModule_Material, RT_DC_NAME_STR("NullEDF_as_BSDF_weightInternal"));

            shared::EDFProcedureSet edfProcSet;
            {
                edfProcSet.progMatches = m_optix.dcNullEDF_matches;
                edfProcSet.progSampleInternal = m_optix.dcNullEDF_sampleInternal;
                edfProcSet.progEvaluateEmittanceInternal = m_optix.dcNullEDF_evaluateEmittanceInternal;
                edfProcSet.progEvaluateInternal = m_optix.dcNullEDF_evaluateInternal;
                edfProcSet.progEvaluatePDFInternal = m_optix.dcNullEDF_evaluatePDFInternal;
                edfProcSet.progWeightInternal = m_optix.dcNullEDF_weightInternal;

                edfProcSet.progGetBaseColorAsBSDF = m_optix.dcNullEDF_as_BSDF_getBaseColor;
                edfProcSet.progMatchesAsBSDF = m_optix.dcNullEDF_as_BSDF_matches;
                edfProcSet.progSampleInternalAsBSDF = m_optix.dcNullEDF_as_BSDF_sampleInternal;
                edfProcSet.progSampleWithRevInternalAsBSDF = m_optix.dcNullEDF_as_BSDF_sampleWithRevInternal;
                edfProcSet.progEvaluateInternalAsBSDF = m_optix.dcNullEDF_as_BSDF_evaluateInternal;
                edfProcSet.progEvaluateWithRevInternalAsBSDF = m_optix.dcNullEDF_as_BSDF_evaluateWithRevInternal;
                edfProcSet.progEvaluatePDFInternalAsBSDF = m_optix.dcNullEDF_as_BSDF_evaluatePDFInternal;
                edfProcSet.progEvaluatePDFWithRevInternalAsBSDF = m_optix.dcNullEDF_as_BSDF_evaluatePDFWithRevInternal;
                edfProcSet.progWeightInternalAsBSDF = m_optix.dcNullEDF_as_BSDF_weightInternal;
            }
            m_optix.nullEDFProcedureSetIndex = allocateEDFProcedureSet();
            updateEDFProcedureSet(m_optix.nullEDFProcedureSetIndex, edfProcSet, 0);
            VLRAssert(m_optix.nullEDFProcedureSetIndex == 0,
                      "Index of the null EDF procedure set is expected to be 0.");
        }



        m_optix.launchParams.DiscretizedSpectrum_xbar = DiscretizedSpectrumAlwaysSpectral::xbar;
        m_optix.launchParams.DiscretizedSpectrum_ybar = DiscretizedSpectrumAlwaysSpectral::ybar;
        m_optix.launchParams.DiscretizedSpectrum_zbar = DiscretizedSpectrumAlwaysSpectral::zbar;
        m_optix.launchParams.DiscretizedSpectrum_integralCMF = DiscretizedSpectrumAlwaysSpectral::integralCMF;

#if SPECTRAL_UPSAMPLING_METHOD == MENG_SPECTRAL_UPSAMPLING
        constexpr uint32_t NumSpectrumGridCells = 168;
        constexpr uint32_t NumSpectrumDataPoints = 186;
        m_optix.UpsampledSpectrum_spectrum_grid.initialize(
            m_cuContext, g_bufferType,
            UpsampledSpectrum::spectrum_grid, NumSpectrumGridCells);
        m_optix.UpsampledSpectrum_spectrum_data_points.initialize(
            m_cuContext, g_bufferType,
            UpsampledSpectrum::spectrum_data_points, NumSpectrumDataPoints);
        m_optix.launchParams.UpsampledSpectrum_spectrum_grid = m_optix.UpsampledSpectrum_spectrum_grid.getDevicePointer();
        m_optix.launchParams.UpsampledSpectrum_spectrum_data_points = m_optix.UpsampledSpectrum_spectrum_data_points.getDevicePointer();
#elif SPECTRAL_UPSAMPLING_METHOD == JAKOB_SPECTRAL_UPSAMPLING
        m_optix.UpsampledSpectrum_maxBrightnesses.initialize(
            m_cuContext, g_bufferType,
            UpsampledSpectrum::maxBrightnesses, UpsampledSpectrum::kTableResolution);
        m_optix.UpsampledSpectrum_coefficients_sRGB_D65.initialize(
            m_cuContext, g_bufferType,
            UpsampledSpectrum::coefficients_sRGB_D65, 3 * pow3(UpsampledSpectrum::kTableResolution));
        m_optix.UpsampledSpectrum_coefficients_sRGB_E.initialize(
            m_cuContext, g_bufferType,
            UpsampledSpectrum::coefficients_sRGB_E, 3 * pow3(UpsampledSpectrum::kTableResolution));
        m_optix.launchParams.UpsampledSpectrum_maxBrightnesses = m_optix.UpsampledSpectrum_maxBrightnesses.getDevicePointer();
        m_optix.launchParams.UpsampledSpectrum_coefficients_sRGB_D65 = m_optix.UpsampledSpectrum_coefficients_sRGB_D65.getDevicePointer();
        m_optix.launchParams.UpsampledSpectrum_coefficients_sRGB_E = m_optix.UpsampledSpectrum_coefficients_sRGB_E.getDevicePointer();
#endif

        CUDADRV_CHECK(cuMemAlloc(&m_optix.launchParamsOnDevice, sizeof(shared::PipelineLaunchParameters)));



        m_optix.linearRngBuffer.initialize(m_cuContext, g_bufferType, OptiX::numLightPaths);
        {
            auto rngs = m_optix.linearRngBuffer.map();
            std::mt19937_64 seedRng(459182033132123413);
            for (uint32_t i = 0; i < OptiX::numLightPaths; ++i)
                rngs[i] = shared::KernelRNG(seedRng());
            m_optix.linearRngBuffer.unmap();
        }
        m_optix.launchParams.linearRngBuffer = m_optix.linearRngBuffer.getDevicePointer();

        m_optix.lightVertexCache.initialize(m_cuContext, g_bufferType, OptiX::numLightPaths * 10);
        m_optix.launchParams.lightVertexCache = m_optix.lightVertexCache.getDevicePointer();
        CUDADRV_CHECK(cuMemAlloc(&m_optix.numLightVertices, sizeof(shared::LightPathVertex)));
        m_optix.launchParams.numLightVertices = reinterpret_cast<uint32_t*>(m_optix.numLightVertices);



        m_optix.denoiser = m_optix.context.createDenoiser(
            OPTIX_DENOISER_MODEL_KIND_HDR, true, true);
        CUDADRV_CHECK(cuMemAlloc(&m_optix.hdrIntensity, sizeof(float)));



        {
            CUDADRV_CHECK(cuModuleLoad(&m_cudaSetupSceneModule, (exeDir / "ptxes/setup_scene.ptx").string().c_str()));

            m_computeInstanceAABBs.set(m_cudaSetupSceneModule, "computeInstanceAABBs", cudau::dim3(32), 0);
            m_finalizeInstanceAABBs.set(m_cudaSetupSceneModule, "finalizeInstanceAABBs", cudau::dim3(32), 0);
            m_computeSceneAABB.set(m_cudaSetupSceneModule, "computeSceneAABB", cudau::dim3(256), 0);
            m_finalizeSceneBounds.set(m_cudaSetupSceneModule, "finalizeSceneBounds", cudau::dim3(32), 0);
        }
        
        {
            CUDADRV_CHECK(cuModuleLoad(&m_cudaPostProcessModule, (exeDir / "ptxes/post_process.ptx").string().c_str()));

            size_t symbolSize;
            CUDADRV_CHECK(cuModuleGetGlobal(&m_cudaPostProcessModuleLaunchParamsPtr, &symbolSize,
                                            m_cudaPostProcessModule, "plp"));

            m_resetAtomicAccumBuffer.set(m_cudaPostProcessModule, "resetAtomicAccumBuffer", cudau::dim3(8, 8), 0);
            m_accumulateFromAtomicAccumBuffer.set(m_cudaPostProcessModule, "accumulateFromAtomicAccumBuffer", cudau::dim3(8, 8), 0);
            m_copyBuffers.set(m_cudaPostProcessModule, "copyBuffers", cudau::dim3(32), 0);
            m_convertToRGB.set(m_cudaPostProcessModule, "convertToRGB", cudau::dim3(32), 0);
        }



        Image2D::initialize(*this);
        ShaderNode::initialize(*this);
        SurfaceMaterial::initialize(*this);
        SurfaceNode::initialize(*this);
        Camera::initialize(*this);
        Scene::initialize(*this);

        // Pipeline for Path Tracing
        {
            OptiX::PathTracing &p = m_optix.pathTracing;

            p.pipeline.link(2, VLR_DEBUG_SELECT(OPTIX_COMPILE_DEBUG_LEVEL_FULL, OPTIX_COMPILE_DEBUG_LEVEL_NONE));
            p.pipeline.setNumCallablePrograms(static_cast<uint32_t>(p.callablePrograms.size()));
            for (int i = 0; i < p.callablePrograms.size(); ++i)
                p.pipeline.setCallableProgram(i, p.callablePrograms[i]);

            size_t sbtSize;
            p.pipeline.generateShaderBindingTableLayout(&sbtSize);
            p.shaderBindingTable.initialize(m_cuContext, g_bufferType, static_cast<uint32_t>(sbtSize), 1);
            p.shaderBindingTable.setMappedMemoryPersistent(true);
            p.pipeline.setShaderBindingTable(p.shaderBindingTable,
                                             p.shaderBindingTable.getMappedPointer());
        }

        // Pipeline for Light Tracing
        {
            OptiX::LightTracing &p = m_optix.lightTracing;

            p.pipeline.link(2, VLR_DEBUG_SELECT(OPTIX_COMPILE_DEBUG_LEVEL_FULL, OPTIX_COMPILE_DEBUG_LEVEL_NONE));
            p.pipeline.setNumCallablePrograms(static_cast<uint32_t>(p.callablePrograms.size()));
            for (int i = 0; i < p.callablePrograms.size(); ++i)
                p.pipeline.setCallableProgram(i, p.callablePrograms[i]);

            size_t sbtSize;
            p.pipeline.generateShaderBindingTableLayout(&sbtSize);
            p.shaderBindingTable.initialize(m_cuContext, g_bufferType, static_cast<uint32_t>(sbtSize), 1);
            p.shaderBindingTable.setMappedMemoryPersistent(true);
            p.pipeline.setShaderBindingTable(p.shaderBindingTable,
                                             p.shaderBindingTable.getMappedPointer());
        }

        // Pipeline for LVC-BPT
        {
            OptiX::LVCBPT &p = m_optix.lvcbpt;

            p.pipeline.link(2, VLR_DEBUG_SELECT(OPTIX_COMPILE_DEBUG_LEVEL_FULL, OPTIX_COMPILE_DEBUG_LEVEL_NONE));
            p.pipeline.setNumCallablePrograms(static_cast<uint32_t>(p.callablePrograms.size()));
            for (int i = 0; i < p.callablePrograms.size(); ++i)
                p.pipeline.setCallableProgram(i, p.callablePrograms[i]);

            size_t sbtSize;
            p.pipeline.generateShaderBindingTableLayout(&sbtSize);
            p.shaderBindingTable.initialize(m_cuContext, g_bufferType, static_cast<uint32_t>(sbtSize), 1);
            p.shaderBindingTable.setMappedMemoryPersistent(true);
            p.pipeline.setShaderBindingTable(p.shaderBindingTable,
                                             p.shaderBindingTable.getMappedPointer());
        }

        // Pipeline for Aux Buffer Generator
        {
            OptiX::AuxBufferGenerator &p = m_optix.auxBufferGenerator;

            p.pipeline.link(1, VLR_DEBUG_SELECT(OPTIX_COMPILE_DEBUG_LEVEL_FULL, OPTIX_COMPILE_DEBUG_LEVEL_NONE));
            p.pipeline.setNumCallablePrograms(static_cast<uint32_t>(p.callablePrograms.size()));
            for (int i = 0; i < p.callablePrograms.size(); ++i)
                p.pipeline.setCallableProgram(i, p.callablePrograms[i]);

            size_t sbtSize;
            p.pipeline.generateShaderBindingTableLayout(&sbtSize);
            p.shaderBindingTable.initialize(m_cuContext, g_bufferType, static_cast<uint32_t>(sbtSize), 1);
            p.shaderBindingTable.setMappedMemoryPersistent(true);
            p.pipeline.setShaderBindingTable(p.shaderBindingTable,
                                             p.shaderBindingTable.getMappedPointer());
        }

        // Pipeline for Debug Rendering
        {
            OptiX::DebugRendering &p = m_optix.debugRendering;

            p.pipeline.link(1, VLR_DEBUG_SELECT(OPTIX_COMPILE_DEBUG_LEVEL_FULL, OPTIX_COMPILE_DEBUG_LEVEL_NONE));
            p.pipeline.setNumCallablePrograms(static_cast<uint32_t>(p.callablePrograms.size()));
            for (int i = 0; i < p.callablePrograms.size(); ++i)
                p.pipeline.setCallableProgram(i, p.callablePrograms[i]);

            size_t sbtSize;
            p.pipeline.generateShaderBindingTableLayout(&sbtSize);
            p.shaderBindingTable.initialize(m_cuContext, g_bufferType, static_cast<uint32_t>(sbtSize), 1);
            p.shaderBindingTable.setMappedMemoryPersistent(true);
            p.pipeline.setShaderBindingTable(p.shaderBindingTable,
                                             p.shaderBindingTable.getMappedPointer());
        }

        m_renderer = VLRRenderer_PathTracing;
        m_debugRenderingAttribute = VLRDebugRenderingMode_BaseColor;

        vlrprintf(" done.\n");
    }

    Context::~Context() {
        m_optix.asScratchMem.finalize();

        m_optix.linearDenoisedColorBuffer.finalize();
        m_optix.linearNormalBuffer.finalize();
        m_optix.linearAlbedoBuffer.finalize();
        m_optix.linearColorBuffer.finalize();
        m_optix.accumNormalBuffer.finalize();
        m_optix.accumAlbedoBuffer.finalize();
        m_optix.denoiserScratchBuffer.finalize();
        m_optix.denoiserStateBuffer.finalize();

        m_optix.outputBufferHolder.finalize();
        m_optix.outputBuffer.finalize();
        m_optix.atomicAccumBuffer.finalize();
        m_optix.accumBuffer.finalize();
        m_optix.rngBuffer.finalize();



        // Pipeline for Debug Rendering
        {
            OptiX::DebugRendering &p = m_optix.debugRendering;
            p.shaderBindingTable.finalize();
        }

        // Pipeline for Aux Buffer Generator
        {
            OptiX::AuxBufferGenerator &p = m_optix.auxBufferGenerator;
            p.shaderBindingTable.finalize();
        }

        // Pipeline for LVC-BPT
        {
            OptiX::LVCBPT &p = m_optix.lvcbpt;
            p.shaderBindingTable.finalize();
        }

        // Pipeline for Light Tracing
        {
            OptiX::LightTracing &p = m_optix.lightTracing;
            p.shaderBindingTable.finalize();
        }

        // Pipeline for Path Tracing
        {
            OptiX::PathTracing &p = m_optix.pathTracing;
            p.shaderBindingTable.finalize();
        }

        Scene::finalize(*this);
        Camera::finalize(*this);
        SurfaceNode::finalize(*this);
        SurfaceMaterial::finalize(*this);
        ShaderNode::finalize(*this);
        Image2D::finalize(*this);



        cuModuleUnload(m_cudaPostProcessModule);
        cuModuleUnload(m_cudaSetupSceneModule);



        cuMemFree(m_optix.hdrIntensity);
        m_optix.denoiser.destroy();



        cuMemFree(m_optix.numLightVertices);
        m_optix.linearRngBuffer.finalize();



        cuMemFree(m_optix.launchParamsOnDevice);

        m_optix.surfaceMaterialDescriptorBuffer.finalize();
        
        m_optix.idfProcedureSetBuffer.finalize();
        m_optix.edfProcedureSetBuffer.finalize();
        m_optix.bsdfProcedureSetBuffer.finalize();
        m_optix.largeNodeDescriptorBuffer.finalize();
        m_optix.mediumNodeDescriptorBuffer.finalize();
        m_optix.smallNodeDescriptorBuffer.finalize();
        m_optix.nodeProcedureSetBuffer.finalize();

#if SPECTRAL_UPSAMPLING_METHOD == MENG_SPECTRAL_UPSAMPLING
        m_optix.UpsampledSpectrum_spectrum_data_points.finalize();
        m_optix.UpsampledSpectrum_spectrum_grid.finalize();
#elif SPECTRAL_UPSAMPLING_METHOD == JAKOB_SPECTRAL_UPSAMPLING
        m_optix.UpsampledSpectrum_coefficients_sRGB_E.finalize();
        m_optix.UpsampledSpectrum_coefficients_sRGB_D65.finalize();
        m_optix.UpsampledSpectrum_maxBrightnesses.finalize();
#endif

        m_optix.materialWithAlpha.destroy();
        m_optix.materialDefault.destroy();

        // Null BSDF/EDF
        {
            releaseEDFProcedureSet(m_optix.nullEDFProcedureSetIndex);
            destroyDirectCallableProgram(m_optix.dcNullEDF_as_BSDF_weightInternal);
            destroyDirectCallableProgram(m_optix.dcNullEDF_as_BSDF_evaluatePDFWithRevInternal);
            destroyDirectCallableProgram(m_optix.dcNullEDF_as_BSDF_evaluatePDFInternal);
            destroyDirectCallableProgram(m_optix.dcNullEDF_as_BSDF_evaluateWithRevInternal);
            destroyDirectCallableProgram(m_optix.dcNullEDF_as_BSDF_evaluateInternal);
            destroyDirectCallableProgram(m_optix.dcNullEDF_as_BSDF_sampleWithRevInternal);
            destroyDirectCallableProgram(m_optix.dcNullEDF_as_BSDF_sampleInternal);
            destroyDirectCallableProgram(m_optix.dcNullEDF_as_BSDF_matches);
            destroyDirectCallableProgram(m_optix.dcNullEDF_as_BSDF_getBaseColor);
            destroyDirectCallableProgram(m_optix.dcNullEDF_weightInternal);
            destroyDirectCallableProgram(m_optix.dcNullEDF_evaluatePDFInternal);
            destroyDirectCallableProgram(m_optix.dcNullEDF_evaluateInternal);
            destroyDirectCallableProgram(m_optix.dcNullEDF_evaluateEmittanceInternal);
            destroyDirectCallableProgram(m_optix.dcNullEDF_sampleInternal);
            destroyDirectCallableProgram(m_optix.dcNullEDF_matches);
            destroyDirectCallableProgram(m_optix.dcNullEDF_setupEDF);

            releaseBSDFProcedureSet(m_optix.nullBSDFProcedureSetIndex);
            destroyDirectCallableProgram(m_optix.dcNullBSDF_weightInternal);
            destroyDirectCallableProgram(m_optix.dcNullBSDF_evaluatePDFWithRevInternal);
            destroyDirectCallableProgram(m_optix.dcNullBSDF_evaluatePDFInternal);
            destroyDirectCallableProgram(m_optix.dcNullBSDF_evaluateWithRevInternal);
            destroyDirectCallableProgram(m_optix.dcNullBSDF_evaluateInternal);
            destroyDirectCallableProgram(m_optix.dcNullBSDF_sampleWithRevInternal);
            destroyDirectCallableProgram(m_optix.dcNullBSDF_sampleInternal);
            destroyDirectCallableProgram(m_optix.dcNullBSDF_matches);
            destroyDirectCallableProgram(m_optix.dcNullBSDF_getBaseColor);
            destroyDirectCallableProgram(m_optix.dcNullBSDF_setupBSDF);
        }

        // Pipeline for Debug Rendering
        {
            OptiX::DebugRendering &p = m_optix.debugRendering;

            p.emptyHitGroup.destroy();
            p.hitGroupWithAlpha.destroy();
            p.hitGroupDefault.destroy();
            p.miss.destroy();
            p.rayGeneration.destroy();

            for (int i = static_cast<int>(lengthof(commonModulePaths)) - 1; i >= 0; --i)
                p.modules[OptiXModule_ShaderNode + i].destroy();
            p.modules[OptiXModule_LightTransport].destroy();

            p.pipeline.destroy();
        }

        // Pipeline for Aux Buffer Generator
        {
            OptiX::AuxBufferGenerator &p = m_optix.auxBufferGenerator;

            p.emptyHitGroup.destroy();
            p.hitGroupWithAlpha.destroy();
            p.hitGroupDefault.destroy();
            p.miss.destroy();
            p.rayGeneration.destroy();

            for (int i = static_cast<int>(lengthof(commonModulePaths)) - 1; i >= 0; --i)
                p.modules[OptiXModule_ShaderNode + i].destroy();
            p.modules[OptiXModule_LightTransport].destroy();

            p.pipeline.destroy();
        }

        // Pipeline for LVC-BPT
        {
            OptiX::LVCBPT &p = m_optix.lvcbpt;

            p.emptyHitGroup.destroy();
            p.connectionHitGroupWithAlpha.destroy();
            p.connectionHitGroupDefault.destroy();
            p.eyePathHitGroupWithAlpha.destroy();
            p.eyePathHitGroupDefault.destroy();
            p.lightPathHitGroupWithAlpha.destroy();
            p.lightPathHitGroupDefault.destroy();
            p.connectionMiss.destroy();
            p.eyePathMiss.destroy();
            p.lightPathMiss.destroy();
            p.eyePathRayGen.destroy();
            p.lightPathRayGen.destroy();

            for (int i = static_cast<int>(lengthof(commonModulePaths)) - 1; i >= 0; --i)
                p.modules[OptiXModule_ShaderNode + i].destroy();
            p.modules[OptiXModule_LightTransport].destroy();

            p.pipeline.destroy();
        }

        // Pipeline for Light Tracing
        {
            OptiX::LightTracing &p = m_optix.lightTracing;

            p.emptyHitGroup.destroy();
            p.hitGroupShadowWithAlpha.destroy();
            p.hitGroupShadowDefault.destroy();
            p.hitGroupWithAlpha.destroy();
            p.hitGroupDefault.destroy();
            p.shadowMiss.destroy();
            p.miss.destroy();
            p.rayGeneration.destroy();

            for (int i = static_cast<int>(lengthof(commonModulePaths)) - 1; i >= 0; --i)
                p.modules[OptiXModule_ShaderNode + i].destroy();
            p.modules[OptiXModule_LightTransport].destroy();

            p.pipeline.destroy();
        }

        // Pipeline for Path Tracing
        {
            OptiX::PathTracing &p = m_optix.pathTracing;

            p.emptyHitGroup.destroy();
            p.hitGroupShadowWithAlpha.destroy();
            p.hitGroupShadowDefault.destroy();
            p.hitGroupWithAlpha.destroy();
            p.hitGroupDefault.destroy();
            p.shadowMiss.destroy();
            p.miss.destroy();
            p.rayGeneration.destroy();

            for (int i = static_cast<int>(lengthof(commonModulePaths)) - 1; i >= 0; --i)
                p.modules[OptiXModule_ShaderNode + i].destroy();
            p.modules[OptiXModule_LightTransport].destroy();

            p.pipeline.destroy();
        }

        m_optix.context.destroy();

        finalizeColorSystem();
    }

    void Context::bindOutputBuffer(uint32_t width, uint32_t height, uint32_t glTexID) {
        m_optix.outputBufferHolder.finalize();
        if (m_optix.outputBuffer.isInitialized())
            m_optix.outputBuffer.finalize();
        if (m_optix.accumBuffer.isInitialized())
            m_optix.accumBuffer.finalize();
        if (m_optix.rngBuffer.isInitialized())
            m_optix.rngBuffer.finalize();

        m_width = width;
        m_height = height;

        if (glTexID > 0) {
            // JP: gl3wInit()は何らかのOpenGLコンテキストが作られた後に呼ぶ必要がある。
            int32_t gl3wRet = gl3wInit();
            VLRAssert(gl3wRet == 0, "gl3wInit() failed.");
            constexpr uint32_t OpenGLMajorVersion = 3;
            constexpr uint32_t OpenGLMinorVersion = 0;
            if (!gl3wIsSupported(OpenGLMajorVersion, OpenGLMinorVersion)) {
                vlrprintf("gl3w doesn't support OpenGL %u.%u\n", OpenGLMajorVersion, OpenGLMinorVersion);
                return;
            }

            m_optix.outputBuffer.initializeFromGLTexture2D(
                m_cuContext, glTexID,
                cudau::ArraySurface::Enable, cudau::ArrayTextureGather::Disable);
            m_optix.outputBufferHolder.initialize(&m_optix.outputBuffer);
            m_optix.useGLTexture = true;
        }
        else {
            m_optix.outputBuffer.initialize2D(
                m_cuContext, cudau::ArrayElementType::Float32, 4,
                cudau::ArraySurface::Enable, cudau::ArrayTextureGather::Disable,
                m_width, m_height, 1);
            m_optix.useGLTexture = false;
        }

        m_optix.accumBuffer.initialize(m_cuContext, g_bufferType, m_width, m_height);
        m_optix.launchParams.accumBuffer = m_optix.accumBuffer.getBlockBuffer2D();

        m_optix.atomicAccumBuffer.finalize();
        m_optix.atomicAccumBuffer.initialize(m_cuContext, g_bufferType, m_width * m_height);
        m_optix.launchParams.atomicAccumBuffer = m_optix.atomicAccumBuffer.getDevicePointer();

        m_optix.launchParams.numLightPaths = OptiX::numLightPaths;

        m_optix.rngBuffer.initialize2D(
            m_cuContext, cudau::ArrayElementType::UInt32, 2,
            cudau::ArraySurface::Enable, cudau::ArrayTextureGather::Disable,
            m_width, m_height, 1);
        {
            auto rngs = m_optix.rngBuffer.map<shared::KernelRNG>();
            std::mt19937_64 seedRng(591842031321323413);
            for (int y = 0; y < static_cast<int>(m_height); ++y) {
                for (int x = 0; x < static_cast<int>(m_width); ++x) {
                    rngs[y * m_width + x] = shared::KernelRNG(seedRng());
                }
            }
            m_optix.rngBuffer.unmap();
        }
        m_optix.launchParams.rngBuffer = m_optix.rngBuffer.getSurfaceObject(0);



        if (m_optix.linearDenoisedColorBuffer.isInitialized())
            m_optix.linearDenoisedColorBuffer.finalize();
        if (m_optix.linearNormalBuffer.isInitialized())
            m_optix.linearNormalBuffer.finalize();
        if (m_optix.linearAlbedoBuffer.isInitialized())
            m_optix.linearAlbedoBuffer.finalize();
        if (m_optix.linearColorBuffer.isInitialized())
            m_optix.linearColorBuffer.finalize();
        if (m_optix.accumNormalBuffer.isInitialized())
            m_optix.accumNormalBuffer.finalize();
        if (m_optix.accumAlbedoBuffer.isInitialized())
            m_optix.accumAlbedoBuffer.finalize();
        if (m_optix.denoiserScratchBuffer.isInitialized())
            m_optix.denoiserScratchBuffer.finalize();
        if (m_optix.denoiserStateBuffer.isInitialized())
            m_optix.denoiserStateBuffer.finalize();
        
        size_t stateBufferSize;
        size_t scratchBufferSize;
        size_t scratchBufferSizeForComputeIntensity;
        uint32_t numTasks;
        m_optix.denoiser.prepare(
            m_width, m_height, 0, 0,
            &stateBufferSize, &scratchBufferSize, &scratchBufferSizeForComputeIntensity,
            &numTasks);
        m_optix.denoiserStateBuffer.initialize(
            m_cuContext, g_bufferType, static_cast<uint32_t>(stateBufferSize), 1);
        m_optix.denoiserScratchBuffer.initialize(
            m_cuContext, g_bufferType,
            static_cast<uint32_t>(std::max(scratchBufferSize, scratchBufferSizeForComputeIntensity)), 1);
        m_optix.denoiserTasks.resize(numTasks);
        m_optix.accumAlbedoBuffer.initialize(m_cuContext, g_bufferType, m_width * m_height);
        m_optix.accumNormalBuffer.initialize(m_cuContext, g_bufferType, m_width * m_height);
        m_optix.linearColorBuffer.initialize(m_cuContext, g_bufferType, m_width * m_height);
        m_optix.linearAlbedoBuffer.initialize(m_cuContext, g_bufferType, m_width * m_height);
        m_optix.linearNormalBuffer.initialize(m_cuContext, g_bufferType, m_width * m_height);
        m_optix.linearDenoisedColorBuffer.initialize(m_cuContext, g_bufferType, m_width * m_height);
        m_optix.denoiser.getTasks(m_optix.denoiserTasks.data());

        m_optix.launchParams.accumAlbedoBuffer = m_optix.accumAlbedoBuffer.getDevicePointer();
        m_optix.launchParams.accumNormalBuffer = m_optix.accumNormalBuffer.getDevicePointer();
        m_optix.launchParams.imageStrideInPixels = m_width;
    }

    void Context::getOutputBufferSize(uint32_t* width, uint32_t* height) {
        *width = m_width;
        *height = m_height;
    }

    const cudau::Array &Context::getOutputBuffer() const {
        return m_optix.outputBuffer;
    }

    void Context::readOutputBuffer(float* data) {
        auto rgbaData = reinterpret_cast<RGBA32Fx4*>(data);
        if (m_optix.useGLTexture)
            m_optix.outputBuffer.beginCUDAAccess(0, 0);
        auto mappedData = m_optix.outputBuffer.map<RGBA32Fx4>(0, 0, cudau::BufferMapFlag::ReadOnly);
        for (int y = 0; y < static_cast<int>(m_height); ++y) {
            for (int x = 0; x < static_cast<int>(m_width); ++x) {
                uint32_t idx = y * m_width + x;
                rgbaData[idx] = mappedData[idx];
            }
        }
        m_optix.outputBuffer.unmap(0);
        if (m_optix.useGLTexture)
            m_optix.outputBuffer.endCUDAAccess(0, 0);
    }

    void Context::setScene(Scene* scene) {
        m_scene = scene;
    }

    void Context::render(CUstream stream, const Camera* camera, bool denoise,
                         uint32_t shrinkCoeff, bool firstFrame,
                         uint32_t limitNumAccumFrames, uint32_t* numAccumFrames) {
        // JP: シェーダーノードのデータを転送する。
        // EN: Transfer shader node data.
        for (ShaderNode* node : m_optix.dirtyShaderNodes)
            node->setup(stream);
        m_optix.dirtyShaderNodes.clear();

        // JP: サーフェスマテリアルのデータを転送する。
        // EN: Transfer surface material data.
        for (SurfaceMaterial* surfMat : m_optix.dirtySurfaceMaterials)
            surfMat->setup(stream);
        m_optix.dirtySurfaceMaterials.clear();

        // JP: シーンのセットアップを行う。
        // EN: Setup a scene.
        size_t asScratchSize;
        optixu::Scene optixScene;
        m_scene->prepareSetup(&asScratchSize, &optixScene);
        if (!m_optix.asScratchMem.isInitialized() || m_optix.asScratchMem.sizeInBytes() < asScratchSize) {
            m_optix.asScratchMem.finalize();
            m_optix.asScratchMem.initialize(m_cuContext, g_bufferType, static_cast<uint32_t>(asScratchSize), 1);
        }
        size_t sbtSize = 0;
        bool sbtLayoutIsUpToDate = optixScene.shaderBindingTableLayoutIsReady();
        optixScene.generateShaderBindingTableLayout(&sbtSize);
        m_scene->setup(stream, m_optix.asScratchMem, &m_optix.launchParams);

        if (m_renderer == VLRRenderer_DebugRendering)
            denoise = false;

        // JP: パイプラインのヒットグループ用シェーダーバインディングテーブルをセットアップする。
        // EN: Setup the hit group shader binding table for the pipeline.
        if (m_renderer == VLRRenderer_DebugRendering) {
            if (m_debugRenderingAttribute < VLRDebugRenderingMode_DenoiserAlbedo) {
                OptiX::DebugRendering &p = m_optix.debugRendering;

                if (!p.hitGroupShaderBindingTable.isInitialized() ||
                    p.hitGroupShaderBindingTable.sizeInBytes() < sbtSize) {
                    p.hitGroupShaderBindingTable.finalize();
                    p.hitGroupShaderBindingTable.initialize(
                        m_cuContext, g_bufferType, static_cast<uint32_t>(sbtSize), 1);
                    p.hitGroupShaderBindingTable.setMappedMemoryPersistent(true);
                }

                if (!sbtLayoutIsUpToDate || !p.pipeline.getScene()) {
                    p.pipeline.setScene(optixScene);
                    p.pipeline.setHitGroupShaderBindingTable(
                        p.hitGroupShaderBindingTable,
                        p.hitGroupShaderBindingTable.getMappedPointer());
                }
            }
            else {
                OptiX::AuxBufferGenerator &p = m_optix.auxBufferGenerator;

                if (!p.hitGroupShaderBindingTable.isInitialized() ||
                    p.hitGroupShaderBindingTable.sizeInBytes() < sbtSize) {
                    p.hitGroupShaderBindingTable.finalize();
                    p.hitGroupShaderBindingTable.initialize(
                        m_cuContext, g_bufferType, static_cast<uint32_t>(sbtSize), 1);
                    p.hitGroupShaderBindingTable.setMappedMemoryPersistent(true);
                }

                if (!sbtLayoutIsUpToDate || !p.pipeline.getScene()) {
                    p.pipeline.setScene(optixScene);
                    p.pipeline.setHitGroupShaderBindingTable(
                        p.hitGroupShaderBindingTable,
                        p.hitGroupShaderBindingTable.getMappedPointer());
                }
            }
        }
        else if (m_renderer == VLRRenderer_PathTracing) {
            OptiX::PathTracing &p = m_optix.pathTracing;

            if (!p.hitGroupShaderBindingTable.isInitialized() ||
                p.hitGroupShaderBindingTable.sizeInBytes() < sbtSize) {
                p.hitGroupShaderBindingTable.finalize();
                p.hitGroupShaderBindingTable.initialize(
                    m_cuContext, g_bufferType, static_cast<uint32_t>(sbtSize), 1);
                p.hitGroupShaderBindingTable.setMappedMemoryPersistent(true);
            }

            if (!sbtLayoutIsUpToDate || !p.pipeline.getScene()) {
                p.pipeline.setScene(optixScene);
                p.pipeline.setHitGroupShaderBindingTable(
                    p.hitGroupShaderBindingTable,
                    p.hitGroupShaderBindingTable.getMappedPointer());
            }
        }
        else if (m_renderer == VLRRenderer_LightTracing) {
            OptiX::AuxBufferGenerator &p = m_optix.auxBufferGenerator;

            if (!p.hitGroupShaderBindingTable.isInitialized() ||
                p.hitGroupShaderBindingTable.sizeInBytes() < sbtSize) {
                p.hitGroupShaderBindingTable.finalize();
                p.hitGroupShaderBindingTable.initialize(
                    m_cuContext, g_bufferType, static_cast<uint32_t>(sbtSize), 1);
                p.hitGroupShaderBindingTable.setMappedMemoryPersistent(true);
            }

            if (!sbtLayoutIsUpToDate || !p.pipeline.getScene()) {
                p.pipeline.setScene(optixScene);
                p.pipeline.setHitGroupShaderBindingTable(
                    p.hitGroupShaderBindingTable,
                    p.hitGroupShaderBindingTable.getMappedPointer());
            }

            OptiX::LightTracing &lpp = m_optix.lightTracing;

            if (!lpp.hitGroupShaderBindingTable.isInitialized() ||
                lpp.hitGroupShaderBindingTable.sizeInBytes() < sbtSize) {
                lpp.hitGroupShaderBindingTable.finalize();
                lpp.hitGroupShaderBindingTable.initialize(
                    m_cuContext, g_bufferType, static_cast<uint32_t>(sbtSize), 1);
                lpp.hitGroupShaderBindingTable.setMappedMemoryPersistent(true);
            }

            if (!sbtLayoutIsUpToDate || !lpp.pipeline.getScene()) {
                lpp.pipeline.setScene(optixScene);
                lpp.pipeline.setHitGroupShaderBindingTable(
                    lpp.hitGroupShaderBindingTable,
                    lpp.hitGroupShaderBindingTable.getMappedPointer());
            }
        }
        else if (m_renderer == VLRRenderer_BPT) {
            OptiX::LVCBPT &p = m_optix.lvcbpt;

            if (!p.hitGroupShaderBindingTable.isInitialized() ||
                p.hitGroupShaderBindingTable.sizeInBytes() < sbtSize) {
                p.hitGroupShaderBindingTable.finalize();
                p.hitGroupShaderBindingTable.initialize(
                    m_cuContext, g_bufferType, static_cast<uint32_t>(sbtSize), 1);
                p.hitGroupShaderBindingTable.setMappedMemoryPersistent(true);
            }

            if (!sbtLayoutIsUpToDate || !p.pipeline.getScene()) {
                p.pipeline.setScene(optixScene);
                p.pipeline.setHitGroupShaderBindingTable(
                    p.hitGroupShaderBindingTable,
                    p.hitGroupShaderBindingTable.getMappedPointer());
            }
        }
        else {

        }

        uint2 imageSize = make_uint2(m_width / shrinkCoeff, m_height / shrinkCoeff);
        uint32_t imageStrideInPixels = m_optix.launchParams.imageStrideInPixels;
        if (firstFrame) {
            m_optix.launchParams.imageSize = imageSize;
            camera->setup(&m_optix.launchParams);
            if (!m_optix.denoiser.stateIsReady())
                m_optix.denoiser.setupState(stream, m_optix.denoiserStateBuffer, m_optix.denoiserScratchBuffer);
            m_numAccumFrames = 0;
        }

        bool debugRender = m_renderer == VLRRenderer_DebugRendering;
        auto debugAttr = static_cast<shared::DebugRenderingAttribute>(m_debugRenderingAttribute);

        if (m_numAccumFrames + 1 <= limitNumAccumFrames) {
            ++m_numAccumFrames;
            *numAccumFrames = m_numAccumFrames;
            m_optix.launchParams.numAccumFrames = m_numAccumFrames;
            m_optix.launchParams.limitNumAccumFrames = limitNumAccumFrames;
            m_optix.launchParams.debugRenderingAttribute = debugAttr;
            m_optix.launchParams.probePixX = m_probePixX;
            m_optix.launchParams.probePixY = m_probePixY;

            if (m_renderer == VLRRenderer_PathTracing) {
                CUDADRV_CHECK(cuMemcpyHtoDAsync(m_optix.launchParamsOnDevice, &m_optix.launchParams,
                                                sizeof(m_optix.launchParams), stream));

                m_optix.pathTracing.pipeline.launch(
                    stream, m_optix.launchParamsOnDevice,
                    imageSize.x, imageSize.y, 1);
            }
            else if (m_renderer == VLRRenderer_LightTracing) {
                CUDADRV_CHECK(cuMemcpyHtoDAsync(m_optix.launchParamsOnDevice, &m_optix.launchParams,
                                                sizeof(m_optix.launchParams), stream));

                m_optix.auxBufferGenerator.pipeline.launch(
                    stream, m_optix.launchParamsOnDevice,
                    imageSize.x, imageSize.y, 1);

                m_resetAtomicAccumBuffer(
                    stream, m_resetAtomicAccumBuffer.calcGridDim(imageSize.x, imageSize.y),
                    m_optix.atomicAccumBuffer.getDevicePointer(),
                    imageSize, imageStrideInPixels);
                m_optix.lightTracing.pipeline.launch(
                    stream, m_optix.launchParamsOnDevice, OptiX::numLightPaths, 1, 1);
                m_accumulateFromAtomicAccumBuffer(
                    stream, m_accumulateFromAtomicAccumBuffer.calcGridDim(imageSize.x, imageSize.y),
                    m_optix.atomicAccumBuffer.getDevicePointer(),
                    m_optix.accumBuffer.getBlockBuffer2D(),
                    imageSize, imageStrideInPixels, static_cast<uint32_t>(firstFrame));
            }
            else if (m_renderer == VLRRenderer_BPT) {
                std::uniform_real_distribution<float> u01;
                WavelengthSamples commonWls = WavelengthSamples::createWithEqualOffsets(
                    u01(m_optix.lvcbpt.rng), u01(m_optix.lvcbpt.rng),
                    &m_optix.launchParams.wavelengthProbability);
                m_optix.launchParams.commonWavelengthSamples = commonWls;
                CUDADRV_CHECK(cuMemcpyHtoDAsync(m_optix.launchParamsOnDevice, &m_optix.launchParams,
                                                sizeof(m_optix.launchParams), stream));

                uint32_t numLightVertices = 0;
                CUDADRV_CHECK(cuMemcpyHtoDAsync(
                    m_optix.numLightVertices, &numLightVertices, sizeof(numLightVertices), stream));

                m_optix.lvcbpt.pipeline.setRayGenerationProgram(m_optix.lvcbpt.lightPathRayGen);
                m_optix.lvcbpt.pipeline.launch(
                    stream, m_optix.launchParamsOnDevice, OptiX::numLightPaths, 1, 1);

                m_resetAtomicAccumBuffer(
                    stream, m_resetAtomicAccumBuffer.calcGridDim(imageSize.x, imageSize.y),
                    m_optix.atomicAccumBuffer.getDevicePointer(),
                    imageSize, imageStrideInPixels);
                m_optix.lvcbpt.pipeline.setRayGenerationProgram(m_optix.lvcbpt.eyePathRayGen);
                m_optix.lvcbpt.pipeline.launch(
                    stream, m_optix.launchParamsOnDevice,
                    imageSize.x, imageSize.y, 1);
                m_accumulateFromAtomicAccumBuffer(
                    stream, m_accumulateFromAtomicAccumBuffer.calcGridDim(imageSize.x, imageSize.y),
                    m_optix.atomicAccumBuffer.getDevicePointer(),
                    m_optix.accumBuffer.getBlockBuffer2D(),
                    imageSize, imageStrideInPixels, 0u);

                //CUDADRV_CHECK(cuStreamSynchronize(stream));
                //CUDADRV_CHECK(cuMemcpyDtoH(
                //    &numLightVertices, m_optix.numLightVertices, sizeof(numLightVertices)));
                //sizeof(shared::LightPathVertex);
                //std::vector<shared::LightPathVertex> lightPathCache = m_optix.lightVertexCache;
                //std::vector<uint32_t> perLengthCounts;
                //float minSumProbRatios = INFINITY;
                //float maxSumProbRatios = 0;
                //for (int i = 0; i < numLightVertices; ++i) {
                //    const shared::LightPathVertex &vertex = lightPathCache[i];
                //    if (perLengthCounts.size() < vertex.pathLength + 1)
                //        perLengthCounts.resize(vertex.pathLength + 1, 0);
                //    ++perLengthCounts[vertex.pathLength];
                //    minSumProbRatios = std::min(vertex.prevSumPowerProbRatios, minSumProbRatios);
                //    maxSumProbRatios = std::max(vertex.prevSumPowerProbRatios, maxSumProbRatios);
                //}
                //printf("");
            }
            else if (m_renderer == VLRRenderer_DebugRendering) {
                CUDADRV_CHECK(cuMemcpyHtoDAsync(m_optix.launchParamsOnDevice, &m_optix.launchParams,
                                                sizeof(m_optix.launchParams), stream));

                if (m_debugRenderingAttribute < VLRDebugRenderingMode_DenoiserAlbedo) {
                    m_optix.debugRendering.pipeline.launch(
                        stream, m_optix.launchParamsOnDevice,
                        imageSize.x, imageSize.y, 1);
                }
                else {
                    m_optix.auxBufferGenerator.pipeline.launch(
                        stream, m_optix.launchParamsOnDevice,
                        imageSize.x, imageSize.y, 1);
                }
            }

            CUDADRV_CHECK(cuMemcpyHtoDAsync(m_cudaPostProcessModuleLaunchParamsPtr, &m_optix.launchParams,
                                            sizeof(m_optix.launchParams), stream));

            Quaternion camOri;
            camera->get("orientation", &camOri);
            Quaternion invCamOri = conjugate(camOri);

            m_copyBuffers(stream, m_copyBuffers.calcGridDim(imageSize.x, imageSize.y),
                          m_optix.accumBuffer.getBlockBuffer2D(),
                          m_optix.accumAlbedoBuffer.getDevicePointer(),
                          m_optix.accumNormalBuffer.getDevicePointer(),
                          invCamOri,
                          imageSize, imageStrideInPixels, m_numAccumFrames,
                          m_optix.linearColorBuffer.getDevicePointer(),
                          m_optix.linearAlbedoBuffer.getDevicePointer(),
                          m_optix.linearNormalBuffer.getDevicePointer());

            if (denoise) {
                m_optix.denoiser.computeIntensity(
                    stream,
                    m_optix.linearColorBuffer, OPTIX_PIXEL_FORMAT_FLOAT4,
                    m_optix.denoiserScratchBuffer, m_optix.hdrIntensity);

                for (int i = 0; i < m_optix.denoiserTasks.size(); ++i)
                    m_optix.denoiser.invoke(
                        stream,
                        false, m_optix.hdrIntensity, 0.0f,
                        m_optix.linearColorBuffer, OPTIX_PIXEL_FORMAT_FLOAT4,
                        m_optix.linearAlbedoBuffer, OPTIX_PIXEL_FORMAT_FLOAT4,
                        m_optix.linearNormalBuffer, OPTIX_PIXEL_FORMAT_FLOAT4,
                        optixu::BufferView(), OPTIX_PIXEL_FORMAT_FLOAT2,
                        optixu::BufferView(), m_optix.linearDenoisedColorBuffer,
                        m_optix.denoiserTasks[i]);
            }
        }

        CUsurfObject renderTarget;
        if (m_optix.useGLTexture) {
            m_optix.outputBufferHolder.beginCUDAAccess(stream);
            renderTarget = m_optix.outputBufferHolder.getNext();
        }
        else {
            renderTarget = m_optix.outputBuffer.getSurfaceObject(0);
        }
        m_convertToRGB(stream, m_convertToRGB.calcGridDim(imageSize.x, imageSize.y),
                       m_optix.accumBuffer.getBlockBuffer2D(),
                       m_optix.linearDenoisedColorBuffer.getDevicePointer(),
                       m_optix.linearAlbedoBuffer.getDevicePointer(),
                       m_optix.linearNormalBuffer.getDevicePointer(),
                       denoise, debugRender, debugAttr,
                       imageSize, imageStrideInPixels, m_numAccumFrames,
                       renderTarget);
        if (m_optix.useGLTexture)
            m_optix.outputBufferHolder.endCUDAAccess(stream);
    }



    uint32_t Context::createDirectCallableProgram(OptiXModule mdl, const char* dcName) {
        // TODO: 何かしらのかたちでパストレーシングから分離？
        uint32_t index = static_cast<uint32_t>(m_optix.pathTracing.callablePrograms.size());
        VLRAssert(m_optix.pathTracing.callablePrograms.size() == m_optix.debugRendering.callablePrograms.size(),
                  "Number of callable programs is expected to be the same between pipelines.");

        // Path Tracing
        {
            OptiX::PathTracing &p = m_optix.pathTracing;
            optixu::ProgramGroup program = p.pipeline.createCallableProgramGroup(
                p.modules[mdl], dcName,
                optixu::Module(), nullptr);
            p.callablePrograms.push_back(program);
        }

        // Light Tracing
        {
            OptiX::LightTracing &p = m_optix.lightTracing;
            optixu::ProgramGroup program = p.pipeline.createCallableProgramGroup(
                p.modules[mdl], dcName,
                optixu::Module(), nullptr);
            p.callablePrograms.push_back(program);
        }

        // LVC-BPT
        {
            OptiX::LVCBPT &p = m_optix.lvcbpt;
            optixu::ProgramGroup program = p.pipeline.createCallableProgramGroup(
                p.modules[mdl], dcName,
                optixu::Module(), nullptr);
            p.callablePrograms.push_back(program);
        }

        // Aux Buffer Generator
        {
            OptiX::AuxBufferGenerator &p = m_optix.auxBufferGenerator;
            optixu::ProgramGroup program = p.pipeline.createCallableProgramGroup(
                p.modules[mdl], dcName,
                optixu::Module(), nullptr);
            p.callablePrograms.push_back(program);
        }

        // Debug Rendering
        {
            OptiX::DebugRendering &p = m_optix.debugRendering;
            optixu::ProgramGroup program = p.pipeline.createCallableProgramGroup(
                p.modules[mdl], dcName,
                optixu::Module(), nullptr);
            p.callablePrograms.push_back(program);
        }

        return index;
    }
    void Context::destroyDirectCallableProgram(uint32_t index) {
        // Path Tracing
        {
            OptiX::PathTracing &p = m_optix.pathTracing;
            VLRAssert(index < p.callablePrograms.size(), "DC program index is out of bounds");
            p.callablePrograms[index].destroy();
        }

        // Light Tracing
        {
            OptiX::LightTracing &p = m_optix.lightTracing;
            VLRAssert(index < p.callablePrograms.size(), "DC program index is out of bounds");
            p.callablePrograms[index].destroy();
        }

        // LVC-BPT
        {
            OptiX::LVCBPT &p = m_optix.lvcbpt;
            VLRAssert(index < p.callablePrograms.size(), "DC program index is out of bounds");
            p.callablePrograms[index].destroy();
        }

        // Aux Buffer Generator
        {
            OptiX::AuxBufferGenerator &p = m_optix.auxBufferGenerator;
            VLRAssert(index < p.callablePrograms.size(), "DC program index is out of bounds");
            p.callablePrograms[index].destroy();
        }

        // Debug Rendering
        {
            OptiX::DebugRendering &p = m_optix.debugRendering;
            VLRAssert(index < p.callablePrograms.size(), "DC program index is out of bounds");
            p.callablePrograms[index].destroy();
        }
    }



    uint32_t Context::allocateNodeProcedureSet() {
        return m_optix.nodeProcedureSetBuffer.allocate();
    }
    void Context::releaseNodeProcedureSet(uint32_t index) {
        m_optix.nodeProcedureSetBuffer.release(index);
    }
    void Context::updateNodeProcedureSet(uint32_t index, const shared::NodeProcedureSet &procSet, CUstream stream) {
        m_optix.nodeProcedureSetBuffer.update(index, procSet, stream);
    }



    uint32_t Context::allocateSmallNodeDescriptor() {
        return m_optix.smallNodeDescriptorBuffer.allocate();
    }
    void Context::releaseSmallNodeDescriptor(uint32_t index) {
        m_optix.smallNodeDescriptorBuffer.release(index);
    }
    void Context::updateSmallNodeDescriptor(uint32_t index, const shared::SmallNodeDescriptor &nodeDesc, CUstream stream) {
        m_optix.smallNodeDescriptorBuffer.update(index, nodeDesc, stream);
    }



    uint32_t Context::allocateMediumNodeDescriptor() {
        return m_optix.mediumNodeDescriptorBuffer.allocate();
    }
    void Context::releaseMediumNodeDescriptor(uint32_t index) {
        m_optix.mediumNodeDescriptorBuffer.release(index);
    }
    void Context::updateMediumNodeDescriptor(uint32_t index, const shared::MediumNodeDescriptor &nodeDesc, CUstream stream) {
        m_optix.mediumNodeDescriptorBuffer.update(index, nodeDesc, stream);
    }



    uint32_t Context::allocateLargeNodeDescriptor() {
        return m_optix.largeNodeDescriptorBuffer.allocate();
    }
    void Context::releaseLargeNodeDescriptor(uint32_t index) {
        m_optix.largeNodeDescriptorBuffer.release(index);
    }
    void Context::updateLargeNodeDescriptor(uint32_t index, const shared::LargeNodeDescriptor &nodeDesc, CUstream stream) {
        m_optix.largeNodeDescriptorBuffer.update(index, nodeDesc, stream);
    }



    uint32_t Context::allocateBSDFProcedureSet() {
        return m_optix.bsdfProcedureSetBuffer.allocate();
    }
    void Context::releaseBSDFProcedureSet(uint32_t index) {
        m_optix.bsdfProcedureSetBuffer.release(index);
    }
    void Context::updateBSDFProcedureSet(uint32_t index, const shared::BSDFProcedureSet &procSet, CUstream stream) {
        m_optix.bsdfProcedureSetBuffer.update(index, procSet, stream);
    }



    uint32_t Context::allocateEDFProcedureSet() {
        return m_optix.edfProcedureSetBuffer.allocate();
    }
    void Context::releaseEDFProcedureSet(uint32_t index) {
        m_optix.edfProcedureSetBuffer.release(index);
    }
    void Context::updateEDFProcedureSet(uint32_t index, const shared::EDFProcedureSet &procSet, CUstream stream) {
        m_optix.edfProcedureSetBuffer.update(index, procSet, stream);
    }



    uint32_t Context::allocateIDFProcedureSet() {
        return m_optix.idfProcedureSetBuffer.allocate();
    }
    void Context::releaseIDFProcedureSet(uint32_t index) {
        m_optix.idfProcedureSetBuffer.release(index);
    }
    void Context::updateIDFProcedureSet(uint32_t index, const shared::IDFProcedureSet &procSet, CUstream stream) {
        m_optix.idfProcedureSetBuffer.update(index, procSet, stream);
    }



    uint32_t Context::allocateSurfaceMaterialDescriptor() {
        return m_optix.surfaceMaterialDescriptorBuffer.allocate();
    }
    void Context::releaseSurfaceMaterialDescriptor(uint32_t index) {
        m_optix.surfaceMaterialDescriptorBuffer.release(index);
    }
    void Context::updateSurfaceMaterialDescriptor(uint32_t index, const shared::SurfaceMaterialDescriptor &matDesc, CUstream stream) {
        m_optix.surfaceMaterialDescriptorBuffer.update(index, matDesc, stream);
    }



    void Context::markShaderNodeDescriptorDirty(ShaderNode* node) {
        m_optix.dirtyShaderNodes.insert(node);
    }

    void Context::markSurfaceMaterialDescriptorDirty(SurfaceMaterial* mat) {
        m_optix.dirtySurfaceMaterials.insert(mat);
    }



    // ----------------------------------------------------------------
    // Miscellaneous

    template <typename RealType>
    void DiscreteDistribution1DTemplate<RealType>::initialize(Context &context, const RealType* values, size_t numValues) {
        CUcontext cuContext = context.getCUcontext();

        m_numValues = static_cast<uint32_t>(numValues);
        m_PMF.initialize(cuContext, g_bufferType, m_numValues);
        m_CDF.initialize(cuContext, g_bufferType, m_numValues + 1);

        RealType* PMF = m_PMF.map();
        RealType* CDF = m_CDF.map();
        std::memcpy(PMF, values, sizeof(RealType) * m_numValues);

        CompensatedSum<RealType> sum(0);
        for (int i = 0; i < static_cast<int>(m_numValues); ++i) {
            CDF[i] = sum;
            sum += PMF[i];
        }
        m_integral = sum;
        for (int i = 0; i < static_cast<int>(m_numValues); ++i) {
            PMF[i] /= m_integral;
            CDF[i] /= m_integral;
        }
        CDF[m_numValues] = 1.0f;

        m_CDF.unmap();
        m_PMF.unmap();
    }

    template <typename RealType>
    void DiscreteDistribution1DTemplate<RealType>::finalize(Context &context) {
        if (m_CDF.isInitialized() && m_PMF.isInitialized()) {
            m_CDF.finalize();
            m_PMF.finalize();
        }
    }

    template <typename RealType>
    void DiscreteDistribution1DTemplate<RealType>::getInternalType(shared::DiscreteDistribution1DTemplate<RealType>* instance) const {
        if (m_PMF.isInitialized() && m_CDF.isInitialized())
            new (instance) shared::DiscreteDistribution1DTemplate<RealType>(
                m_PMF.getDevicePointer(), m_CDF.getDevicePointer(), m_integral, m_numValues);
    }

    template class DiscreteDistribution1DTemplate<float>;



    template <typename RealType>
    void RegularConstantContinuousDistribution1DTemplate<RealType>::initialize(Context &context, const RealType* values, size_t numValues) {
        CUcontext cuContext = context.getCUcontext();

        m_numValues = static_cast<uint32_t>(numValues);
        m_PDF.initialize(cuContext, g_bufferType, m_numValues);
        m_CDF.initialize(cuContext, g_bufferType, m_numValues + 1);

        RealType* PDF = m_PDF.map();
        RealType* CDF = m_CDF.map();
        std::memcpy(PDF, values, sizeof(RealType) * m_numValues);

        CompensatedSum<RealType> sum{ 0 };
        for (int i = 0; i < static_cast<int>(m_numValues); ++i) {
            CDF[i] = sum;
            sum += PDF[i] / m_numValues;
        }
        m_integral = sum;
        for (int i = 0; i < static_cast<int>(m_numValues); ++i) {
            PDF[i] /= m_integral;
            CDF[i] /= m_integral;
        }
        CDF[m_numValues] = 1.0f;

        m_CDF.unmap();
        m_PDF.unmap();
    }

    template <typename RealType>
    void RegularConstantContinuousDistribution1DTemplate<RealType>::finalize(Context &context) {
        if (m_CDF.isInitialized() && m_PDF.isInitialized()) {
            m_CDF.finalize();
            m_PDF.finalize();
        }
    }

    template <typename RealType>
    void RegularConstantContinuousDistribution1DTemplate<RealType>::getInternalType(shared::RegularConstantContinuousDistribution1DTemplate<RealType>* instance) const {
        new (instance) shared::RegularConstantContinuousDistribution1DTemplate<RealType>(
            m_PDF.getDevicePointer(), m_CDF.getDevicePointer(), m_integral, m_numValues);
    }

    template class RegularConstantContinuousDistribution1DTemplate<float>;



    template <typename RealType>
    void RegularConstantContinuousDistribution2DTemplate<RealType>::initialize(Context &context, const RealType* values, size_t numD1, size_t numD2) {
        CUcontext cuContext = context.getCUcontext();

        m_1DDists = new RegularConstantContinuousDistribution1DTemplate<RealType>[numD2];
        m_raw1DDists.initialize(cuContext, g_bufferType, static_cast<uint32_t>(numD2));

        shared::RegularConstantContinuousDistribution1DTemplate<RealType>* rawDists = m_raw1DDists.map();

        // JP: まず各行に関するDistribution1Dを作成する。
        // EN: First, create Distribution1D's for every rows.
        CompensatedSum<RealType> sum(0);
        RealType* integrals = new RealType[numD2];
        for (int i = 0; i < numD2; ++i) {
            RegularConstantContinuousDistribution1D &dist = m_1DDists[i];
            dist.initialize(context, values + i * numD1, numD1);
            dist.getInternalType(&rawDists[i]);
            integrals[i] = dist.getIntegral();
            sum += integrals[i];
        }

        // JP: 各行の積分値を用いてDistribution1Dを作成する。
        // EN: create a Distribution1D using integral values of each row.
        m_top1DDist.initialize(context, integrals, numD2);
        delete[] integrals;

        VLRAssert(std::isfinite(m_top1DDist.getIntegral()), "invalid integral value.");

        m_raw1DDists.unmap();
    }

    template <typename RealType>
    void RegularConstantContinuousDistribution2DTemplate<RealType>::finalize(Context &context) {
        m_top1DDist.finalize(context);

        for (int i = m_top1DDist.getNumValues() - 1; i >= 0; --i) {
            m_1DDists[i].finalize(context);
        }

        m_raw1DDists.finalize();
        delete[] m_1DDists;
        m_1DDists = nullptr;
    }

    template <typename RealType>
    void RegularConstantContinuousDistribution2DTemplate<RealType>::getInternalType(shared::RegularConstantContinuousDistribution2DTemplate<RealType>* instance) const {
        shared::RegularConstantContinuousDistribution1DTemplate<RealType> top1DDist;
        m_top1DDist.getInternalType(&top1DDist);
        new (instance) shared::RegularConstantContinuousDistribution2DTemplate<RealType>(
            m_raw1DDists.getDevicePointer(), top1DDist);
    }

    template class RegularConstantContinuousDistribution2DTemplate<float>;

    // END: Miscellaneous
    // ----------------------------------------------------------------
}
