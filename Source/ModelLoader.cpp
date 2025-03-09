#include "ModelLoader.h"

using namespace fbxsdk;
using namespace DirectX;

template<class T>
constexpr const T& clamp(const T& v, const T& lo, const T& hi)
{
	assert(!(hi < lo));
	return (v < lo) ? lo : (hi < v) ? hi : v;
}

void writeToLogFile(
	const std::string& fileName,
	const std::string& text)
{
	FILE* fp;

	fopen_s(&fp, fileName.c_str(), "a");
	if (nullptr == fp)
	{
		return;
	}

	fprintf(fp, "%s\n", text.c_str());
	fclose(fp);
}

ModelLoader::ModelLoader()
	:m_sdkManagerPtr(nullptr)
	, m_devicePtr(nullptr)
	, m_filename()
	, m_scenePtr(nullptr)
	, m_majorFileVersion(0)
	, m_minorFileVersion(0)
	, m_revisionFileVersion(0)
	, m_modelVector()
	, m_textureName()
	, m_boneVector()
	, m_allByControlPoint(true)
	, m_boneMatrixVectorSize(0)
	, m_boneMatrixVector()
	, m_initialAnimationDurationInMs(0)
	, maxVertex(INT_MIN, INT_MIN, INT_MIN)
	, minVertex(INT_MAX, INT_MAX, INT_MAX)
{
	m_sdkManagerPtr = FbxManager::Create();
	assert(nullptr != m_sdkManagerPtr);

	fbxsdk::FbxIOSettings* ioSettingsPtr = FbxIOSettings::Create(m_sdkManagerPtr, IOSROOT);
	m_sdkManagerPtr->SetIOSettings(ioSettingsPtr);

	m_sdkManagerPtr->GetIOSettings()->SetBoolProp(IMP_FBX_CONSTRAINT, false);
	m_sdkManagerPtr->GetIOSettings()->SetBoolProp(IMP_FBX_CONSTRAINT_COUNT, false);
	m_sdkManagerPtr->GetIOSettings()->SetBoolProp(IMP_FBX_MATERIAL, true);
	m_sdkManagerPtr->GetIOSettings()->SetBoolProp(IMP_FBX_TEXTURE, true);
	m_sdkManagerPtr->GetIOSettings()->SetBoolProp(IMP_FBX_LINK, true);
	m_sdkManagerPtr->GetIOSettings()->SetBoolProp(IMP_FBX_SHAPE, true);
	m_sdkManagerPtr->GetIOSettings()->SetBoolProp(IMP_FBX_GOBO, false);
	m_sdkManagerPtr->GetIOSettings()->SetBoolProp(IMP_FBX_ANIMATION, true);
	m_sdkManagerPtr->GetIOSettings()->SetBoolProp(IMP_FBX_GLOBAL_SETTINGS, true);
}

ModelLoader::~ModelLoader()
{

	if (nullptr != m_sdkManagerPtr)
	{
		m_sdkManagerPtr->Destroy();
		m_sdkManagerPtr = nullptr;
	}
}

void ModelLoader::load(Microsoft::WRL::ComPtr<ID3D12Device> devicePtr, const char* meshName,unsigned long boneMatrixVectorSize)
{
	assert(boneMatrixVectorSize >= kTriangleVertexCount * kBoneInfluencesPerVertice);
	assert(devicePtr != nullptr);

	m_devicePtr = devicePtr;
	m_filename = meshName;
	m_boneMatrixVectorSize = boneMatrixVectorSize;

	_loadModel();

	// We just need the duration of the first animation track.
	m_initialAnimationDurationInMs = _getAnimationDuration();
}

void ModelLoader::advanceTime()
{
	fbxsdk::FbxTime fbxFrameTime;
	unsigned long long localAnimationTime = GetTickCount64() % m_initialAnimationDurationInMs;

	fbxFrameTime.SetMilliSeconds(localAnimationTime);

	_buildMatrices(fbxFrameTime);
}

void ModelLoader::_buildMatrices(
	const fbxsdk::FbxTime& fbxFrameTime)
{
	if (m_boneVector.empty())
	{
		return;
	}

	// Set the matrices that change by time and animation keys.
	_loadNodeLocalTransformMatrices(fbxFrameTime);

	// Propagate the local transform matrices from parent to child.
	_calculateCombinedTransforms();

	// This is the final pass that prepends the offset matrix.
	_calculatePaletteMatrices();
}

