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
#include "GraphicsPluginD3D12.h"

namespace sample {
    class IOpenXrProgram {
    public: 
        virtual ~IOpenXrProgram() = default;
        virtual void Run() = 0;

        struct SwapchainD3D12 {
            xr::SwapchainHandle Handle;
            DXGI_FORMAT Format{DXGI_FORMAT_UNKNOWN};
            uint32_t Width{0};
            uint32_t Height{0};
            uint32_t ArraySize{0};
            std::vector<XrSwapchainImageD3D12KHR> Images;
            std::vector<CD3DX12_CPU_DESCRIPTOR_HANDLE> ViewHandles;
        };

        struct RenderResources {
            XrViewState ViewState{XR_TYPE_VIEW_STATE};
            std::vector<XrView> Views;
            std::vector<XrViewConfigurationView> ConfigViews;
            SwapchainD3D12 ColorSwapchain;
            SwapchainD3D12 DepthSwapchain;
            std::vector<XrCompositionLayerProjectionView> ProjectionLayerViews;
            std::vector<XrCompositionLayerDepthInfoKHR> DepthInfoViews;
        };
    };

    std::unique_ptr<IGraphicsPluginD3D12> CreateGraphicsCore();
    std::unique_ptr<IOpenXrProgram> CreateOpenXrProgram(std::string applicationName, std::unique_ptr<IGraphicsPluginD3D12> graphicsPlugin);

} // namespace sample
