﻿#pragma once

#include "materials.h"

namespace vlr {
    class Transform : public TypeAwareClass {
    public:
        VLR_DECLARE_TYPE_AWARE_CLASS_INTERFACE();

        virtual ~Transform() {}

        virtual bool isStatic() const = 0;
    };



    class StaticTransform : public Transform {
        Matrix4x4 m_matrix;
        Matrix4x4 m_invMatrix;

    public:
        VLR_DECLARE_TYPE_AWARE_CLASS_INTERFACE();

        StaticTransform(const Matrix4x4 &m = Matrix4x4::Identity()) : m_matrix(m), m_invMatrix(invert(m)) {}

        bool isStatic() const override { return true; }

        StaticTransform operator*(const Matrix4x4 &m) const { return StaticTransform(m_matrix * m); }
        StaticTransform operator*(const StaticTransform &t) const { return StaticTransform(m_matrix * t.m_matrix); }
        bool operator==(const StaticTransform &t) const { return m_matrix == t.m_matrix; }
        bool operator!=(const StaticTransform &t) const { return m_matrix != t.m_matrix; }

        void getArrays(float mat[16], float invMat[16]) const {
            m_matrix.getArray(mat);
            m_invMatrix.getArray(invMat);
        }
    };



    // ----------------------------------------------------------------
    // Shallow Hierarchy
    // JP: レンダリング時のパフォーマンスを考えるとシーン階層は浅いほうが良い。
    //     ユーザーには任意の深いシーングラフを許可する裏で同時に浅いシーングラフを生成する。
    // EN: Shallower scene hierarchy is preferrable for rendering performance.
    //     Generate shallow scene graph while allowing the user build arbitrarily deep scene graph.

    class SHGeometryGroup;
    struct SHGeometryInstance;
    class SurfaceNode;

    class SHGeometryGroup {
        std::vector<const SHGeometryInstance*> m_shGeomInsts;

    public:
        SHGeometryGroup() {}
        ~SHGeometryGroup() {}

        void addChild(const SHGeometryInstance* geomInst) {
            m_shGeomInsts.push_back(geomInst);
        }
        void removeChild(const SHGeometryInstance* geomInst) {
            auto idx = std::find(m_shGeomInsts.cbegin(), m_shGeomInsts.cend(), geomInst);
            VLRAssert(idx != m_shGeomInsts.cend(), "SHGeometryInstance %p is not a child of SHGeometryGroup %p.", geomInst, this);
            m_shGeomInsts.erase(idx);
        }
        void updateChild(const SHGeometryInstance* geomInst) {
            auto idx = std::find(m_shGeomInsts.cbegin(), m_shGeomInsts.cend(), geomInst);
            VLRAssert(idx != m_shGeomInsts.cend(), "SHGeometryInstance %p is not a child of SHGeometryGroup %p.", geomInst, this);
            VLRAssert_NotImplemented();
        }

        const SHGeometryInstance* childAt(uint32_t index) const {
            return m_shGeomInsts[index];
        }
        uint32_t getNumChildren() const {
            return static_cast<uint32_t>(m_shGeomInsts.size());
        }
    };

    struct SHGeometryInstance {
        const SurfaceNode* surfNode;
        uint32_t userData;

        SHGeometryInstance() : surfNode(nullptr), userData(0) {}
    };

    class SHTransform {
        std::string m_name;

        StaticTransform m_transform;
        union {
            const SHTransform* m_childTransform;
            const SHGeometryGroup* m_childGeomGroup;
        };
        bool m_childIsTransform;

        StaticTransform resolveTransform() const;

    public:
        SHTransform(const std::string &name, const StaticTransform &transform, const SHGeometryGroup* geomGroup) :
            m_name(name),
            m_transform(transform), m_childGeomGroup(geomGroup),
            m_childIsTransform(false) {}
        SHTransform(const std::string &name, const StaticTransform &transform, const SHTransform* childTransform) :
            m_name(name),
            m_transform(transform), m_childTransform(childTransform),
            m_childIsTransform(true) {}
        ~SHTransform() {}