void ModelLoader::_loadModel()
{
	std::ifstream file(m_filename.c_str(), std::ios::binary | std::ios::ate);
	size_t fileSize = (size_t)file.tellg();
	file.seekg(0, std::ios::beg);

	std::vector<unsigned char> buffer;

	buffer.resize(size_t(fileSize), 0);

	assert(file.read((char*)buffer.data(), fileSize));

	//Create an FBX scene. This object holds the object imported from a file.
	m_scenePtr = FbxScene::Create(m_sdkManagerPtr, m_filename.c_str());

	assert(nullptr != m_scenePtr);

	FbxIOPluginRegistry* fbxIoPluginRegistry = m_sdkManagerPtr->GetIOPluginRegistry();
	tAutodeskMemoryStream stream = tAutodeskMemoryStream((char*)buffer.data(), fileSize, fbxIoPluginRegistry->FindReaderIDByExtension("fbx"));
	fbxsdk::FbxImporter* importerPtr = FbxImporter::Create(m_sdkManagerPtr, "");

	assert(nullptr != importerPtr);
	assert(importerPtr->Initialize(&stream, nullptr, -1, m_sdkManagerPtr->GetIOSettings()));
	assert(importerPtr->Import(m_scenePtr));

	importerPtr->GetFileVersion(m_majorFileVersion, m_minorFileVersion, m_revisionFileVersion);

	assert(m_scenePtr->GetSrcObjectCount<FbxConstraint>() == 0);

	FbxNode* rootNodePtr = m_scenePtr->GetRootNode();

	assert(nullptr != rootNodePtr);

	FbxAxisSystem SceneAxisSystem = m_scenePtr->GetGlobalSettings().GetAxisSystem();
	FbxAxisSystem OurAxisSystem(FbxAxisSystem::eYAxis, FbxAxisSystem::eParityOdd, FbxAxisSystem::eRightHanded);
	if (SceneAxisSystem != OurAxisSystem)
	{
		OurAxisSystem.ConvertScene(m_scenePtr);
	}

	FbxSystemUnit SceneSystemUnit = m_scenePtr->GetGlobalSettings().GetSystemUnit();
	if (SceneSystemUnit.GetScaleFactor() != 1.0)
	{
		// VERY IMPORTANT: NEED TO SCALE 
		FbxSystemUnit OurSystemUnit(100);
		OurSystemUnit.ConvertScene(m_scenePtr);
	}

	// Convert mesh, NURBS and patch into triangle mesh
	// DirectX desires triangles.
	{
		FbxGeometryConverter geometryConverter(m_sdkManagerPtr);

		assert(geometryConverter.Triangulate(
			m_scenePtr,
			true,
			false));
	}

	_loadTextureNames();

	_loadBones(m_scenePtr->GetRootNode(), kInvalidBoneIndex);
	_loadMeshes(m_scenePtr->GetRootNode());

	importerPtr->Destroy();
	importerPtr = nullptr;
}

void ModelLoader::_loadTextureNames()
{
	const long lTextureCount = m_scenePtr->GetTextureCount();
	for (long lTextureIndex = 0; lTextureIndex < lTextureCount; ++lTextureIndex)
	{
		FbxTexture* lTexture = m_scenePtr->GetTexture(lTextureIndex);
		FbxFileTexture* lFileTexture = FbxCast<FbxFileTexture>(lTexture);
		if (lFileTexture && !lFileTexture->GetUserDataPtr())
		{
			std::string fileName = lFileTexture->GetFileName();
		}
	}
}


void ModelLoader::_loadBones(
	FbxNode* nodePtr,
	long parentBoneIndex)
{
	FbxNodeAttribute* pNodeAttribute = nodePtr->GetNodeAttribute();
	long childCount = nodePtr->GetChildCount();

	if ((nullptr != pNodeAttribute)
		&& (pNodeAttribute->GetAttributeType() == FbxNodeAttribute::eSkeleton))
	{
		_loadBone(
			nodePtr,
			parentBoneIndex);

		parentBoneIndex = static_cast<long>(m_boneVector.size()) - 1;
	}

	for (long lChildIndex = 0; lChildIndex < childCount; ++lChildIndex)
	{
		_loadBones(nodePtr->GetChild(lChildIndex), parentBoneIndex);
	}
}

