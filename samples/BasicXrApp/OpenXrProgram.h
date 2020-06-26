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

namespace sample {
    struct Cube {
        xr::SpaceHandle Space{};
        std::optional<XrPosef> PoseInSpace{}; // Cube pose in above Space. Default to identity.
        XrVector3f Scale{0.1f, 0.1f, 0.1f};

        XrPosef PoseInScene = xr::math::Pose::Identity(); // Cube pose in the scene.  Got updated every frame
    };

    struct IOpenXrProgram {
        virtual ~IOpenXrProgram() = default;
        virtual void Run() = 0;
    };

    struct IGraphicsPluginD3D12 {
        virtual ~IGraphicsPluginD3D12() = default;

        // Create an instance of this graphics api for the provided instance and systemId.
        virtual ID3D12Device* InitializeD3D12(LUID adapterLuid) = 0;

        // List of color pixel formats supported by this app.
        virtual const std::vector<DXGI_FORMAT>& SupportedColorFormats() const = 0;
        virtual const std::vector<DXGI_FORMAT>& SupportedDepthFormats() const = 0;

        // Render to swapchain images using stereo image array
        /*virtual void RenderView(const XrRect2Di& imageRect,
                                const float renderTargetClearColor[4],
                                const std::vector<xr::math::ViewProjection>& viewProjections,
                                DXGI_FORMAT colorSwapchainFormat,
                                ID3D11Texture2D* colorTexture,
                                DXGI_FORMAT depthSwapchainFormat,
                                ID3D11Texture2D* depthTexture,
                                const std::vector<const sample::Cube*>& cubes) = 0;*/

    public:
        winrt::com_ptr<ID3D12Device> m_pDevice;
        winrt::com_ptr<ID3D12CommandQueue> m_pCommandQueue;
        bool m_bDebugD3D12 = true;
        int m_nMSAASampleCount = 1;
        UINT m_nFrameIndex;
        static const int g_nFrameCount = 2; // Swapchain depth //TODO: exclude companion window items for now, this too?
        // uint32_t m_nCompanionWindowWidth; //TODO: exclude companion window items for now
        // uint32_t m_nCompanionWindowHeight; //TODO: exclude companion window items for now
        UINT m_nRTVDescriptorSize;
        UINT m_nDSVDescriptorSize;
        UINT m_nCBVSRVDescriptorSize;
        winrt::com_ptr<ID3D12DescriptorHeap> m_pCBVSRVHeap;
        winrt::com_ptr<ID3D12DescriptorHeap> m_pRTVHeap;
        winrt::com_ptr<ID3D12DescriptorHeap> m_pDSVHeap;
        winrt::com_ptr<ID3D12Resource> m_pSceneConstantBuffer;
        UINT8* m_pSceneConstantBufferData[2];
        D3D12_CPU_DESCRIPTOR_HANDLE m_sceneConstantBufferView[2];
        winrt::com_ptr<ID3D12CommandAllocator> m_pCommandAllocators[g_nFrameCount];
        winrt::com_ptr<ID3D12PipelineState> m_pScenePipelineState;
        winrt::com_ptr<ID3D12GraphicsCommandList> m_pCommandList;
        winrt::com_ptr<ID3D12Fence> m_pFence;
        UINT64 m_nFenceValues[g_nFrameCount];
        HANDLE m_fenceEvent;
    };

    std::unique_ptr<IGraphicsPluginD3D12> CreateCubeGraphics();
    std::unique_ptr<IOpenXrProgram> CreateOpenXrProgram(std::string applicationName, std::unique_ptr<IGraphicsPluginD3D12> graphicsPlugin);

} // namespace sample