        const std::string &getName() const { return m_name; }
        void setName(const std::string &name) {
            m_name = name;
        }

        void setTransform(const StaticTransform &transform);
        void update();
        bool isStatic() const;
        StaticTransform getStaticTransform() const;

        void setChild(const SHGeometryGroup* childGeomGroup);
        const SHGeometryGroup* getGeometryDescendant() const;
    };

    // END: Shallow Hierarchy
    // ----------------------------------------------------------------



    class Node;
    class ParentNode;
    class InternalNode;
    class Scene;



    class Node : public Object {
    protected:
        std::string m_name;

    public:
        VLR_DECLARE_TYPE_AWARE_CLASS_INTERFACE();

        Node(Context &context, const std::string &name) :
            Object(context), m_name(name) {}
        virtual ~Node() {}

        virtual void setName(const std::string &name) {
            m_name = name;
        }
        virtual void addParent(ParentNode* parent) {
            VLRAssert_ShouldNotBeCalled();
        }
        virtual void removeParent(ParentNode* parent) {
            VLRAssert_ShouldNotBeCalled();
        }

        const std::string &getName() const {
            return m_name;
        }
    };



    class SurfaceNode : public Node {
    protected:
        std::set<ParentNode*> m_parents;

    public:
        VLR_DECLARE_TYPE_AWARE_CLASS_INTERFACE();

        static void initialize(Context &context);
        static void finalize(Context &context);

        SurfaceNode(Context &context, const std::string &name) : Node(context, name) {}
        virtual ~SurfaceNode() {
            while (!m_parents.empty())
                removeParent(*m_parents.rbegin());
        }

        void addParent(ParentNode* parent) override;
        void removeParent(ParentNode* parent) override;

        virtual bool isIntersectable() const { return true; }
        virtual void setupData(
            uint32_t userData, uint32_t geomInstIndex,
            optixu::GeometryInstance* optixGeomInst, shared::GeometryInstance* geomInst) const = 0;
    };



    class TriangleMeshSurfaceNode : public SurfaceNode {
        struct OptiXProgramSet {
            uint32_t dcDecodeLocalHitPointForTriangle;
            uint32_t dcDecodeHitPointForTriangle;
            uint32_t dcSampleTriangleMesh;
        };

        static std::unordered_map<uint32_t, OptiXProgramSet> s_optiXProgramSets;

        struct MaterialGroup {
            std::vector<uint32_t> indices;
            cudau::TypedBuffer<shared::Triangle> optixIndexBuffer;
            DiscreteDistribution1D primDist;
            const SurfaceMaterial* material;
            ShaderNodePlug nodeNormal;
            ShaderNodePlug nodeTangent;
            ShaderNodePlug nodeAlpha;
            BoundingBox3D aabb;
            SHGeometryInstance* shGeomInst;

            MaterialGroup() {}
            MaterialGroup(MaterialGroup &&v) {
                indices = std::move(v.indices);
                optixIndexBuffer = std::move(v.optixIndexBuffer);
                primDist = std::move(v.primDist);
                material = v.material;
                nodeNormal = v.nodeNormal;
                nodeTangent = v.nodeTangent;
                nodeAlpha = v.nodeAlpha;
                aabb = v.aabb;
                shGeomInst = v.shGeomInst;
            }
            MaterialGroup &operator=(MaterialGroup &&v) {
                indices = std::move(v.indices);
                optixIndexBuffer = std::move(v.optixIndexBuffer);
                primDist = std::move(v.primDist);
                material = v.material;
                nodeNormal = v.nodeNormal;
                nodeTangent = v.nodeTangent;
                nodeAlpha = v.nodeAlpha;
                aabb = v.aabb;
                shGeomInst = v.shGeomInst;
                return *this;
            }
        };