void ModelLoader::_loadBone(
	FbxNode* nodePtr,
	long parentBoneIndex)
{
	tBone dummy;

	m_boneVector.push_back(dummy);

	tBone& back = m_boneVector.back();

	back.boneNodePtr = nodePtr;
	back.fbxSkeletonPtr = (FbxSkeleton*)nodePtr->GetNodeAttribute();
	back.fbxClusterPtr = nullptr;
	back.name = back.fbxSkeletonPtr->GetName();
	back.parentIndex = parentBoneIndex;

	back.offset = MathHelper::Identity4x4();
	back.boneMatrice = MathHelper::Identity4x4();
	back.combinedTransform = MathHelper::Identity4x4();

	// Get our local transform at time 0.
	_getNodeLocalTransform(nodePtr, back.nodeLocalTransform);

	if (parentBoneIndex != kInvalidBoneIndex)
	{
		m_boneVector[parentBoneIndex].childIndexes.push_back(static_cast<long>(m_boneVector.size() - 1));
	}
}

void ModelLoader::_getNodeLocalTransform(
	FbxNode* nodePtr,
	DirectX::XMFLOAT4X4& matrix)
{
	FbxAMatrix fbxMatrix = m_scenePtr->GetAnimationEvaluator()->GetNodeLocalTransform(nodePtr);

	_fbxToMatrix(fbxMatrix, matrix);
}

void ModelLoader::_getNodeLocalTransform(
	FbxNode* nodePtr,
	const FbxTime& fbxTime,
	DirectX::XMFLOAT4X4& matrix)
{
	FbxAMatrix fbxMatrix = m_scenePtr->GetAnimationEvaluator()->GetNodeLocalTransform(nodePtr, fbxTime);

	_fbxToMatrix(fbxMatrix, matrix);
}

void ModelLoader::_fbxToMatrix(
	const FbxAMatrix& fbxMatrix,
	DirectX::XMFLOAT4X4& matrix)
{
	for (unsigned long i = 0; i < 4; ++i)
	{
		matrix(i, 0) = (float)fbxMatrix.Get(i, 0);
		matrix(i, 1) = (float)fbxMatrix.Get(i, 1);
		matrix(i, 2) = (float)fbxMatrix.Get(i, 2);
		matrix(i, 3) = (float)fbxMatrix.Get(i, 3);
	}
}


void ModelLoader::_loadMeshes(
	FbxNode* nodePtr)
{
	FbxNodeAttribute* pNodeAttribute = nodePtr->GetNodeAttribute();
	long childCount = nodePtr->GetChildCount();

	if ((nullptr != pNodeAttribute)
		&& (pNodeAttribute->GetAttributeType() == FbxNodeAttribute::eMesh))
	{
		_loadMesh(nodePtr);
	}

	for (long lChildIndex = 0; lChildIndex < childCount; ++lChildIndex)
	{
		_loadMeshes(nodePtr->GetChild(lChildIndex));
	}
}

void ModelLoader::_loadMesh(
	FbxNode* nodePtr)
{
	const long materialCount = nodePtr->GetMaterialCount();
	FbxNodeAttribute* nodeAttributePtr = nodePtr->GetNodeAttribute();

	{
		tModelRec emptyModelRec;
		m_modelVector.push_back(emptyModelRec);
	}
	tModelRec& modelRec = m_modelVector.back();

	modelRec.indexBufferPtr = nullptr;
	modelRec.vertexBufferPtr = nullptr;

	for (long lMaterialIndex = 0; lMaterialIndex < materialCount; ++lMaterialIndex)
	{
		FbxSurfaceMaterial* lMaterial = nodePtr->GetMaterial(lMaterialIndex);
		if (lMaterial && !lMaterial->GetUserDataPtr())
		{
			modelRec.materialName = lMaterial->GetName();
		}
	}

	if (nullptr != nodeAttributePtr)
	{
		// Bake mesh as VBO(vertex buffer object) into GPU.
		if (nodeAttributePtr->GetAttributeType() == FbxNodeAttribute::eMesh)
		{
			FbxMesh* meshPtr = nodePtr->GetMesh();
			if (nullptr != meshPtr)
			{
				long boneCount = _getBoneCount(meshPtr);

				modelRec.meshName = meshPtr->GetName();

				_loadMeshPositionNormalUV(nodePtr, modelRec);

				assert(_isMeshSkinned(meshPtr));

				// Ensure our bone matrix vector size is large enough.
				assert(m_boneMatrixVectorSize >= (unsigned int)boneCount);

				_loadMeshBoneWeightsAndIndices(nodePtr, modelRec);
				_normalizeBoneWeights(modelRec);
				_compressSkinnedVertices(modelRec);
			}
		}
	}
}

