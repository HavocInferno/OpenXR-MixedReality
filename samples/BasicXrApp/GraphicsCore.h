//*********************************************************
//    Copyright (c) Microsoft. All rights reserved.
//
//    Apache 2.0 License
//
//    You may obtain a copy of the License at
//    http://www.apache.org/licenses/LICENSE-2.0
//
//    Unless required by applicable law or agreed to in writing, software
//    distributed under the License is distributed on an "AS IS" BASIS,
//    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
//    implied. See the License for the specific language governing
//    permissions and limitations under the License.
//
//*********************************************************

#pragma once

#include "pch.h"
#include "OpenXrProgram.h"
#include "Eigen/Geometry"

namespace CubeShader {
    struct Vertex {
        XrVector3f Position;
        XrVector3f Color;
    };

    constexpr XrVector3f Red{1, 0, 0};
    constexpr XrVector3f DarkRed{0.25f, 0, 0};
    constexpr XrVector3f Green{0, 1, 0};
    constexpr XrVector3f DarkGreen{0, 0.25f, 0};
    constexpr XrVector3f Blue{0, 0, 1};
    constexpr XrVector3f DarkBlue{0, 0, 0.25f};

    // Vertices for a 1x1x1 meter cube. (Left/Right, Top/Bottom, Front/Back)
    constexpr XrVector3f LBB{-0.5f, -0.5f, -0.5f};
    constexpr XrVector3f LBF{-0.5f, -0.5f, 0.5f};
    constexpr XrVector3f LTB{-0.5f, 0.5f, -0.5f};
    constexpr XrVector3f LTF{-0.5f, 0.5f, 0.5f};
    constexpr XrVector3f RBB{0.5f, -0.5f, -0.5f};
    constexpr XrVector3f RBF{0.5f, -0.5f, 0.5f};
    constexpr XrVector3f RTB{0.5f, 0.5f, -0.5f};
    constexpr XrVector3f RTF{0.5f, 0.5f, 0.5f};

#define CUBE_SIDE(V1, V2, V3, V4, V5, V6, COLOR) {V1, COLOR}, {V2, COLOR}, {V3, COLOR}, {V4, COLOR}, {V5, COLOR}, {V6, COLOR},

    constexpr Vertex c_cubeVertices[] = {
        CUBE_SIDE(LTB, LBF, LBB, LTB, LTF, LBF, DarkRed)   // -X
        CUBE_SIDE(RTB, RBB, RBF, RTB, RBF, RTF, Red)       // +X
        CUBE_SIDE(LBB, LBF, RBF, LBB, RBF, RBB, DarkGreen) // -Y
        CUBE_SIDE(LTB, RTB, RTF, LTB, RTF, LTF, Green)     // +Y
        CUBE_SIDE(LBB, RBB, RTB, LBB, RTB, LTB, DarkBlue)  // -Z
        CUBE_SIDE(LBF, LTF, RTF, LBF, RTF, RBF, Blue)      // +Z
    };

    // Winding order is clockwise. Each side uses a different color.
    constexpr unsigned short c_cubeIndices[] = {
        0,  1,  2,  3,  4,  5,  // -X
        6,  7,  8,  9,  10, 11, // +X
        12, 13, 14, 15, 16, 17, // -Y
        18, 19, 20, 21, 22, 23, // +Y
        24, 25, 26, 27, 28, 29, // -Z
        30, 31, 32, 33, 34, 35, // +Z
    };

    struct ModelConstantBuffer {
        DirectX::XMFLOAT4X4 Model;
    };

    struct ViewProjectionConstantBuffer {
        DirectX::XMFLOAT4X4 ViewProjection[2];
    };

    constexpr uint32_t MaxViewInstance = 2;

    // Separate entrypoints for the vertex and pixel shader functions.
    constexpr char ShaderHlsl[] = R"_(
        struct VSOutput {
            float4 Pos : SV_POSITION;
            float3 Color : COLOR0;
            uint viewId : SV_RenderTargetArrayIndex;
        };
        struct VSInput {
            float3 Pos : POSITION;
            float3 Color : COLOR0;
            uint instId : SV_InstanceID;
        };
        cbuffer ModelConstantBuffer : register(b0) {
            float4x4 Model;
        };
        cbuffer ViewProjectionConstantBuffer : register(b1) {
            float4x4 ViewProjection[2];
        };

        VSOutput MainVS(VSInput input) {
            VSOutput output;
            output.Pos = mul(mul(float4(input.Pos, 1), Model), ViewProjection[input.instId]);
            output.Color = input.Color;
            output.viewId = input.instId;
            return output;
        }

        float4 MainPS(VSOutput input) : SV_TARGET {
            return float4(input.Color, 1);
        }
        )_";

} // namespace CubeShader

// Slots in the RenderTargetView descriptor heap
enum RTVIndex_t { RTV_LEFT_EYE = 0, RTV_RIGHT_EYE, NUM_RTVS };