        std::vector<Vertex> m_vertices;
        cudau::TypedBuffer<Vertex> m_optixVertexBuffer;
        std::vector<MaterialGroup> m_materialGroups;

    public:
        VLR_DECLARE_TYPE_AWARE_CLASS_INTERFACE();

        static void initialize(Context &context);
        static void finalize(Context &context);

        TriangleMeshSurfaceNode(Context &context, const std::string &name);
        ~TriangleMeshSurfaceNode();

        void addParent(ParentNode* parent) override;
        void removeParent(ParentNode* parent) override;

        void setVertices(std::vector<Vertex> &&vertices);
        void addMaterialGroup(
            std::vector<uint32_t> &&indices, const SurfaceMaterial* material, 
            const ShaderNodePlug &nodeNormal, const ShaderNodePlug& nodeTangent, const ShaderNodePlug &nodeAlpha);

        void setupData(
            uint32_t userData, uint32_t geomInstIndex,
            optixu::GeometryInstance* optixGeomInst, shared::GeometryInstance* geomInst) const;
    };



    class PointSurfaceNode : public SurfaceNode {
        struct OptiXProgramSet {
            uint32_t dcDecodeHitPointForPoint;
            uint32_t dcSamplePoint;
        };

        static std::unordered_map<uint32_t, OptiXProgramSet> s_optiXProgramSets;

        struct MaterialGroup {
            std::vector<uint32_t> indices;
            cudau::TypedBuffer<uint32_t> optixIndexBuffer;
            DiscreteDistribution1D primDist;
            const SurfaceMaterial* material;
            SHGeometryInstance* shGeomInst;

            MaterialGroup() {}
            MaterialGroup(MaterialGroup &&v) {
                indices = std::move(v.indices);
                optixIndexBuffer = std::move(v.optixIndexBuffer);
                primDist = std::move(v.primDist);
                material = v.material;
                shGeomInst = v.shGeomInst;
            }
            MaterialGroup &operator=(MaterialGroup &&v) {
                indices = std::move(v.indices);
                optixIndexBuffer = std::move(v.optixIndexBuffer);
                primDist = std::move(v.primDist);
                material = v.material;
                shGeomInst = v.shGeomInst;
                return *this;
            }
        };

        std::vector<Vertex> m_vertices;
        cudau::TypedBuffer<Vertex> m_optixVertexBuffer;
        std::vector<MaterialGroup> m_materialGroups;

    public:
        VLR_DECLARE_TYPE_AWARE_CLASS_INTERFACE();

        static void initialize(Context &context);
        static void finalize(Context &context);

        PointSurfaceNode(Context &context, const std::string &name);
        ~PointSurfaceNode();

        void addParent(ParentNode* parent) override;
        void removeParent(ParentNode* parent) override;

        void setVertices(std::vector<Vertex> &&vertices);
        void addMaterialGroup(
            std::vector<uint32_t> &&indices, const SurfaceMaterial* material);

        bool isIntersectable() const override { return false; }
        void setupData(
            uint32_t userData, uint32_t geomInstIndex,
            optixu::GeometryInstance* optixGeomInst, shared::GeometryInstance* geomInst) const override;
    };



    class InfiniteSphereSurfaceNode : public SurfaceNode {
        struct OptiXProgramSet {
            uint32_t dcDecodeHitPointForInfiniteSphere;
            uint32_t dcSampleInfiniteSphere;
        };

        static std::unordered_map<uint32_t, OptiXProgramSet> s_optiXProgramSets;

        EnvironmentEmitterSurfaceMaterial* m_material;
        SHGeometryInstance* m_shGeomInst;

    public:
        VLR_DECLARE_TYPE_AWARE_CLASS_INTERFACE();

        static void initialize(Context &context);
        static void finalize(Context &context);

        InfiniteSphereSurfaceNode(Context &context, const std::string &name, EnvironmentEmitterSurfaceMaterial* material);
        ~InfiniteSphereSurfaceNode();

        void addParent(ParentNode* parent) override;
        void removeParent(ParentNode* parent) override;