long ModelLoader::_getBoneCount(
	FbxMesh* meshPtr)
{
	if (meshPtr->GetDeformerCount(FbxDeformer::eSkin) == 0)
	{
		return 0;
	}

	return ((FbxSkin*)(meshPtr->GetDeformer(0, FbxDeformer::eSkin)))->GetClusterCount();
}

void ModelLoader::_loadMeshPositionNormalUV(
	FbxNode* nodePtr,
	tModelRec& modelRec)
{
	FbxMesh* meshPtr = nodePtr->GetMesh();

	assert(nullptr != meshPtr->GetElementMaterial());
	assert(meshPtr->GetElementNormalCount() > 0);
	assert(meshPtr->GetElementUVCount() > 0);
	assert(0 == meshPtr->GetDeformerCount(FbxDeformer::eBlendShape));
	assert(0 == meshPtr->GetDeformerCount(FbxDeformer::eVertexCache));
	assert(1 <= meshPtr->GetDeformerCount(FbxDeformer::eSkin));

	FbxGeometryElement::EMappingMode normalMappingMode = meshPtr->GetElementNormal(0)->GetMappingMode();
	FbxGeometryElement::EMappingMode uvMappingMode = meshPtr->GetElementUV(0)->GetMappingMode();
	FbxLayerElementArrayTemplate<int>* materialIndice = &meshPtr->GetElementMaterial()->GetIndexArray();

	assert(normalMappingMode != FbxGeometryElement::eNone);
	assert(uvMappingMode != FbxGeometryElement::eNone);
	assert(nullptr != materialIndice);

	const long polygonCount = meshPtr->GetPolygonCount();
	FbxGeometryElement::EMappingMode lMaterialMappingMode = meshPtr->GetElementMaterial()->GetMappingMode();
	tSkinnedVertice emptyVertice;

	ZeroMemory(&emptyVertice, sizeof(emptyVertice));

	if (normalMappingMode != FbxGeometryElement::eByControlPoint)
	{
		m_allByControlPoint = false;
	}

	if (uvMappingMode != FbxGeometryElement::eByControlPoint)
	{
		m_allByControlPoint = false;
	}

	long polygonVertexCount = meshPtr->GetControlPointsCount();

	if (!m_allByControlPoint)
	{
		polygonVertexCount = polygonCount * kTriangleVertexCount;
	}

	modelRec.indexVector.resize(polygonCount * kTriangleVertexCount);
	modelRec.verticeVector.resize(polygonVertexCount, emptyVertice);

	FbxStringList uvNames;
	meshPtr->GetUVSetNames(uvNames);
	const char* uvName = NULL;

	if (uvNames.GetCount() != 0)
	{
		uvName = uvNames[0];
	}

	const FbxVector4* controlPoints = meshPtr->GetControlPoints();
	FbxVector4 currentVertex;
	FbxVector4 currentNormal;
	FbxVector2 currentUV;
	DirectX::XMFLOAT4X4 geometryTransformMatrix;

	_getGeometryTransformMatrix(nodePtr, geometryTransformMatrix);

	DirectX::XMMATRIX geometryTransformMatrix_formated = DirectX::XMLoadFloat4x4(&geometryTransformMatrix);

	if (m_allByControlPoint)
	{
		const FbxGeometryElementNormal* lNormalElement = meshPtr->GetElementNormal(0);;
		const FbxGeometryElementUV* lUVElement = meshPtr->GetElementUV(0);

		for (long index = 0; index < polygonVertexCount; ++index)
		{
			{
				currentVertex = controlPoints[index];
				XMVECTOR pointPreProcessed = DirectX::XMVectorSet(
					static_cast<float>(currentVertex[0]),
					static_cast<float>(currentVertex[1]),
					static_cast<float>(currentVertex[2]),
					1.0f);
				XMVector3TransformCoord(pointPreProcessed, geometryTransformMatrix_formated);

				XMStoreFloat3(&(modelRec.verticeVector[index].point), pointPreProcessed);

				XMVECTOR currMax = DirectX::XMVectorSet(
					maxVertex.x,
					maxVertex.y,
					maxVertex.z,
					1.0f
				);

				XMVECTOR currMin = DirectX::XMVectorSet(
					minVertex.x,
					minVertex.y,
					minVertex.z,
					1.0f
				);

				currMax = XMVectorMax(currMax, pointPreProcessed);
				currMin = XMVectorMin(currMin, pointPreProcessed);

				XMStoreFloat3(&maxVertex, currMax);
				XMStoreFloat3(&minVertex, currMin);

			}

			{
				long lNormalIndex = index;

				if (lNormalElement->GetReferenceMode() == FbxLayerElement::eIndexToDirect)
				{
					lNormalIndex = lNormalElement->GetIndexArray().GetAt(index);
				}

				currentNormal = lNormalElement->GetDirectArray().GetAt(lNormalIndex);
				XMVECTOR normalPreProcessed = DirectX::XMVectorSet(
					static_cast<float>(currentNormal[0]),
					static_cast<float>(currentNormal[1]),
					static_cast<float>(currentNormal[2]),
					1.0f);
				XMVector3TransformCoord(normalPreProcessed, geometryTransformMatrix_formated);
				XMStoreFloat3(&(modelRec.verticeVector[index].normal), normalPreProcessed);;
			}

			{
				long uvIndex = index;

				if (lUVElement->GetReferenceMode() == FbxLayerElement::eIndexToDirect)
				{
					uvIndex = lUVElement->GetIndexArray().GetAt(index);
				}

				currentUV = lUVElement->GetDirectArray().GetAt(uvIndex);

				modelRec.verticeVector[index].tex.x = static_cast<float>(currentUV[0]);
				modelRec.verticeVector[index].tex.y = static_cast<float>(currentUV[1]);
			}
		}
	}

	long lVertexCount = 0;
	long numberOfTriangles = 0;

	for (long lPolygonIndex = 0; lPolygonIndex < polygonCount; ++lPolygonIndex)
	{
		long materialIndex = 0;
		if (materialIndice && lMaterialMappingMode == FbxGeometryElement::eByPolygon)
		{
			materialIndex = materialIndice->GetAt(lPolygonIndex);
		}

		const long indexOffset = numberOfTriangles * 3;

		for (long verticeIndex = 0; verticeIndex < kTriangleVertexCount; ++verticeIndex)
		{
			const long controlPointIndex = meshPtr->GetPolygonVertex(lPolygonIndex, verticeIndex);

			if (m_allByControlPoint)
			{
				modelRec.indexVector[indexOffset + verticeIndex] = static_cast<unsigned short>(controlPointIndex);
			}
			else
			{
				modelRec.indexVector[indexOffset + verticeIndex] = static_cast<unsigned short>(lVertexCount);

				currentVertex = controlPoints[controlPointIndex];
				XMVECTOR pointPreProcessed = DirectX::XMVectorSet(
					static_cast<float>(currentVertex[0]),
					static_cast<float>(currentVertex[1]),
					static_cast<float>(currentVertex[2]),
					1.0f);
				XMVector3TransformCoord(pointPreProcessed, geometryTransformMatrix_formated);
				XMStoreFloat3(&(modelRec.verticeVector[lVertexCount].point), pointPreProcessed);

				XMVECTOR currMax = DirectX::XMVectorSet(
					maxVertex.x,
					maxVertex.y,
					maxVertex.z,
					1.0f
				);

				XMVECTOR currMin = DirectX::XMVectorSet(
					minVertex.x,
					minVertex.y,
					minVertex.z,
					1.0f
				);

				currMax = XMVectorMax(currMax, pointPreProcessed);
				currMin = XMVectorMin(currMin, pointPreProcessed);

				XMStoreFloat3(&maxVertex, currMax);
				XMStoreFloat3(&minVertex, currMin);

				meshPtr->GetPolygonVertexNormal(lPolygonIndex, verticeIndex, currentNormal);
				XMVECTOR normalPreProcessed = DirectX::XMVectorSet(
					static_cast<float>(currentNormal[0]),
					static_cast<float>(currentNormal[1]),
					static_cast<float>(currentNormal[2]),
					1.0f);
				XMVector3TransformCoord(normalPreProcessed, geometryTransformMatrix_formated);
				XMStoreFloat3(&(modelRec.verticeVector[lVertexCount].normal), normalPreProcessed);

				{
					bool unmappedUV;

					meshPtr->GetPolygonVertexUV(lPolygonIndex, verticeIndex, uvName, currentUV, unmappedUV);
					modelRec.verticeVector[lVertexCount].tex.x = static_cast<float>(currentUV[0]);
					modelRec.verticeVector[lVertexCount].tex.y = 1.0f - static_cast<float>(currentUV[1]);
				}
			}

			++lVertexCount;
		}

		numberOfTriangles += 1;
	}
}

