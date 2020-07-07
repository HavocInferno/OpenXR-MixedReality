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

namespace sample {
    struct Cube {
        xr::SpaceHandle Space{};
        std::optional<XrPosef> PoseInSpace{}; // Cube pose in above Space. Default to identity.
        XrVector3f Scale{0.1f, 0.1f, 0.1f};

        XrPosef PoseInScene = xr::math::Pose::Identity(); // Cube pose in the scene.  Got updated every frame
    };

    class IGraphicsPluginD3D12 {
    public: 
        virtual ~IGraphicsPluginD3D12() = default;

        // Create an instance of this graphics api for the provided instance and systemId.
        virtual ID3D12Device* InitializeD3D12(LUID adapterLuid) = 0; 

        // List of color pixel formats supported by this app.
        virtual const std::vector<DXGI_FORMAT>& SupportedColorFormats() const = 0;
        virtual const std::vector<DXGI_FORMAT>& SupportedDepthFormats() const = 0;

        // set D3D12 RenderTarget/DepthStencil view handles for given framebuffer
        virtual bool SetStereoFramebufferHandles(unsigned int viewCount,
                                                 unsigned int swapchainIndex,
                                                 ID3D12Resource* framebufferColorTexture,
                                                 DXGI_FORMAT framebufferColorFormat,
                                                 CD3DX12_CPU_DESCRIPTOR_HANDLE& renderTargetViewHandle,
                                                 ID3D12Resource* framebufferDepthStencil,
                                                 DXGI_FORMAT framebufferDepthStencilFormat,
                                                 CD3DX12_CPU_DESCRIPTOR_HANDLE& depthStencilViewHandle) = 0;

        // Render to swapchain images using stereo image array
        virtual void RenderView(const XrRect2Di& imageRect,
                                const float renderTargetClearColor[4],
                                const std::vector<xr::math::ViewProjection>& viewProjections,
                                DXGI_FORMAT colorSwapchainFormat,
                                ID3D12Resource* colorTexture,
                                DXGI_FORMAT depthSwapchainFormat,
                                ID3D12Resource* depthTexture,
                                const std::vector<const sample::Cube*>& cubes,
                                CD3DX12_CPU_DESCRIPTOR_HANDLE colorHandle,
                                CD3DX12_CPU_DESCRIPTOR_HANDLE depthHandle) = 0;

        virtual void SetClearColor(Eigen::Vector4f& newcol) = 0;

    public:
        winrt::com_ptr<ID3D12Device> m_pDevice;
        winrt::com_ptr<ID3D12CommandQueue> m_pCommandQueue;
#ifdef _DEBUG
        bool m_bDebugD3D12 = true;
#else
        bool m_bDebugD3D12 = false; 
#endif // DEBUG
        int m_nMSAASampleCount = 1;
        UINT m_nFrameIndex;
        // uint32_t m_nCompanionWindowWidth; //TODO: exclude companion window items for now
        // uint32_t m_nCompanionWindowHeight; //TODO: exclude companion window items for now
        static const int m_maxSwapchainLength = 3;
        static int m_actualSwapchainLength;
        UINT m_nRTVDescriptorSize;
        UINT m_nDSVDescriptorSize;
        UINT m_nCBVSRVDescriptorSize;
        winrt::com_ptr<ID3D12DescriptorHeap> m_pCBVSRVHeap;
        winrt::com_ptr<ID3D12DescriptorHeap> m_pRTVHeap;
        winrt::com_ptr<ID3D12DescriptorHeap> m_pDSVHeap;
        winrt::com_ptr<ID3D12Resource> m_pSceneConstantBuffer;
        UINT8* m_pSceneConstantBufferData[2];
        D3D12_CPU_DESCRIPTOR_HANDLE m_sceneConstantBufferView[2];
        winrt::com_ptr<ID3D12CommandAllocator> m_pCommandAllocators[m_maxSwapchainLength];
        winrt::com_ptr<ID3D12GraphicsCommandList> m_pCommandList;
        winrt::com_ptr<ID3D12Fence> m_pFence;
        UINT64 m_nFenceValues[m_maxSwapchainLength];
        HANDLE m_fenceEvent;

        winrt::com_ptr<ID3D12RootSignature> m_pRootSignature;
        winrt::com_ptr<ID3D12PipelineState> m_pScenePipelineState;
        winrt::com_ptr<ID3D12PipelineState> m_pCompanionPipelineState;
        winrt::com_ptr<ID3D12PipelineState> m_pAxesPipelineState;
        winrt::com_ptr<ID3D12PipelineState> m_pRenderModelPipelineState;
    };
} // namespace sample