        bool isIntersectable() const override { return false; }
        void setupData(
            uint32_t userData, uint32_t geomInstIndex,
            optixu::GeometryInstance* optixGeomInst, shared::GeometryInstance* geomInst) const override;
    };



    // JP: ParentNodeは対応するひとつのSHTransformとSHGeometryGroupを持つ。
    //     また自分の子孫が持つ連なったSHTransformも管理する。
    //     SHGeometryGroupにはParentNodeに「直接」所属するSurfaceNodeのジオメトリが登録される。
    // EN: ParentNode has a single SHTransform corresponding the node itself and a single SHGeometryGroup.
    //     And it manages chaining SHTransforms that children have.
    //     SHGeometryGroup holds geometries of SurfaceNode's which *directly* belong to the ParentNode.
    class ParentNode : public Node {
    protected:
        uint32_t m_serialChildID;
        std::unordered_set<Node*> m_children;
        std::vector<Node*> m_orderedChildren;
        const Transform* m_localToWorld;

        // key: child SHTransform
        // JP: 自分自身のトランスフォームのみを含むSHTransformはnullptrをキーとする。
        // EN: SHTransform containing only the self transform uses nullptr as the key.
        std::map<const SHTransform*, SHTransform*> m_shTransforms;

        SHGeometryGroup* m_shGeomGroup;

        void createConcatanatedTransforms(const std::set<SHTransform*>& childDelta, std::set<SHTransform*>* delta);
        void removeConcatanatedTransforms(const std::set<SHTransform*>& childDelta, std::set<SHTransform*>* delta);
        void updateConcatanatedTransforms(const std::set<SHTransform*>& childDelta, std::set<SHTransform*>* delta);

    public:
        VLR_DECLARE_TYPE_AWARE_CLASS_INTERFACE();

        ParentNode(Context &context, const std::string &name, const Transform* localToWorld);
        virtual ~ParentNode();

        void setName(const std::string &name) override;

        void addGeometryInstance(const std::set<const SHGeometryInstance*> &childDelta);
        void removeGeometryInstance(const std::set<const SHGeometryInstance*> &childDelta);
        void updateGeometryInstance(const std::set<const SHGeometryInstance*> &childDelta);

        virtual void transformAddEvent(const std::set<SHTransform*> &childDelta) = 0;
        virtual void transformRemoveEvent(const std::set<SHTransform*> &childDelta) = 0;
        virtual void transformUpdateEvent(const std::set<SHTransform*> &childDelta) = 0;

        virtual void geometryAddEvent(const SHTransform* childTransform,
                                      const std::set<const SHGeometryInstance*> &childDelta) = 0;
        virtual void geometryRemoveEvent(const SHTransform* childTransform,
                                         const std::set<const SHGeometryInstance*> &childDelta) = 0;
        virtual void geometryUpdateEvent(const SHTransform* childTransform,
                                         const std::set<const SHGeometryInstance*> &childDelta) = 0;

        virtual void setTransform(const Transform* localToWorld);
        const Transform* getTransform() const {
            return m_localToWorld;
        }

        void addChild(InternalNode* child);
        void removeChild(InternalNode* child);
        void addChild(SurfaceNode* child);
        void removeChild(SurfaceNode* child);
        uint32_t getNumChildren() const;
        void getChildren(Node** children) const;
        Node* getChildAt(uint32_t index) const;
    };



    class InternalNode : public ParentNode {
        std::set<ParentNode*> m_parents;

    public:
        VLR_DECLARE_TYPE_AWARE_CLASS_INTERFACE();

        InternalNode(Context &context, const std::string &name, const Transform* localToWorld);
        ~InternalNode();

        void transformAddEvent(const std::set<SHTransform*> &childDelta) override;
        void transformRemoveEvent(const std::set<SHTransform*> &childDelta) override;
        void transformUpdateEvent(const std::set<SHTransform*> &childDelta) override;