void ModelLoader::_getGeometryTransformMatrix(
	FbxNode* nodePtr,
	DirectX::XMFLOAT4X4& geometryOffsetMatrix)
{
	// Get the geometry offset to a node. It is never inherited by the children.
	const FbxVector4 lT = nodePtr->GetGeometricTranslation(FbxNode::eSourcePivot);
	const FbxVector4 lR = nodePtr->GetGeometricRotation(FbxNode::eSourcePivot);
	const FbxVector4 lS = nodePtr->GetGeometricScaling(FbxNode::eSourcePivot);

	FbxAMatrix fbxMatrix = FbxAMatrix(lT, lR, lS);

	_fbxToMatrix(fbxMatrix, geometryOffsetMatrix);
}

bool ModelLoader::_isMeshSkinned(
	FbxMesh* meshPtr)
{
	long boneCount = _getBoneCount(meshPtr);

	return boneCount > 0;
}

void ModelLoader::_loadMeshBoneWeightsAndIndices(
	FbxNode* nodePtr,
	tModelRec& modelRec)
{
	FbxMesh* meshPtr = nodePtr->GetMesh();
	DirectX::XMFLOAT4X4 geometryTransform;
	tControlPointRemap controlPointRemap;

	// maps control points to vertex indexes
	_loadControlPointRemap(meshPtr, controlPointRemap);

	// takes into account an offsetted model.
	_getGeometryTransformMatrix(nodePtr, geometryTransform);

	// A deformer is a FBX thing, which contains some clusters
	// A cluster contains a link, which is basically a joint
	// Normally, there is only one deformer in a mesh
	// We are using only skins, so we see if this is a skin
	FbxSkin* currSkin = (FbxSkin*)(meshPtr->GetDeformer(0, FbxDeformer::eSkin));

	long numberOfClusters = currSkin->GetClusterCount();
	for (long clusterIndex = 0; clusterIndex < numberOfClusters; ++clusterIndex)
	{
		FbxCluster* clusterPtr = currSkin->GetCluster(clusterIndex);
		long numOfIndices = clusterPtr->GetControlPointIndicesCount();
		double* weightPtr = clusterPtr->GetControlPointWeights();
		int* indicePtr = clusterPtr->GetControlPointIndices();
		std::string currJointName = clusterPtr->GetLink()->GetName();
		std::string secondName = clusterPtr->GetName();
		unsigned long boneIndex = _boneNameToindex(currJointName);
		FbxAMatrix transformMatrix;
		FbxAMatrix transformLinkMatrix;
		FbxAMatrix globalBindposeInverseMatrix;

		// Now that we have the clusterPtr, let's calculate the inverse bind pose matrix.
		clusterPtr->GetTransformLinkMatrix(transformLinkMatrix);
		clusterPtr->GetTransformMatrix(transformMatrix);
		globalBindposeInverseMatrix = transformLinkMatrix.Inverse() * transformMatrix;
		_fbxToMatrix(globalBindposeInverseMatrix, m_boneVector[boneIndex].offset);

		// Update the information in mSkeleton 
		m_boneVector[boneIndex].fbxClusterPtr = clusterPtr;

		if (nullptr == clusterPtr->GetLink())
		{
			continue;
		}

		// Associate each joint with the control points it affects
		for (long i = 0; i < numOfIndices; ++i)
		{
			double weight = weightPtr[i];

			if (weight == 0.0)
			{
				continue;
			}

			// all the points the control word is mapped to
			tControlPointIndexes& controlPointIndexes = controlPointRemap[indicePtr[i]];

			for (auto const& i : controlPointIndexes)
			{
				// Change the index vector offset, to a vertex offset.
				long vertexIndex = modelRec.indexVector[i];

				_addBoneInfluence(modelRec.verticeVector, vertexIndex, boneIndex, weight);
			}
		}
	}
}