// Slots in the ConstantBufferView/ShaderResourceView descriptor heap
enum CBVSRVIndex_t {
    CBV_LEFT_EYE = 0,
    CBV_RIGHT_EYE,
    SRV_LEFT_EYE,
    SRV_RIGHT_EYE,
    SRV_TEXTURE_MAP,
    // Slot for texture in each possible render model
    SRV_TEXTURE_RENDER_MODEL0,
    // SRV_TEXTURE_RENDER_MODEL_MAX = SRV_TEXTURE_RENDER_MODEL0 + vr::k_unMaxTrackedDeviceCount, //TODO: replace vr->xr
    // Slot for transform in each possible rendermodel
    CBV_LEFT_EYE_RENDER_MODEL0,
    // CBV_LEFT_EYE_RENDER_MODEL_MAX = CBV_LEFT_EYE_RENDER_MODEL0 + vr::k_unMaxTrackedDeviceCount, //TODO: replace vr->xr
    CBV_RIGHT_EYE_RENDER_MODEL0,
    // CBV_RIGHT_EYE_RENDER_MODEL_MAX = CBV_RIGHT_EYE_RENDER_MODEL0 + vr::k_unMaxTrackedDeviceCount, //TODO: replace vr->xr
    NUM_SRV_CBVS
};

struct VertexDataScene {
    Eigen::Vector3f position;
    Eigen::Vector2f texCoord;
};

class GraphicsCore : public sample::IGraphicsPluginD3D12 {
public:
    // Inherited via IGraphicsPluginD3D12
    ID3D12Device* InitializeD3D12(LUID adapterLuid, std::unique_ptr<sample::IOpenXrProgram::RenderResources>& renderresc) override;

    void InitializeResources2(std::unique_ptr<sample::IOpenXrProgram::RenderResources>& renderresc) override; 

    const std::vector<DXGI_FORMAT>& SupportedColorFormats() const override;

    const std::vector<DXGI_FORMAT>& SupportedDepthFormats() const override; 

    virtual void RenderView(const XrRect2Di& imageRect,
                            const float renderTargetClearColor[4],
                            const std::vector<xr::math::ViewProjection>& viewProjections,
                            DXGI_FORMAT colorSwapchainFormat,
                            ID3D12Resource* colorTexture,
                            DXGI_FORMAT depthSwapchainFormat,
                            ID3D12Resource* depthTexture,
                            const std::vector<const sample::Cube*>& cubes,
                            CD3DX12_CPU_DESCRIPTOR_HANDLE colorHandle,
                            CD3DX12_CPU_DESCRIPTOR_HANDLE depthHandle) override;
    bool SetStereoFramebufferHandles(unsigned int viewCount,
                                     unsigned int swapchainIndex,
                                     ID3D12Resource* framebufferColorTexture,
                                     DXGI_FORMAT framebufferColorFormat,
                                     CD3DX12_CPU_DESCRIPTOR_HANDLE& renderTargetViewHandle,
                                     ID3D12Resource* framebufferDepthStencil,
                                     DXGI_FORMAT framebufferDepthStencilFormat,
                                     CD3DX12_CPU_DESCRIPTOR_HANDLE& depthStencilViewHandle) override;


private:
    bool InitializeD3D12Device(LUID adapterLuid);

    // D3D resource setup functions
    bool InitializeD3DResources(std::unique_ptr<sample::IOpenXrProgram::RenderResources>& renderresc);

    bool SetupTexturemaps();
    bool SetupScene();
    bool SetupCameras();
    bool SetupStereoRenderTargets();
    bool SetupRenderModels();

    bool GenMipMapRGBA(const UINT8* pSrc, UINT8** ppDst, int nSrcWidth, int nSrcHeight, int* pDstWidthOut, int* pDstHeightOut); 
    bool AddCubeVertex(float fl0, float fl1, float fl2, float fl3, float fl4, std::vector<float>& vertdata); 
    bool AddCubeToScene(Eigen::Matrix4f mat, std::vector<float>& vertdata); 

    bool RenderScene(int eyeIndex); 
    bool RenderStereoTargets(const XrRect2Di& imageRect,
                             ID3D12Resource* colorTexture,
                             ID3D12Resource* depthTexture,
                             CD3DX12_CPU_DESCRIPTOR_HANDLE colorHandle,
                             CD3DX12_CPU_DESCRIPTOR_HANDLE depthHandle);

private:
    winrt::com_ptr<ID3D12Resource> m_pSceneVertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_sceneVertexBufferView;
    winrt::com_ptr<ID3D12Resource> m_pTexture;
    winrt::com_ptr<ID3D12Resource> m_pTextureUploadHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE m_textureShaderResourceView;

    // cube array   // TODO: add to app init (or just graphics init?)
    int m_iSceneVolumeInit = 20;
    int m_iSceneVolumeWidth = m_iSceneVolumeInit;
    int m_iSceneVolumeHeight = m_iSceneVolumeInit;
    int m_iSceneVolumeDepth = m_iSceneVolumeInit;

    float m_fScale = 0.3f;
    float m_fScaleSpacing = 4.0f;
    //
    // float m_fNearClip = 0.1f;
    // float m_fFarClip = 30.0f;

    unsigned int m_uiVertcount = 0;

    Eigen::Vector4f m_debugClearColor = {1.0f, 0.0f, 0.0f, 1.0f};
};
