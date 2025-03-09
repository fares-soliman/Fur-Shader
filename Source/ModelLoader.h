#pragma once
#include "../Utilities/d3dUtil.h"
#include "../Utilities/MathHelper.h"
#include "../Utilities/tAutodeskMemoryStream.h"
#include <fbxsdk.h>
#include <string>
#include <vector>

class ModelLoader
{

public:
	// Skinned mesh definition
	typedef struct
	{
		DirectX::XMFLOAT3 point;
		DirectX::XMFLOAT3 normal;
		DirectX::XMFLOAT2 tex;
		unsigned long boneWeights;
		unsigned long boneIndices;
	} tSkinnedVertice;

private:
	fbxsdk::FbxManager* m_sdkManagerPtr;
	Microsoft::WRL::ComPtr<ID3D12Device> m_devicePtr;

	// All the necessary pieces to keep an .fbx model loaded

	enum
	{
		kInvalidBoneIndex = -1,
		kTriangleVertexCount = 3,
		kBoneInfluencesPerVertice = 4,
	};

	typedef union
	{
		unsigned char bytes[4];
		unsigned long number;
	} tPackedInt;

	typedef std::vector<tSkinnedVertice> tSkinnedVerticeVector;
	typedef tSkinnedVerticeVector::iterator tSkinnedVerticeIterator;
	typedef tSkinnedVerticeVector::const_iterator tSkinnedVerticeConstIterator;

	typedef std::vector<unsigned short> tVertexIndexVector;
	typedef tVertexIndexVector::iterator tVertexIndexIterator;
	typedef tVertexIndexVector::const_iterator tVertexIndexConstIterator;

	typedef struct
	{
		std::string materialName;
		std::string meshName;
		tSkinnedVerticeVector verticeVector;
		tVertexIndexVector indexVector;
		Microsoft::WRL::ComPtr<ID3D12Resource> indexBufferPtr;
		Microsoft::WRL::ComPtr<ID3D12Resource> vertexBufferPtr;
	} tModelRec;
	typedef std::vector<tModelRec> tModelVector;
	typedef tModelVector::iterator tModelIterator;
	typedef tModelVector::const_iterator tModelConstIterator;

	typedef struct
	{
		std::string name;

		// calculates the locals together depth first
		// equals localTransform * parentMatrix
		DirectX::XMFLOAT4X4 combinedTransform;

		// updated for animation
		DirectX::XMFLOAT4X4 nodeLocalTransform;

		// This is an inverse of the parent to child matrix,
		// or you can call it the inverse bind pose matrix.
		// It's set at mesh load time and doesn't change.
		DirectX::XMFLOAT4X4 offset;

		// Ready for a skinned mesh shader as the bone matrice.
		// equals offset * combinedTransform
		DirectX::XMFLOAT4X4 boneMatrice;
		fbxsdk::FbxNode* boneNodePtr;
		fbxsdk::FbxSkeleton* fbxSkeletonPtr;
		fbxsdk::FbxCluster* fbxClusterPtr;

		std::vector<int> childIndexes;
		int parentIndex;
	} tBone;

	// Used to do a reverse lookup.
	typedef std::vector<int> tControlPointIndexes;
	typedef std::vector<tControlPointIndexes> tControlPointRemap;

	typedef std::vector<tBone> tBoneVector;
	typedef tBoneVector::iterator tBoneIterator;
	typedef tBoneVector::const_iterator tBoneConstIterator;

	typedef std::vector<DirectX::XMFLOAT4X4> tMatrixVector;
	typedef tMatrixVector::iterator tMatrixIterator;
	typedef tMatrixVector::const_iterator tMatrixConstIterator;

	std::string m_filename;
	fbxsdk::FbxScene* m_scenePtr;
	int m_majorFileVersion;
	int m_minorFileVersion;
	int m_revisionFileVersion;
	std::string m_textureName;
	tBoneVector m_boneVector;
	bool m_allByControlPoint;
	unsigned int m_boneMatrixVectorSize;
	unsigned long long m_initialAnimationDurationInMs;

	void _buildMatrices(
		const fbxsdk::FbxTime& time);

	void _loadModel();
	void _loadBones(
		fbxsdk::FbxNode* nodePtr,
		long parentBoneIndex);
	void _loadBone(
		fbxsdk::FbxNode* nodePtr,
		long parentBoneIndex);
	void _loadMeshes(
		fbxsdk::FbxNode* nodePtr);
	void _loadMesh(
		fbxsdk::FbxNode* nodePtr);
	void _loadMeshPositionNormalUV(
		fbxsdk::FbxNode* nodePtr,
		tModelRec& meshRec);
	void _compressSkinnedVertices(
		tModelRec& modelRec);
	unsigned short _findSkinnedVertice(
		const tSkinnedVerticeVector& skinnedVerticeVector,
		const tSkinnedVertice& skinnedVertice);
	void _loadMeshBoneWeightsAndIndices(
		fbxsdk::FbxNode* nodePtr,
		tModelRec& modelRec);
	void _normalizeBoneWeights(
		tModelRec& modelRec);
	void _loadTextureNames();

	bool _isMeshSkinned(
		fbxsdk::FbxMesh* meshPtr);
	long _getBoneCount(
		fbxsdk::FbxMesh* meshPtr);
	void _getGeometryTransformMatrix(
		fbxsdk::FbxNode* nodePtr,
		DirectX::XMFLOAT4X4& geometryOffsetMatrix);
	void _getNodeLocalTransform(
		fbxsdk::FbxNode* nodePtr,
		DirectX::XMFLOAT4X4& matrix);
	void _getNodeLocalTransform(
		fbxsdk::FbxNode* nodePtr,
		const fbxsdk::FbxTime& fbxTime,
		DirectX::XMFLOAT4X4& matrix);
	void _fbxToMatrix(
		const fbxsdk::FbxAMatrix& fbxMatrix,
		DirectX::XMFLOAT4X4& matrix);
	void _calculateCombinedTransforms();
	long _boneNameToindex(
		const std::string& name);
	void _loadControlPointRemap(
		fbxsdk::FbxMesh* meshPtr,
		tControlPointRemap& controlPointRemap);
	void _addBoneInfluence(
		tSkinnedVerticeVector& skinnedVerticeVector,
		long vertexIndex,
		long boneIndex,
		double boneWeight);

	void _calculatePaletteMatrices();
	void _loadNodeLocalTransformMatrices(
		const fbxsdk::FbxTime& fbxTime);

	unsigned long long _getAnimationDuration();

public:
	ModelLoader();
	~ModelLoader();

	void load(
		Microsoft::WRL::ComPtr<ID3D12Device> devicePtr,
		const char* meshName,
		unsigned long boneMatrixVectorSize = 50);
	void advanceTime();
	void loadBoneMatriceVector();
	tMatrixVector m_boneMatrixVector;
	tModelVector m_modelVector;
	DirectX::XMFLOAT3 maxVertex;
	DirectX::XMFLOAT3 minVertex;
};