void ModelLoader::_calculateCombinedTransforms()
{
	if (m_boneVector.empty())
	{
		return;
	}

	if (m_boneVector.size() == 1)
	{
		return;
	}

	for (auto& bone : m_boneVector)
	{
		if (bone.parentIndex != kInvalidBoneIndex)
		{
			XMMATRIX nodeLocalTransform = XMLoadFloat4x4(&bone.nodeLocalTransform);
			XMMATRIX combinedTransformLocal = XMLoadFloat4x4(&m_boneVector[bone.parentIndex].combinedTransform);
			XMMATRIX combinedTransformFinal = XMMatrixMultiply(nodeLocalTransform, combinedTransformLocal);
			XMStoreFloat4x4(&(bone.combinedTransform), combinedTransformFinal);
		}
		else
		{
			bone.combinedTransform = bone.nodeLocalTransform;
		}
	}
}

long ModelLoader::_boneNameToindex(
	const std::string& boneName)
{
	long index = 0;

	for (auto const& i : m_boneVector)
	{
		if (i.name == boneName)
		{
			return index;
		}

		++index;
	}

	assert(false);
	return kInvalidBoneIndex;
}

void ModelLoader::_addBoneInfluence(
	ModelLoader::tSkinnedVerticeVector& skinnedVerticeVector,
	long vertexIndex,
	long boneIndex,
	double boneWeight)
{
	unsigned long integerWeight = clamp(static_cast<unsigned long>(boneWeight * 255.0 + 0.5), unsigned long(0), unsigned long(255));

	if (0 == integerWeight)
	{
		return;
	}

	tPackedInt packedIndices;
	tPackedInt packedWeights;
	unsigned long smallestWeightIndex = 0xFFFFFFFF;
	unsigned long smallestWeight = 0xFFFFFFFF;

	packedIndices.number = skinnedVerticeVector[vertexIndex].boneIndices;
	packedWeights.number = skinnedVerticeVector[vertexIndex].boneWeights;

	for (unsigned long i = 0; i < 4; ++i)
	{
		unsigned long packedWeight = packedWeights.bytes[i];

		if (packedWeight == 0)
		{
			smallestWeightIndex = i;
			smallestWeight = 0;
		}
		else if ((packedWeight < boneWeight)
			&& (packedWeight < smallestWeight))
		{
			smallestWeightIndex = i;
			smallestWeight = packedWeight;
		}

		if (packedIndices.bytes[i] == boneIndex)
		{
			return;
		}
	}

	if (smallestWeightIndex == 0xFFFFFFFF)
	{
		// We had more than four weights, and this weight is the smallest.
		return;
	}

	packedIndices.bytes[smallestWeightIndex] = static_cast<unsigned char>(boneIndex);
	packedWeights.bytes[smallestWeightIndex] = static_cast<unsigned char>(integerWeight);
	skinnedVerticeVector[vertexIndex].boneIndices = packedIndices.number;
	skinnedVerticeVector[vertexIndex].boneWeights = packedWeights.number;
}