        void geometryAddEvent(const SHTransform* childTransform,
                              const std::set<const SHGeometryInstance*> &childDelta) override;
        void geometryRemoveEvent(const SHTransform* childTransform,
                                 const std::set<const SHGeometryInstance*> &childDelta) override;
        void geometryUpdateEvent(const SHTransform* childTransform,
                                 const std::set<const SHGeometryInstance*> &childDelta) override;

        void setTransform(const Transform* localToWorld) override;

        void addParent(ParentNode* parent) override;
        void removeParent(ParentNode* parent) override;
    };



    class Scene : public ParentNode {
        optixu::Scene m_optixScene;

        std::unordered_set<SurfaceNode*> m_dirtySurfaceNodes;
        std::unordered_set<ParentNode*> m_dirtyParentNodes;
        bool m_iasIsDirty;

        SlotBuffer<shared::GeometryInstance> m_geomInstBuffer;
        SlotBuffer<shared::Instance> m_instBuffer;

        optixu::InstanceAccelerationStructure m_ias;
        cudau::Buffer m_iasMem;
        cudau::TypedBuffer<OptixInstance> m_instanceBuffer;

        cudau::TypedBuffer<uint32_t> m_computeInstAabbs_instIndices;
        cudau::TypedBuffer<uint32_t> m_computeInstAabbs_itemOffsets;
        CUdeviceptr m_sceneBounds;
        cudau::TypedBuffer<uint32_t> m_lightInstIndices;
        DiscreteDistribution1D m_lightInstDist;

        struct GeometryInstance {
            optixu::GeometryInstance optixGeomInst;
            uint32_t geomInstIndex;
            shared::GeometryInstance data;
            uint32_t referenceCount;
        };
        struct GeometryAS {
            optixu::GeometryAccelerationStructure optixGas;
            cudau::Buffer optixGasMem;
        };
        struct Instance {
            optixu::Instance optixInst;
            uint32_t instIndex;
            cudau::TypedBuffer<uint32_t> optixGeomInstIndices;
            DiscreteDistribution1D lightGeomInstDistribution;
            shared::Instance data;
        };

        // Environmental Light
        EnvironmentEmitterSurfaceMaterial* m_matEnv;
        InfiniteSphereSurfaceNode* m_envNode;
        GeometryInstance m_envGeomInst;
        Instance m_envInst;
        bool m_envIsDirty;

        std::unordered_map<const SHGeometryInstance*, GeometryInstance> m_geometryInstances;
        std::unordered_map<const SHGeometryGroup*, GeometryAS> m_geometryASes;
        std::unordered_map<const SHTransform*, Instance> m_instances;

        std::unordered_set<const SHGeometryInstance*> m_dirtyGeometryInstances;
        std::unordered_set<uint32_t> m_removedGeometryInstanceIndices;
        std::unordered_set<const SHGeometryGroup*> m_dirtyGeometryASes;
        std::unordered_set<const SHTransform*> m_dirtyInstances;
        std::unordered_set<uint32_t> m_removedInstanceIndices;

    public:
        VLR_DECLARE_TYPE_AWARE_CLASS_INTERFACE();

        static void initialize(Context &context);
        static void finalize(Context &context);

        Scene(Context &context, const Transform* localToWorld);
        ~Scene();

        void transformAddEvent(const std::set<SHTransform*> &childDelta) override;
        void transformRemoveEvent(const std::set<SHTransform*> &childDelta) override;
        void transformUpdateEvent(const std::set<SHTransform*> &childDelta) override;

        void geometryAddEvent(const SHTransform* childTransform,
                              const std::set<const SHGeometryInstance*> &childDelta) override;
        void geometryRemoveEvent(const SHTransform* childTransform,
                                 const std::set<const SHGeometryInstance*> &childDelta) override;
        void geometryUpdateEvent(const SHTransform* childTransform,
                                 const std::set<const SHGeometryInstance*> &childDelta) override;

