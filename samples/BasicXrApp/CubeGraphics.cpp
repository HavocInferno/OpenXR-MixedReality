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

#include "pch.h"
#include "OpenXrProgram.h"
#include "DxUtility.h"

namespace {
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
    enum RTVIndex_t { 
        RTV_LEFT_EYE = 0, 
        RTV_RIGHT_EYE, 
        RTV_SWAPCHAIN0, 
        RTV_SWAPCHAIN1, 
        NUM_RTVS 
    };

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

    struct CubeGraphics : sample::IGraphicsPluginD3D12 {
        bool InitializeD3D12Device(LUID adapterLuid) {
            /* 
            adapted from Valve HelloVR DX12 sample 
            */

            UINT nDXGIFactoryFlags = 0;

            // Debug layers if -dxdebug is specified
            if (m_bDebugD3D12) {
                winrt::com_ptr<ID3D12Debug> pDebugController;
                if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(pDebugController.put())))) {
                    pDebugController->EnableDebugLayer();
                    nDXGIFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
                }
            }

            winrt::com_ptr<IDXGIFactory4> pFactory;
            if (FAILED(CreateDXGIFactory2(nDXGIFactoryFlags, IID_PPV_ARGS(pFactory.put())))) {
                sample::dx::dprintf("CreateDXGIFactory2 failed.\n");
                return false;
            }

            // Query OpenXR for the output adapter index
            winrt::com_ptr<IDXGIAdapter1> pAdapter = sample::dx::GetAdapter(adapterLuid);
            
            DXGI_ADAPTER_DESC1 adapterDesc;
            pAdapter->GetDesc1(&adapterDesc);

            if (FAILED(D3D12CreateDevice(pAdapter.get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(m_pDevice.put())))) {
                sample::dx::dprintf("Failed to create D3D12 device with D3D12CreateDevice.\n");
                return false;
            }

            return true; 
        }

        ID3D12Device* InitializeD3D12(LUID adapterLuid) override {
            bool ret = InitializeD3D12Device(adapterLuid); 
            ret = InitializeD3DResources();

            return m_pDevice.get();
        }

        bool InitializeD3DResources() {
            //CHECK_MSG(options.VPAndRTArrayIndexFromAnyShaderFeedingRasterizer,
            //          "This sample requires VPRT support. Adjust sample shaders on GPU without VRPT.");



            /*
            adapted from Valve HelloVR DX12 sample
            */

            // Create the command queue
            D3D12_COMMAND_QUEUE_DESC queueDesc = {};
            queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
            queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            if (FAILED(m_pDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(m_pCommandQueue.put())))) {
                printf("Failed to create D3D12 command queue.\n");
                return false;
            }

            // Create the swapchain //TODO: exclude companion window items for now
            /*DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
            swapChainDesc.BufferCount = g_nFrameCount;
            swapChainDesc.Width = m_nCompanionWindowWidth;
            swapChainDesc.Height = m_nCompanionWindowHeight;
            swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            swapChainDesc.SampleDesc.Count = 1;*/

            // Determine the HWND from SDL //TODO: exclude companion window items for now
            /*struct SDL_SysWMinfo wmInfo;
            SDL_VERSION(&wmInfo.version);
            SDL_GetWindowWMInfo(m_pCompanionWindow, &wmInfo);
            HWND hWnd = wmInfo.info.win.window;

            ComPtr<IDXGISwapChain1> pSwapChain;
            if (FAILED(pFactory->CreateSwapChainForHwnd(m_pCommandQueue.Get(), hWnd, &swapChainDesc, nullptr, nullptr, &pSwapChain))) {
                dprintf("Failed to create DXGI swapchain.\n");
                return false;
            }

            pFactory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);
            pSwapChain.As(&m_pSwapChain);
            m_nFrameIndex = m_pSwapChain->GetCurrentBackBufferIndex();*/

            // Create descriptor heaps
            {
                m_nRTVDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
                m_nDSVDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
                m_nCBVSRVDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

                D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
                rtvHeapDesc.NumDescriptors = NUM_RTVS;
                rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
                rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
                m_pDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(m_pRTVHeap.put()));

                D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
                rtvHeapDesc.NumDescriptors = NUM_RTVS;
                rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
                rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
                m_pDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(m_pDSVHeap.put()));

                D3D12_DESCRIPTOR_HEAP_DESC cbvSrvHeapDesc = {};
                cbvSrvHeapDesc.NumDescriptors = NUM_SRV_CBVS;
                cbvSrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                cbvSrvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                m_pDevice->CreateDescriptorHeap(&cbvSrvHeapDesc, IID_PPV_ARGS(m_pCBVSRVHeap.put()));
            }

            // Create per-frame resources 
            for (int nFrame = 0; nFrame < g_nFrameCount; nFrame++) {
                if (FAILED(
                        m_pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(m_pCommandAllocators[nFrame].put())))) {
                    sample::dx::dprintf("Failed to create command allocators.\n");
                    return false;
                }

                // TODO: exclude companion window items for now
                /*
                // Create swapchain render targets
                m_pSwapChain->GetBuffer(nFrame, IID_PPV_ARGS(&m_pSwapChainRenderTarget[nFrame]));

                // Create swapchain render target views
                CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_pRTVHeap->GetCPUDescriptorHandleForHeapStart());
                rtvHandle.Offset(RTV_SWAPCHAIN0 + nFrame, m_nRTVDescriptorSize);
                m_pDevice->CreateRenderTargetView(m_pSwapChainRenderTarget[nFrame].Get(), nullptr, rtvHandle);
                */
            }

            // Create constant buffer
            {
                auto heap_props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD); 
                auto resource_desc = CD3DX12_RESOURCE_DESC::Buffer(1024 * 64); 
                m_pDevice->CreateCommittedResource(&heap_props,
                                                   D3D12_HEAP_FLAG_NONE,
                                                   &resource_desc,
                                                   D3D12_RESOURCE_STATE_GENERIC_READ,
                                                   nullptr,
                                                   IID_PPV_ARGS(m_pSceneConstantBuffer.put()));

                // Keep as persistently mapped buffer, store left eye in first 256 bytes, right eye in second
                UINT8* pBuffer;
                CD3DX12_RANGE readRange(0, 0);
                m_pSceneConstantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pBuffer));
                // Left eye to first 256 bytes, right eye to second 256 bytes
                m_pSceneConstantBufferData[0] = pBuffer;
                m_pSceneConstantBufferData[1] = pBuffer + 256;

                // Left eye CBV
                CD3DX12_CPU_DESCRIPTOR_HANDLE cbvLeftEyeHandle(m_pCBVSRVHeap->GetCPUDescriptorHandleForHeapStart());
                cbvLeftEyeHandle.Offset(CBV_LEFT_EYE, m_nCBVSRVDescriptorSize);
                D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
                cbvDesc.BufferLocation = m_pSceneConstantBuffer->GetGPUVirtualAddress();
                cbvDesc.SizeInBytes = (sizeof(DirectX::XMMATRIX) + 255) & ~255; // Pad to 256 bytes
                m_pDevice->CreateConstantBufferView(&cbvDesc, cbvLeftEyeHandle);
                m_sceneConstantBufferView[0] = cbvLeftEyeHandle;

                // Right eye CBV
                CD3DX12_CPU_DESCRIPTOR_HANDLE cbvRightEyeHandle(m_pCBVSRVHeap->GetCPUDescriptorHandleForHeapStart());
                cbvRightEyeHandle.Offset(CBV_RIGHT_EYE, m_nCBVSRVDescriptorSize);
                cbvDesc.BufferLocation += 256;
                m_pDevice->CreateConstantBufferView(&cbvDesc, cbvRightEyeHandle);
                m_sceneConstantBufferView[1] = cbvRightEyeHandle;
            }

            // Create fence 
            {
                memset(m_nFenceValues, 0, sizeof(m_nFenceValues));
                m_pDevice->CreateFence(m_nFenceValues[m_nFrameIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_pFence.put()));
                m_nFenceValues[m_nFrameIndex]++;

                m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            }

            if (!sample::dx::CreateAllShaders(m_pDevice,
                                              m_pRootSignature,
                                              m_nMSAASampleCount,
                                              m_pScenePipelineState,
                                              m_pAxesPipelineState,
                                              m_pRenderModelPipelineState))
                return false;

            // Create command list
            m_pDevice->CreateCommandList(0,
                                         D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         m_pCommandAllocators[m_nFrameIndex].get(),
                                         m_pScenePipelineState.get(),
                                         IID_PPV_ARGS(m_pCommandList.put()));

            // SetupTexturemaps(); //TODO: adapt from CMainApplication
            // SetupScene(); //TODO: adapt from CMainApplication
            // SetupCameras(); //TODO: adapt from CMainApplication
            // SetupStereoRenderTargets(); //TODO: adapt from CMainApplication
            // SetupCompanionWindow(); //TODO: adapt from CMainApplication
            // SetupRenderModels(); //TODO: adapt from CMainApplication

            // Do any work that was queued up during loading
            m_pCommandList->Close();
            ID3D12CommandList* ppCommandLists[] = {m_pCommandList.get()};
            m_pCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

            // Wait for it to finish
            m_pCommandQueue->Signal(m_pFence.get(), m_nFenceValues[m_nFrameIndex]);
            m_pFence->SetEventOnCompletion(m_nFenceValues[m_nFrameIndex], m_fenceEvent);
            WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
            m_nFenceValues[m_nFrameIndex]++;

            return true;
        }

        const std::vector<DXGI_FORMAT>& SupportedColorFormats() const override {
            const static std::vector<DXGI_FORMAT> SupportedColorFormats = {
                DXGI_FORMAT_R8G8B8A8_UNORM,
                DXGI_FORMAT_B8G8R8A8_UNORM,
                DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
                DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
            };
            return SupportedColorFormats;
        }

        const std::vector<DXGI_FORMAT>& SupportedDepthFormats() const override {
            const static std::vector<DXGI_FORMAT> SupportedDepthFormats = {
                DXGI_FORMAT_D32_FLOAT,
                DXGI_FORMAT_D16_UNORM,
                DXGI_FORMAT_D24_UNORM_S8_UINT,
                DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
            };
            return SupportedDepthFormats;
        }

        /*void RenderView(const XrRect2Di& imageRect,
                        const float renderTargetClearColor[4],
                        const std::vector<xr::math::ViewProjection>& viewProjections,
                        DXGI_FORMAT colorSwapchainFormat,
                        ID3D11Texture2D* colorTexture,
                        DXGI_FORMAT depthSwapchainFormat,
                        ID3D11Texture2D* depthTexture,
                        const std::vector<const sample::Cube*>& cubes) override {
            const uint32_t viewInstanceCount = (uint32_t)viewProjections.size();
            CHECK_MSG(viewInstanceCount <= CubeShader::MaxViewInstance,
                      "Sample shader supports 2 or fewer view instances. Adjust shader to accommodate more.")

            CD3D11_VIEWPORT viewport(
                (float)imageRect.offset.x, (float)imageRect.offset.y, (float)imageRect.extent.width, (float)imageRect.extent.height);
            m_deviceContext->RSSetViewports(1, &viewport);

            // Create RenderTargetView with the original swapchain format (swapchain image is typeless).
            winrt::com_ptr<ID3D11RenderTargetView> renderTargetView;
            const CD3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc(D3D11_RTV_DIMENSION_TEXTURE2DARRAY, colorSwapchainFormat);
            CHECK_HRCMD(m_device->CreateRenderTargetView(colorTexture, &renderTargetViewDesc, renderTargetView.put()));

            // Create a DepthStencilView with the original swapchain format (swapchain image is typeless)
            winrt::com_ptr<ID3D11DepthStencilView> depthStencilView;
            CD3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc(D3D11_DSV_DIMENSION_TEXTURE2DARRAY, depthSwapchainFormat);
            CHECK_HRCMD(m_device->CreateDepthStencilView(depthTexture, &depthStencilViewDesc, depthStencilView.put()));

            const bool reversedZ = viewProjections[0].NearFar.Near > viewProjections[0].NearFar.Far;
            const float depthClearValue = reversedZ ? 0.f : 1.f;

            // Clear swapchain and depth buffer. NOTE: This will clear the entire render target view, not just the specified view.
            m_deviceContext->ClearRenderTargetView(renderTargetView.get(), renderTargetClearColor);
            m_deviceContext->ClearDepthStencilView(depthStencilView.get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, depthClearValue, 0);
            m_deviceContext->OMSetDepthStencilState(reversedZ ? m_reversedZDepthNoStencilTest.get() : nullptr, 0);

            ID3D11RenderTargetView* renderTargets[] = {renderTargetView.get()};
            m_deviceContext->OMSetRenderTargets((UINT)std::size(renderTargets), renderTargets, depthStencilView.get());

            ID3D11Buffer* const constantBuffers[] = {m_modelCBuffer.get(), m_viewProjectionCBuffer.get()};
            m_deviceContext->VSSetConstantBuffers(0, (UINT)std::size(constantBuffers), constantBuffers);
            m_deviceContext->VSSetShader(m_vertexShader.get(), nullptr, 0);
            m_deviceContext->PSSetShader(m_pixelShader.get(), nullptr, 0);

            CubeShader::ViewProjectionConstantBuffer viewProjectionCBufferData{};

            for (uint32_t k = 0; k < viewInstanceCount; k++) {
                const DirectX::XMMATRIX spaceToView = xr::math::LoadInvertedXrPose(viewProjections[k].Pose);
                const DirectX::XMMATRIX projectionMatrix = ComposeProjectionMatrix(viewProjections[k].Fov, viewProjections[k].NearFar);

                // Set view projection matrix for each view, transpose for shader usage.
                DirectX::XMStoreFloat4x4(&viewProjectionCBufferData.ViewProjection[k],
                                         DirectX::XMMatrixTranspose(spaceToView * projectionMatrix));
            }
            m_deviceContext->UpdateSubresource(m_viewProjectionCBuffer.get(), 0, nullptr, &viewProjectionCBufferData, 0, 0);

            // Set cube primitive data.
            const UINT strides[] = {sizeof(CubeShader::Vertex)};
            const UINT offsets[] = {0};
            ID3D11Buffer* vertexBuffers[] = {m_cubeVertexBuffer.get()};
            m_deviceContext->IASetVertexBuffers(0, (UINT)std::size(vertexBuffers), vertexBuffers, strides, offsets);
            m_deviceContext->IASetIndexBuffer(m_cubeIndexBuffer.get(), DXGI_FORMAT_R16_UINT, 0);
            m_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            m_deviceContext->IASetInputLayout(m_inputLayout.get());

            // Render each cube
            for (const sample::Cube* cube : cubes) {
                // Compute and update the model transform for each cube, transpose for shader usage.
                CubeShader::ModelConstantBuffer model;
                const DirectX::XMMATRIX scaleMatrix = DirectX::XMMatrixScaling(cube->Scale.x, cube->Scale.y, cube->Scale.z);
                DirectX::XMStoreFloat4x4(&model.Model, DirectX::XMMatrixTranspose(scaleMatrix * xr::math::LoadXrPose(cube->PoseInScene)));
                m_deviceContext->UpdateSubresource(m_modelCBuffer.get(), 0, nullptr, &model, 0, 0);

                // Draw the cube.
                m_deviceContext->DrawIndexedInstanced((UINT)std::size(CubeShader::c_cubeIndices), viewInstanceCount, 0, 0, 0);
            }
        }*/

    private:
        /*winrt::com_ptr<ID3D12Device> m_device;
        winrt::com_ptr<ID3D11DeviceContext> m_deviceContext;
        winrt::com_ptr<ID3D11VertexShader> m_vertexShader;
        winrt::com_ptr<ID3D11PixelShader> m_pixelShader;
        winrt::com_ptr<ID3D11InputLayout> m_inputLayout;
        winrt::com_ptr<ID3D11Buffer> m_modelCBuffer;
        winrt::com_ptr<ID3D11Buffer> m_viewProjectionCBuffer;
        winrt::com_ptr<ID3D11Buffer> m_cubeVertexBuffer;
        winrt::com_ptr<ID3D11Buffer> m_cubeIndexBuffer;
        winrt::com_ptr<ID3D11DepthStencilState> m_reversedZDepthNoStencilTest;*/
    };
} // namespace

namespace sample {
    std::unique_ptr<sample::IGraphicsPluginD3D12> CreateCubeGraphics() {
        return std::make_unique<CubeGraphics>();
    }
} // namespace sample