void ModelLoader::_calculatePaletteMatrices()
{
	for (auto& bone : m_boneVector)
	{
		XMMATRIX offset = XMLoadFloat4x4(&bone.offset);
		XMMATRIX combinedTransform = XMLoadFloat4x4(&bone.combinedTransform);
		XMMATRIX boneMatrice = XMMatrixMultiply(offset, combinedTransform);
		XMStoreFloat4x4(&(bone.boneMatrice), XMMatrixTranspose(boneMatrice));
	}
}

void ModelLoader::_loadNodeLocalTransformMatrices(
	const FbxTime& fbxTime)
{
	for (unsigned long i = 0; i < m_boneVector.size(); ++i)
	{
		_getNodeLocalTransform(m_boneVector[i].boneNodePtr, fbxTime, m_boneVector[i].nodeLocalTransform);
	}
}

void ModelLoader::_normalizeBoneWeights(
	tModelRec& modelRec)
{
	for (auto& vertice : modelRec.verticeVector)
	{
		tPackedInt packedWeights;

		packedWeights.number = vertice.boneWeights;

		{
			unsigned long totalWeight =
				packedWeights.bytes[0]
				+ packedWeights.bytes[1]
				+ packedWeights.bytes[2]
				+ packedWeights.bytes[3];

			assert(totalWeight != 0);

			if ((totalWeight >= 254)
				&& (totalWeight <= 256))
			{ // no need to normalize
				continue;
			}
		}

		float weights[4];
		float calculation[4];

		weights[0] = static_cast<float>(packedWeights.bytes[0]);
		weights[1] = static_cast<float>(packedWeights.bytes[1]);
		weights[2] = static_cast<float>(packedWeights.bytes[2]);
		weights[3] = static_cast<float>(packedWeights.bytes[3]);

		float totalWeight = weights[0] + weights[1] + weights[2] + weights[3];

		calculation[0] = 255.0f * (weights[0] / totalWeight) + 0.5f;
		calculation[1] = 255.0f * (weights[1] / totalWeight) + 0.5f;
		calculation[2] = 255.0f * (weights[2] / totalWeight) + 0.5f;
		calculation[3] = 255.0f * (weights[3] / totalWeight) + 0.5f;

		packedWeights.bytes[0] = static_cast<unsigned char>(calculation[0]);
		packedWeights.bytes[1] = static_cast<unsigned char>(calculation[1]);
		packedWeights.bytes[2] = static_cast<unsigned char>(calculation[2]);
		packedWeights.bytes[3] = static_cast<unsigned char>(calculation[3]);

		vertice.boneWeights = packedWeights.number;
	}
}


