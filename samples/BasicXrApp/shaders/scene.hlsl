
// Vertex Shader
struct VS_INPUT
{
	float3 vPosition : POSITION;
	float2 vUVCoords: TEXCOORD0;
    uint instId : SV_InstanceID;
};

struct PS_INPUT
{
	float4 vPosition : SV_POSITION;
	float2 vUVCoords : TEXCOORD0;
    uint viewId : SV_RenderTargetArrayIndex;
};

cbuffer SceneConstantBuffer : register(b0)
{
	float4x4 g_MVPMatrix;
};
cbuffer ViewProjectionConstantBuffer : register(b1) {
    float4x4 ViewProjection[2];
};

SamplerState g_SamplerState : register(s0);
Texture2D g_Texture : register(t0);


PS_INPUT VSMain( VS_INPUT i )
{
	PS_INPUT o;
	//o.vPosition = mul( g_MVPMatrix, float4( i.vPosition, 1.0 ) );
	o.vPosition = mul(mul(float4(i.vPosition, 1), g_MVPMatrix), ViewProjection[i.instId]);
#ifdef VULKAN
	o.vPosition.y = -o.vPosition.y;
#endif
	o.vUVCoords = i.vUVCoords;
    o.viewId = i.instId; 
	return o;
}

float4 PSMain( PS_INPUT i ) : SV_TARGET
{
	float4 vColor = g_Texture.Sample( g_SamplerState, i.vUVCoords );
	return vColor;
}