        void prepareSetup(size_t* asScratchSize, optixu::Scene* optixScene);
        void setup(
            CUstream stream,
            const cudau::Buffer &asScratchMem, shared::PipelineLaunchParameters* launchParams);

        // TODO: 内部実装をInfiniteSphereSurfaceNode + EnvironmentEmitterMaterialを使ったものに変えられないかを考える。
        void setEnvironment(EnvironmentEmitterSurfaceMaterial* matEnv);
        void setEnvironmentRotation(float rotationPhi);
    };



    class Camera : public Queryable {
        enum CameraCallableName {
            CameraCallableName_sample = 0,
            CameraCallableName_testIntersection,
            CameraCallableName_setupIDF,
            CameraCallableName_IDF_sampleInternal,
            CameraCallableName_IDF_evaluateSpatialImportanceInternal,
            CameraCallableName_IDF_evaluateDirectionalImportanceInternal,
            CameraCallableName_IDF_evaluatePDFInternal,
            CameraCallableName_IDF_backProjectDirection,
            NumCameraCallableNames
        };

    protected:
        struct OptiXProgramSet {
            uint32_t dcSampleLensPosition;
            uint32_t dcTestLensIntersection;
            uint32_t dcSetupIDF;
            uint32_t dcIDFSampleInternal;
            uint32_t dcIDFEvaluateSpatialImportanceInternal;
            uint32_t dcIDFEvaluateDirectionalImportanceInternal;
            uint32_t dcIDFEvaluatePDFInternal;
            uint32_t dcIDFBackProjectDirectionInternal;
            uint32_t idfProcedureSetIndex;
        };

        static void commonInitializeProcedure(
            Context& context, const char* identifiers[NumCameraCallableNames], OptiXProgramSet* programSet);
        static void commonFinalizeProcedure(Context& context, OptiXProgramSet& programSet);

    public:
        VLR_DECLARE_TYPE_AWARE_CLASS_INTERFACE();

        static void initialize(Context &context);
        static void finalize(Context &context);

        Camera(Context &context) : 
            Queryable(context) {}
        virtual ~Camera() {}

        virtual void setup(shared::PipelineLaunchParameters* launchParams) const = 0;
    };



    class PerspectiveCamera : public Camera {
        VLR_DECLARE_QUERYABLE_INTERFACE();

        static std::unordered_map<uint32_t, OptiXProgramSet> s_optiXProgramSets;

        shared::PerspectiveCamera m_data;

    public:
        VLR_DECLARE_TYPE_AWARE_CLASS_INTERFACE();

        static void initialize(Context &context);
        static void finalize(Context &context);

        PerspectiveCamera(Context &context);

        bool get(const char* paramName, Point3D* value) const override;
        bool get(const char* paramName, Quaternion* value) const override;
        bool get(const char* paramName, float* values, uint32_t length) const override;

        bool set(const char* paramName, const Point3D &value) override;
        bool set(const char* paramName, const Quaternion &value) override;
        bool set(const char* paramName, const float* values, uint32_t length) override;

        void setup(shared::PipelineLaunchParameters* launchParams) const override;
    };



    class EquirectangularCamera : public Camera {
        VLR_DECLARE_QUERYABLE_INTERFACE();

        static std::unordered_map<uint32_t, OptiXProgramSet> s_optiXProgramSets;

        shared::EquirectangularCamera m_data;

    public:
        VLR_DECLARE_TYPE_AWARE_CLASS_INTERFACE();

        static void initialize(Context &context);
        static void finalize(Context &context);

        EquirectangularCamera(Context &context);

        bool get(const char* paramName, Point3D* value) const override;
        bool get(const char* paramName, Quaternion* value) const override;
        bool get(const char* paramName, float* values, uint32_t length) const override;

        bool set(const char* paramName, const Point3D& value) override;
        bool set(const char* paramName, const Quaternion& value) override;
        bool set(const char* paramName, const float* values, uint32_t length) override;

        void setup(shared::PipelineLaunchParameters* launchParams) const override;
    };
}