// This function re-indexes the vertex buffer and makes it smaller.
void ModelLoader::_compressSkinnedVertices(
	ModelLoader::tModelRec& modelRec)
{
	tSkinnedVerticeVector newVertices;
	unsigned short foundIndice;

	for (auto& i : modelRec.indexVector)
	{
		tSkinnedVertice currentSkinnedVertice = modelRec.verticeVector[i];

		foundIndice = _findSkinnedVertice(newVertices, currentSkinnedVertice);

		if (foundIndice == 0xFFFF) // not found
		{
			i = static_cast<unsigned short>(newVertices.size());
			newVertices.push_back(currentSkinnedVertice);
		}
		else
		{
			i = foundIndice;
		}
	}

	modelRec.verticeVector.swap(newVertices);
}

void ModelLoader::_loadControlPointRemap(
	FbxMesh* meshPtr,
	ModelLoader::tControlPointRemap& controlPointRemap)
{
	const long lPolygonCount = meshPtr->GetPolygonCount();

	controlPointRemap.resize(lPolygonCount);

	for (long lPolygonIndex = 0; lPolygonIndex < lPolygonCount; lPolygonIndex++)
	{
		long lPolygonSize = meshPtr->GetPolygonSize(lPolygonIndex);

		for (long lVertexIndex = 0; lVertexIndex < lPolygonSize; lVertexIndex++)
		{
			long lControlPointIndex = meshPtr->GetPolygonVertex(lPolygonIndex, lVertexIndex);

			controlPointRemap[lControlPointIndex].push_back(lPolygonIndex * kTriangleVertexCount + lVertexIndex);
		}
	}
}

unsigned short  ModelLoader::_findSkinnedVertice(
	const  ModelLoader::tSkinnedVerticeVector& skinnedVerticeVector,
	const  ModelLoader::tSkinnedVertice& skinnedVertice)
{
	for (size_t i = 0; i < skinnedVerticeVector.size(); ++i)
	{
		XMVECTOR chosenPoint = XMLoadFloat3(&(skinnedVertice.point));
		XMVECTOR pointInVector = XMLoadFloat3(&(skinnedVerticeVector[i].point));
		XMVECTOR chosenNormal = XMLoadFloat3(&(skinnedVertice.normal));
		XMVECTOR normalInVector = XMLoadFloat3(&(skinnedVerticeVector[i].normal));

		if ((XMVector3Equal(chosenPoint, pointInVector))
			&& (XMVector3Equal(chosenNormal, normalInVector))
			&& (skinnedVertice.tex.x == skinnedVerticeVector[i].tex.x)
			&& (skinnedVertice.tex.y == skinnedVerticeVector[i].tex.y))
		{
			return static_cast<unsigned short>(i);
		}
	}

	return 0xFFFF;
}

void ModelLoader::loadBoneMatriceVector()
{
	m_boneMatrixVector.clear();

	for (const auto& bone : m_boneVector)
	{
		m_boneMatrixVector.push_back(bone.boneMatrice);
	}

}

unsigned long long ModelLoader::_getAnimationDuration()
{
	FbxArray<FbxString*> animStackNameArray;
	FbxAnimStack* fbxAnimStackPtr = m_scenePtr->GetSrcObject<FbxAnimStack>(0);

	assert(nullptr != fbxAnimStackPtr);

	m_scenePtr->FillAnimStackNameArray(animStackNameArray);

	int animStackNameArraySize = animStackNameArray.GetCount();

	assert(animStackNameArraySize >= 1);

	FbxTakeInfo* currentTakeInfoPtr = m_scenePtr->GetTakeInfo(*animStackNameArray[0]);

	assert(nullptr != currentTakeInfoPtr);

	return currentTakeInfoPtr->mLocalTimeSpan.GetStop().GetMilliSeconds()
		- currentTakeInfoPtr->mLocalTimeSpan.GetStart().GetMilliSeconds();
}
