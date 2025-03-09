#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 3
#endif

#ifndef NUM_POINT_LIGHTS
    #define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
    #define NUM_SPOT_LIGHTS 0
#endif

#define CONE_OF_SHAME 0.2f

#include "Common.hlsl"

struct VertexIn
{
	float3 PosL    : POSITION;
    float3 NormalL : NORMAL;
	float2 TexC    : TEXCOORD;
#ifdef SKINNED
    uint4 BoneWeights : WEIGHTS;
    uint4 BoneIndices : BONEINDICES;
#endif
};

struct VertexOut
{
	float4 PosH    : SV_POSITION;
    float3 PosW    : POSITION;
    float3 NormalW : NORMAL;
	float2 TexC    : TEXCOORD;
};

VertexOut VS(VertexIn vin)
{
	VertexOut vout = (VertexOut)0.0f;

#ifdef SKINNED
    float3 gForce = float3(0.0f, -5.0f, 0.0f);
    float boneWeights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    boneWeights[0] = vin.BoneWeights.x / 255.0f;
    boneWeights[1] = vin.BoneWeights.y / 255.0f;
    boneWeights[2] = vin.BoneWeights.z / 255.0f;
    boneWeights[3] = vin.BoneWeights.w / 255.0f;
    
    float4 inPos = float4(vin.PosL, 1.0f);
    float3 inNorm = vin.NormalL;
    float3 blendedPosition = float3(0.0f, 0.0f, 0.0f);
    float3 blendedNorm = float3(0.0f, 0.0f, 0.0f);

    for (int indice = 0; indice < 4; ++indice)
    {
        float normalizedBoneWeight = boneWeights[indice];
        float4x4 boneMatrix = gBoneMatrices[vin.BoneIndices[indice]];
        blendedPosition += normalizedBoneWeight * mul(inPos, boneMatrix).xyz;
        blendedNorm += normalizedBoneWeight * mul(inNorm, (float3x3)boneMatrix);
    }

    vin.PosL = blendedPosition;
    vin.NormalL = blendedNorm;
#endif

	MaterialData matData = gMaterialData[gMaterialIndex];

    vout.NormalW = mul(vin.NormalL, (float3x3)gWorld);

#ifdef SKINNED
    float3 normalizedForceVector =
        normalize(gFurLength * vout.NormalW
            + gForce * gStiffness);
    float3 forceVector = normalizedForceVector * gFurLength;
    float forceNormalDot = dot(normalizedForceVector, vout.NormalW);

    // This is a simple check to see if the hair is going to protrude into the skin.
    if (forceNormalDot < CONE_OF_SHAME)
    {
        if (forceNormalDot == -1.0f)
        {
            forceVector = float3(0.0f, 0.0f, 0.0f);
        }
        else
        {
            // Enforces our cone of shame for force vectors
            float3 correctedForceVector =
                normalize(forceVector + (vout.NormalW - forceVector) * ((CONE_OF_SHAME - forceNormalDot) * 0.5f));

            // The force is scaled by the distance to the end of the hair.
            forceVector = correctedForceVector * gFurLength;
        }
    }
#endif
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
#ifdef SKINNED
    vout.PosW = posW.xyz + forceVector;
#else
    vout.PosW = posW.xyz;
#endif
    vout.PosH = mul(float4(vout.PosW, 1.0), gViewProj);
	
	float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
	vout.TexC = mul(texC, matData.MatTransform).xy;
	
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
	MaterialData matData = gMaterialData[gMaterialIndex];
	float4 diffuseAlbedo = matData.DiffuseAlbedo;
	float3 fresnelR0 = matData.FresnelR0;
	float  roughness = matData.Roughness;
	uint diffuseTexIndex = matData.DiffuseMapIndex;

	diffuseAlbedo *= gDiffuseMap[diffuseTexIndex].Sample(gsamAnisotropicWrap, pin.TexC);
    pin.NormalW = normalize(pin.NormalW);

    float furLength = gFurLength;

    float3 toEyeW = normalize(gEyePosW - pin.PosW);

    float4 ambient = gAmbientLight*diffuseAlbedo* furLength;
	const float shininess = 1.0f - roughness;

    Material mat = { diffuseAlbedo, fresnelR0, shininess };
    float3 shadowFactor = 1.0f;
    float4 directLight = ComputeLighting(gLights, mat, pin.PosW,
        pin.NormalW, toEyeW, shadowFactor);

    float4 litColor = ambient + directLight;

	float3 r = reflect(-toEyeW, pin.NormalW);
	float4 reflectionColor = gCubeMap.Sample(gsamLinearWrap, r);
	float3 fresnelFactor = SchlickFresnel(fresnelR0, pin.NormalW, r);
	litColor.rgb += shininess * fresnelFactor * reflectionColor.rgb;

    litColor.a = diffuseAlbedo.a;

    return litColor;
}

float4 PS_fur(VertexOut pin) : SV_Target
{
    MaterialData matData = gMaterialData[gMaterialIndex];
    float4 diffuseAlbedo = matData.DiffuseAlbedo;
    float3 fresnelR0 = matData.FresnelR0;
    float  roughness = matData.Roughness;
    uint diffuseTexIndex = matData.DiffuseMapIndex;

    diffuseAlbedo *= gDiffuseMap[diffuseTexIndex].Sample(gsamAnisotropicWrap, pin.TexC);
    float4 furTexture = gDiffuseMap[3].Sample(gsamAnisotropicWrap, (pin.TexC * 4.0f));
    float4 furStencilTexture = gDiffuseMap[4].Sample(gsamAnisotropicWrap, pin.TexC);
    
    pin.NormalW = normalize(pin.NormalW);

    float3 toEyeW = normalize(gEyePosW - pin.PosW);

    float4 ambient = gAmbientLight * diffuseAlbedo;

    const float shininess = 1.0f - roughness;
    Material mat = { diffuseAlbedo, fresnelR0, shininess };
    float3 shadowFactor = gShadowFactor;
    float4 directLight = ComputeLighting(gLights, mat, pin.PosW,
        pin.NormalW, toEyeW, shadowFactor);

    float4 litColor = ambient + directLight;

    float3 r = reflect(-toEyeW, pin.NormalW);
    float4 reflectionColor = gCubeMap.Sample(gsamLinearWrap, r);
    float3 fresnelFactor = SchlickFresnel(fresnelR0, pin.NormalW, r);
    litColor.rgb += shininess * fresnelFactor * reflectionColor.rgb;

    if (!gFurIndex) {
        litColor.a = diffuseAlbedo.a;
    }
    else {
        litColor.a = furTexture.a * gAlphaDecay * furStencilTexture.r;
    }
    

    return litColor;
}
