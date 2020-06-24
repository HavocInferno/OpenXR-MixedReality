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

//-----------------------------------------------------------------------------
// Purpose: Outputs a set of optional arguments to debugging output, using
//          the printf format setting specified in fmt*.
//-----------------------------------------------------------------------------
static bool g_bPrintf = true;
void dprintf(const char* fmt, ...) {
    va_list args;
    char buffer[2048];

    va_start(args, fmt);
    vsprintf_s(buffer, fmt, args);
    va_end(args);

    if (g_bPrintf)
        printf("%s", buffer);

    OutputDebugStringA(buffer);
}

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
    //SRV_TEXTURE_RENDER_MODEL_MAX = SRV_TEXTURE_RENDER_MODEL0 + vr::k_unMaxTrackedDeviceCount, //TODO: replace vr->xr
    // Slot for transform in each possible rendermodel
    CBV_LEFT_EYE_RENDER_MODEL0,
    //CBV_LEFT_EYE_RENDER_MODEL_MAX = CBV_LEFT_EYE_RENDER_MODEL0 + vr::k_unMaxTrackedDeviceCount, //TODO: replace vr->xr
    CBV_RIGHT_EYE_RENDER_MODEL0,
    //CBV_RIGHT_EYE_RENDER_MODEL_MAX = CBV_RIGHT_EYE_RENDER_MODEL0 + vr::k_unMaxTrackedDeviceCount, //TODO: replace vr->xr
    NUM_SRV_CBVS
};

namespace {
    struct ImplementOpenXrProgram : sample::IOpenXrProgram {
        ImplementOpenXrProgram(std::string applicationName, std::unique_ptr<sample::IGraphicsPluginD3D12> graphicsPlugin)
            : m_applicationName(std::move(applicationName))
            , m_graphicsPlugin(std::move(graphicsPlugin)) {
        }

        void Run() override {
            CreateInstance();
            CreateActions();

            bool requestRestart = false;
            do {
                InitializeSystem();
                InitializeD3D12();
                InitializeSession();

                while (true) {
                    bool exitRenderLoop = false;
                    ProcessEvents(&exitRenderLoop, &requestRestart);
                    if (exitRenderLoop) {
                        break;
                    }

                    if (m_sessionRunning) {
                        PollActions();
                        RenderFrame();
                    } else {
                        // Throttle loop since xrWaitFrame won't be called.
                        using namespace std::chrono_literals;
                        std::this_thread::sleep_for(250ms);
                    }
                }

                if (requestRestart) {
                    PrepareSessionRestart();
                }
            } while (requestRestart);
        }

    private:
        void CreateInstance() {
            CHECK(m_instance.Get() == XR_NULL_HANDLE);

            // Build out the extensions to enable. Some extensions are required and some are optional.
            const std::vector<const char*> enabledExtensions = SelectExtensions();

            // Create the instance with desired extensions.
            XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
            createInfo.enabledExtensionCount = (uint32_t)enabledExtensions.size();
            createInfo.enabledExtensionNames = enabledExtensions.data();

            createInfo.applicationInfo = {"BasicXrApp", 1, "", 1, XR_CURRENT_API_VERSION};
            strcpy_s(createInfo.applicationInfo.applicationName, m_applicationName.c_str());
            CHECK_XRCMD(xrCreateInstance(&createInfo, m_instance.Put()));

            m_extensions.PopulateDispatchTable(m_instance.Get());
        }

        std::vector<const char*> SelectExtensions() {
            // Fetch the list of extensions supported by the runtime.
            uint32_t extensionCount;
            CHECK_XRCMD(xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr));
            std::vector<XrExtensionProperties> extensionProperties(extensionCount, {XR_TYPE_EXTENSION_PROPERTIES});
            CHECK_XRCMD(xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, extensionProperties.data()));

            std::vector<const char*> enabledExtensions;

            // Add a specific extension to the list of extensions to be enabled, if it is supported.
            auto EnableExtentionIfSupported = [&](const char* extensionName) {
                for (uint32_t i = 0; i < extensionCount; i++) {
                    if (strcmp(extensionProperties[i].extensionName, extensionName) == 0) {
                        enabledExtensions.push_back(extensionName);
                        return true;
                    }
                }
                return false;
            };

            // D3D12 extension is required for this sample, so check if it's supported.
            CHECK(EnableExtentionIfSupported(XR_KHR_D3D12_ENABLE_EXTENSION_NAME));

            // Additional optional extensions for enhanced functionality. Track whether enabled in m_optionalExtensions.
            m_optionalExtensions.DepthExtensionSupported = EnableExtentionIfSupported(XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME);
            m_optionalExtensions.UnboundedRefSpaceSupported = EnableExtentionIfSupported(XR_MSFT_UNBOUNDED_REFERENCE_SPACE_EXTENSION_NAME);
            m_optionalExtensions.SpatialAnchorSupported = EnableExtentionIfSupported(XR_MSFT_SPATIAL_ANCHOR_EXTENSION_NAME);

            return enabledExtensions;
        }

        void CreateActions() {
            CHECK(m_instance.Get() != XR_NULL_HANDLE);

            // Create an action set.
            {
                XrActionSetCreateInfo actionSetInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
                strcpy_s(actionSetInfo.actionSetName, "place_hologram_action_set");
                strcpy_s(actionSetInfo.localizedActionSetName, "Placement");
                CHECK_XRCMD(xrCreateActionSet(m_instance.Get(), &actionSetInfo, m_actionSet.Put()));
            }

            // Create actions.
            {
                // Enable subaction path filtering for left or right hand.
                m_subactionPaths[LeftSide] = GetXrPath("/user/hand/left");
                m_subactionPaths[RightSide] = GetXrPath("/user/hand/right");

                // Create an input action to place a hologram.
                {
                    XrActionCreateInfo actionInfo{XR_TYPE_ACTION_CREATE_INFO};
                    actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
                    strcpy_s(actionInfo.actionName, "place_hologram");
                    strcpy_s(actionInfo.localizedActionName, "Place Hologram");
                    actionInfo.countSubactionPaths = (uint32_t)m_subactionPaths.size();
                    actionInfo.subactionPaths = m_subactionPaths.data();
                    CHECK_XRCMD(xrCreateAction(m_actionSet.Get(), &actionInfo, m_placeAction.Put()));
                }

                // Create an input action getting the left and right hand poses.
                {
                    XrActionCreateInfo actionInfo{XR_TYPE_ACTION_CREATE_INFO};
                    actionInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;
                    strcpy_s(actionInfo.actionName, "hand_pose");
                    strcpy_s(actionInfo.localizedActionName, "Hand Pose");
                    actionInfo.countSubactionPaths = (uint32_t)m_subactionPaths.size();
                    actionInfo.subactionPaths = m_subactionPaths.data();
                    CHECK_XRCMD(xrCreateAction(m_actionSet.Get(), &actionInfo, m_poseAction.Put()));
                }

                // Create an output action for vibrating the left and right controller.
                {
                    XrActionCreateInfo actionInfo{XR_TYPE_ACTION_CREATE_INFO};
                    actionInfo.actionType = XR_ACTION_TYPE_VIBRATION_OUTPUT;
                    strcpy_s(actionInfo.actionName, "vibrate");
                    strcpy_s(actionInfo.localizedActionName, "Vibrate");
                    actionInfo.countSubactionPaths = (uint32_t)m_subactionPaths.size();
                    actionInfo.subactionPaths = m_subactionPaths.data();
                    CHECK_XRCMD(xrCreateAction(m_actionSet.Get(), &actionInfo, m_vibrateAction.Put()));
                }

                // Create an input action to exit session
                {
                    XrActionCreateInfo actionInfo{XR_TYPE_ACTION_CREATE_INFO};
                    actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
                    strcpy_s(actionInfo.actionName, "exit_session");
                    strcpy_s(actionInfo.localizedActionName, "Exit session");
                    actionInfo.countSubactionPaths = (uint32_t)m_subactionPaths.size();
                    actionInfo.subactionPaths = m_subactionPaths.data();
                    CHECK_XRCMD(xrCreateAction(m_actionSet.Get(), &actionInfo, m_exitAction.Put()));
                }
            }

            // Setup suggest bindings for simple controller.
            {
                std::vector<XrActionSuggestedBinding> bindings;
                bindings.push_back({m_placeAction.Get(), GetXrPath("/user/hand/right/input/select/click")});
                bindings.push_back({m_placeAction.Get(), GetXrPath("/user/hand/left/input/select/click")});
                bindings.push_back({m_poseAction.Get(), GetXrPath("/user/hand/right/input/grip/pose")});
                bindings.push_back({m_poseAction.Get(), GetXrPath("/user/hand/left/input/grip/pose")});
                bindings.push_back({m_vibrateAction.Get(), GetXrPath("/user/hand/right/output/haptic")});
                bindings.push_back({m_vibrateAction.Get(), GetXrPath("/user/hand/left/output/haptic")});
                bindings.push_back({m_exitAction.Get(), GetXrPath("/user/hand/right/input/menu/click")});
                bindings.push_back({m_exitAction.Get(), GetXrPath("/user/hand/left/input/menu/click")});

                XrInteractionProfileSuggestedBinding suggestedBindings{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
                suggestedBindings.interactionProfile = GetXrPath("/interaction_profiles/khr/simple_controller");
                suggestedBindings.suggestedBindings = bindings.data();
                suggestedBindings.countSuggestedBindings = (uint32_t)bindings.size();
                CHECK_XRCMD(xrSuggestInteractionProfileBindings(m_instance.Get(), &suggestedBindings));
            }
        }

        void InitializeSystem() {
            CHECK(m_instance.Get() != XR_NULL_HANDLE);
            CHECK(m_systemId == XR_NULL_SYSTEM_ID);

            XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
            systemInfo.formFactor = m_formFactor;
            while (true) {
                XrResult result = xrGetSystem(m_instance.Get(), &systemInfo, &m_systemId);
                if (SUCCEEDED(result)) {
                    break;
                } else if (result == XR_ERROR_FORM_FACTOR_UNAVAILABLE) {
                    DEBUG_PRINT("No headset detected.  Trying again in one second...");
                    using namespace std::chrono_literals;
                    std::this_thread::sleep_for(1s);
                } else {
                    CHECK_XRRESULT(result, "xrGetSystem");
                }
            };

            // Choose an environment blend mode.
            {
                // Query the list of supported environment blend modes for the current system
                uint32_t count;
                CHECK_XRCMD(xrEnumerateEnvironmentBlendModes(m_instance.Get(), m_systemId, m_primaryViewConfigType, 0, &count, nullptr));
                CHECK(count > 0); // A system must support at least one environment blend mode.

                std::vector<XrEnvironmentBlendMode> environmentBlendModes(count);
                CHECK_XRCMD(xrEnumerateEnvironmentBlendModes(
                    m_instance.Get(), m_systemId, m_primaryViewConfigType, count, &count, environmentBlendModes.data()));

                // This sample supports all modes, pick the system's preferred one.
                m_environmentBlendMode = environmentBlendModes[0];
            }

            // Choose a reasonable depth range can help improve hologram visual quality.
            // Use reversed Z (near > far) for more uniformed Z resolution.
            m_nearFar = {20.f, 0.1f};
        }

        bool InitializeD3D12() {
            // TODO: missing vars 
            //  -> move these to CubeGraphics.cpp private, 
            //  then move this device init code to CubeGraphics::InitializeDevice and 
            //  the D3D12 resource init to CubeGraphics::InitializeD3DResources
            winrt::com_ptr<ID3D12Device> m_pDevice;
            winrt::com_ptr<ID3D12CommandQueue> m_pCommandQueue;
            bool m_bDebugD3D12;
            UINT m_nFrameIndex;
            static const int g_nFrameCount = 2; // Swapchain depth //TODO: exclude companion window items for now, this too?
            //uint32_t m_nCompanionWindowWidth; //TODO: exclude companion window items for now
            //uint32_t m_nCompanionWindowHeight; //TODO: exclude companion window items for now
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



            // adapted from Valve HelloVR DX12 sample
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
                dprintf("CreateDXGIFactory2 failed.\n");
                return false;
            }

            // Query OpenXR for the output adapter index
            int32_t nAdapterIndex = 0;
            XrGraphicsRequirementsD3D12KHR graphicsRequirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR}; 
            CHECK_XRCMD(m_extensions.xrGetD3D12GraphicsRequirementsKHR(m_instance.Get(), m_systemId, &graphicsRequirements));
            winrt::com_ptr<IDXGIAdapter1> pAdapter = sample::dx::GetAdapter(graphicsRequirements.adapterLuid);
            if (FAILED(pFactory->EnumAdapters1(nAdapterIndex, pAdapter.put()))) {
                dprintf("Error enumerating DXGI adapter.\n");
            }
            DXGI_ADAPTER_DESC1 adapterDesc;
            pAdapter->GetDesc1(&adapterDesc);

            if (FAILED(D3D12CreateDevice(pAdapter.get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(m_pDevice.put())))) {
                dprintf("Failed to create D3D12 device with D3D12CreateDevice.\n");
                return false;
            }

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

            // Create per-frame resources //TODO: exclude companion window items for now
            /*for (int nFrame = 0; nFrame < g_nFrameCount; nFrame++) {
                if (FAILED(
                        m_pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_pCommandAllocators[nFrame])))) {
                    dprintf("Failed to create command allocators.\n");
                    return false;
                }

                // Create swapchain render targets
                m_pSwapChain->GetBuffer(nFrame, IID_PPV_ARGS(&m_pSwapChainRenderTarget[nFrame]));

                // Create swapchain render target views
                CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_pRTVHeap->GetCPUDescriptorHandleForHeapStart());
                rtvHandle.Offset(RTV_SWAPCHAIN0 + nFrame, m_nRTVDescriptorSize);
                m_pDevice->CreateRenderTargetView(m_pSwapChainRenderTarget[nFrame].Get(), nullptr, rtvHandle);
            }*/

            // Create constant buffer 
            {
                m_pDevice->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
                                                   D3D12_HEAP_FLAG_NONE,
                                                   &CD3DX12_RESOURCE_DESC::Buffer(1024 * 64),
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

            // Create fence //TODO: exclude companion window items for now
            /*{
                memset(m_nFenceValues, 0, sizeof(m_nFenceValues));
                m_pDevice->CreateFence(m_nFenceValues[m_nFrameIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_pFence));
                m_nFenceValues[m_nFrameIndex]++;

                m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            }*/

            if (!sample::dx::CreateAllShaders())
                return false;

            // Create command list
            m_pDevice->CreateCommandList(0,
                                         D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         m_pCommandAllocators[m_nFrameIndex].get(),
                                         m_pScenePipelineState.get(),
                                         IID_PPV_ARGS(m_pCommandList.put()));

            //SetupTexturemaps(); //TODO: adapt from CMainApplication
            //SetupScene(); //TODO: adapt from CMainApplication
            //SetupCameras(); //TODO: adapt from CMainApplication
            //SetupStereoRenderTargets(); //TODO: adapt from CMainApplication
            //SetupCompanionWindow(); //TODO: adapt from CMainApplication
            //SetupRenderModels(); //TODO: adapt from CMainApplication

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

        void InitializeSession() {
            CHECK(m_instance.Get() != XR_NULL_HANDLE);
            CHECK(m_systemId != XR_NULL_SYSTEM_ID);
            CHECK(m_session.Get() == XR_NULL_HANDLE);

            // Create the D3D11 device for the adapter associated with the system.
            XrGraphicsRequirementsD3D12KHR graphicsRequirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR}; //TODO: dx12
            CHECK_XRCMD(m_extensions.xrGetD3D12GraphicsRequirementsKHR(m_instance.Get(), m_systemId, &graphicsRequirements));

            // Create a list of feature levels which are both supported by the OpenXR runtime and this application.
            std::vector<D3D_FEATURE_LEVEL> featureLevels = {
                D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
            featureLevels.erase(std::remove_if(featureLevels.begin(),
                                               featureLevels.end(),
                                               [&](D3D_FEATURE_LEVEL fl) { return fl < graphicsRequirements.minFeatureLevel; }),
                                featureLevels.end());
            CHECK_MSG(featureLevels.size() != 0, "Unsupported minimum feature level!");

            ID3D12Device* device; // = m_graphicsPlugin->InitializeDevice(graphicsRequirements.adapterLuid, featureLevels); // TODO: dx12

            XrGraphicsBindingD3D12KHR graphicsBinding{XR_TYPE_GRAPHICS_BINDING_D3D12_KHR}; //TODO: dx12
            graphicsBinding.device = device;

            XrSessionCreateInfo createInfo{XR_TYPE_SESSION_CREATE_INFO};
            createInfo.next = &graphicsBinding;
            createInfo.systemId = m_systemId;
            CHECK_XRCMD(xrCreateSession(m_instance.Get(), &createInfo, m_session.Put()));

            XrSessionActionSetsAttachInfo attachInfo{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
            std::vector<XrActionSet> actionSets = {m_actionSet.Get()};
            attachInfo.countActionSets = (uint32_t)actionSets.size();
            attachInfo.actionSets = actionSets.data();
            CHECK_XRCMD(xrAttachSessionActionSets(m_session.Get(), &attachInfo));

            CreateSpaces();
            CreateSwapchains();
        }

        void CreateSpaces() {
            CHECK(m_session.Get() != XR_NULL_HANDLE);

            // Create a scene space to bridge interactions and all holograms.
            {
                if (m_optionalExtensions.UnboundedRefSpaceSupported) {
                    // Unbounded reference space provides the best scene space for world-scale experiences.
                    m_sceneSpaceType = XR_REFERENCE_SPACE_TYPE_UNBOUNDED_MSFT;
                } else {
                    // If running on a platform that does not support world-scale experiences, fall back to local space.
                    m_sceneSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
                }

                XrReferenceSpaceCreateInfo spaceCreateInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
                spaceCreateInfo.referenceSpaceType = m_sceneSpaceType;
                spaceCreateInfo.poseInReferenceSpace = xr::math::Pose::Identity();
                CHECK_XRCMD(xrCreateReferenceSpace(m_session.Get(), &spaceCreateInfo, m_sceneSpace.Put()));
            }

            // Create a space for each hand pointer pose.
            for (uint32_t side : {LeftSide, RightSide}) {
                XrActionSpaceCreateInfo createInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
                createInfo.action = m_poseAction.Get();
                createInfo.poseInActionSpace = xr::math::Pose::Identity();
                createInfo.subactionPath = m_subactionPaths[side];
                CHECK_XRCMD(xrCreateActionSpace(m_session.Get(), &createInfo, m_cubesInHand[side].Space.Put()));
            }
        }

        std::tuple<DXGI_FORMAT, DXGI_FORMAT> SelectSwapchainPixelFormats() {
            CHECK(m_session.Get() != XR_NULL_HANDLE);

            // Query runtime preferred swapchain formats.
            uint32_t swapchainFormatCount;
            CHECK_XRCMD(xrEnumerateSwapchainFormats(m_session.Get(), 0, &swapchainFormatCount, nullptr));

            std::vector<int64_t> swapchainFormats(swapchainFormatCount);
            CHECK_XRCMD(xrEnumerateSwapchainFormats(
                m_session.Get(), (uint32_t)swapchainFormats.size(), &swapchainFormatCount, swapchainFormats.data()));

            // Choose the first runtime preferred format that this app supports.
            auto SelectPixelFormat = [](const std::vector<int64_t>& runtimePreferredFormats,
                                        const std::vector<DXGI_FORMAT>& applicationSupportedFormats) {
                auto found = std::find_first_of(std::begin(runtimePreferredFormats),
                                                std::end(runtimePreferredFormats),
                                                std::begin(applicationSupportedFormats),
                                                std::end(applicationSupportedFormats));
                if (found == std::end(runtimePreferredFormats)) {
                    THROW("No runtime swapchain format is supported.");
                }
                return (DXGI_FORMAT)*found;
            };

            DXGI_FORMAT colorSwapchainFormat = SelectPixelFormat(swapchainFormats, m_graphicsPlugin->SupportedColorFormats());
            DXGI_FORMAT depthSwapchainFormat = SelectPixelFormat(swapchainFormats, m_graphicsPlugin->SupportedDepthFormats());

            return {colorSwapchainFormat, depthSwapchainFormat};
        }

        void CreateSwapchains() {
            CHECK(m_session.Get() != XR_NULL_HANDLE);
            CHECK(m_renderResources == nullptr);

            m_renderResources = std::make_unique<RenderResources>(); //TODO: dx12

            // Read graphics properties for preferred swapchain length and logging.
            XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
            CHECK_XRCMD(xrGetSystemProperties(m_instance.Get(), m_systemId, &systemProperties));

            // Select color and depth swapchain pixel formats
            const auto [colorSwapchainFormat, depthSwapchainFormat] = SelectSwapchainPixelFormats(); //TODO: dx12?

            // Query and cache view configuration views.
            uint32_t viewCount;
            CHECK_XRCMD(xrEnumerateViewConfigurationViews(m_instance.Get(), m_systemId, m_primaryViewConfigType, 0, &viewCount, nullptr));
            CHECK(viewCount == m_stereoViewCount);

            m_renderResources->ConfigViews.resize(viewCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
            CHECK_XRCMD(xrEnumerateViewConfigurationViews(
                m_instance.Get(), m_systemId, m_primaryViewConfigType, viewCount, &viewCount, m_renderResources->ConfigViews.data()));

            // Using texture array for better performance, but requiring left/right views have identical sizes.
            const XrViewConfigurationView& view = m_renderResources->ConfigViews[0];
            CHECK(m_renderResources->ConfigViews[0].recommendedImageRectWidth ==
                  m_renderResources->ConfigViews[1].recommendedImageRectWidth);
            CHECK(m_renderResources->ConfigViews[0].recommendedImageRectHeight ==
                  m_renderResources->ConfigViews[1].recommendedImageRectHeight);
            CHECK(m_renderResources->ConfigViews[0].recommendedSwapchainSampleCount ==
                  m_renderResources->ConfigViews[1].recommendedSwapchainSampleCount);

            // Use recommended rendering parameters for a balance between quality and performance
            const uint32_t imageRectWidth = view.recommendedImageRectWidth;
            const uint32_t imageRectHeight = view.recommendedImageRectHeight;
            const uint32_t swapchainSampleCount = view.recommendedSwapchainSampleCount;

            // Create swapchains with texture array for color and depth images.
            // The texture array has the size of viewCount, and they are rendered in a single pass using VPRT.
            const uint32_t textureArraySize = viewCount;
            m_renderResources->ColorSwapchain =
                CreateSwapchainD3D11(m_session.Get(),
                                     colorSwapchainFormat,
                                     imageRectWidth,
                                     imageRectHeight,
                                     textureArraySize,
                                     swapchainSampleCount,
                                     0 /*createFlags*/,
                                     XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT); //TODO: dx12

            m_renderResources->DepthSwapchain =
                CreateSwapchainD3D11(m_session.Get(),
                                     depthSwapchainFormat,
                                     imageRectWidth,
                                     imageRectHeight,
                                     textureArraySize,
                                     swapchainSampleCount,
                                     0 /*createFlags*/,
                                     XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT); //TODO: dx12

            // Preallocate view buffers for xrLocateViews later inside frame loop.
            m_renderResources->Views.resize(viewCount, {XR_TYPE_VIEW});
        }

        struct SwapchainD3D12;
        SwapchainD3D12 CreateSwapchainD3D11(XrSession session,
                                            DXGI_FORMAT format,
                                            uint32_t width,
                                            uint32_t height,
                                            uint32_t arraySize,
                                            uint32_t sampleCount,
                                            XrSwapchainCreateFlags createFlags,
                                            XrSwapchainUsageFlags usageFlags) {
            SwapchainD3D12 swapchain;
            swapchain.Format = format;
            swapchain.Width = width;
            swapchain.Height = height;
            swapchain.ArraySize = arraySize;

            XrSwapchainCreateInfo swapchainCreateInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
            swapchainCreateInfo.arraySize = arraySize;
            swapchainCreateInfo.format = format;
            swapchainCreateInfo.width = width;
            swapchainCreateInfo.height = height;
            swapchainCreateInfo.mipCount = 1;
            swapchainCreateInfo.faceCount = 1;
            swapchainCreateInfo.sampleCount = sampleCount;
            swapchainCreateInfo.createFlags = createFlags;
            swapchainCreateInfo.usageFlags = usageFlags;

            CHECK_XRCMD(xrCreateSwapchain(session, &swapchainCreateInfo, swapchain.Handle.Put()));

            uint32_t chainLength;
            CHECK_XRCMD(xrEnumerateSwapchainImages(swapchain.Handle.Get(), 0, &chainLength, nullptr));

            swapchain.Images.resize(chainLength, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
            CHECK_XRCMD(xrEnumerateSwapchainImages(swapchain.Handle.Get(),
                                                   (uint32_t)swapchain.Images.size(),
                                                   &chainLength,
                                                   reinterpret_cast<XrSwapchainImageBaseHeader*>(swapchain.Images.data())));

            return swapchain;
        }

        void ProcessEvents(bool* exitRenderLoop, bool* requestRestart) {
            *exitRenderLoop = *requestRestart = false;

            auto pollEvent = [&](XrEventDataBuffer& eventData) -> bool {
                eventData.type = XR_TYPE_EVENT_DATA_BUFFER;
                eventData.next = nullptr;
                return CHECK_XRCMD(xrPollEvent(m_instance.Get(), &eventData)) == XR_SUCCESS;
            };

            XrEventDataBuffer eventData{};
            while (pollEvent(eventData)) {
                switch (eventData.type) {
                case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
                    *exitRenderLoop = true;
                    *requestRestart = false;
                    return;
                }
                case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                    const auto stateEvent = *reinterpret_cast<const XrEventDataSessionStateChanged*>(&eventData);
                    CHECK(m_session.Get() != XR_NULL_HANDLE && m_session.Get() == stateEvent.session);
                    m_sessionState = stateEvent.state;
                    switch (m_sessionState) {
                    case XR_SESSION_STATE_READY: {
                        CHECK(m_session.Get() != XR_NULL_HANDLE);
                        XrSessionBeginInfo sessionBeginInfo{XR_TYPE_SESSION_BEGIN_INFO};
                        sessionBeginInfo.primaryViewConfigurationType = m_primaryViewConfigType;
                        CHECK_XRCMD(xrBeginSession(m_session.Get(), &sessionBeginInfo));
                        m_sessionRunning = true;
                        break;
                    }
                    case XR_SESSION_STATE_STOPPING: {
                        m_sessionRunning = false;
                        CHECK_XRCMD(xrEndSession(m_session.Get()));
                        break;
                    }
                    case XR_SESSION_STATE_EXITING: {
                        // Do not attempt to restart because user closed this session.
                        *exitRenderLoop = true;
                        *requestRestart = false;
                        break;
                    }
                    case XR_SESSION_STATE_LOSS_PENDING: {
                        // Poll for a new systemId
                        *exitRenderLoop = true;
                        *requestRestart = true;
                        break;
                    }
                    }
                    break;
                }
                case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
                case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
                default: {
                    DEBUG_PRINT("Ignoring event type %d", eventData.type);
                    break;
                }
                }
            }
        }

        struct Hologram;
        Hologram CreateHologram(const XrPosef& poseInScene, XrTime placementTime) const {
            Hologram hologram{};
            if (m_optionalExtensions.SpatialAnchorSupported) {
                // Anchors provide the best stability when moving beyond 5 meters, so if the extension is enabled,
                // create an anchor at given location and place the hologram at the resulting anchor space.
                XrSpatialAnchorCreateInfoMSFT createInfo{XR_TYPE_SPATIAL_ANCHOR_CREATE_INFO_MSFT};
                createInfo.space = m_sceneSpace.Get();
                createInfo.pose = poseInScene;
                createInfo.time = placementTime;

                XrResult result = m_extensions.xrCreateSpatialAnchorMSFT(
                    m_session.Get(), &createInfo, hologram.Anchor.Put(m_extensions.xrDestroySpatialAnchorMSFT));
                if (XR_SUCCEEDED(result)) {
                    XrSpatialAnchorSpaceCreateInfoMSFT createSpaceInfo{XR_TYPE_SPATIAL_ANCHOR_SPACE_CREATE_INFO_MSFT};
                    createSpaceInfo.anchor = hologram.Anchor.Get();
                    createSpaceInfo.poseInAnchorSpace = xr::math::Pose::Identity();
                    CHECK_XRCMD(m_extensions.xrCreateSpatialAnchorSpaceMSFT(m_session.Get(), &createSpaceInfo, hologram.Cube.Space.Put()));
                } else if (result == XR_ERROR_CREATE_SPATIAL_ANCHOR_FAILED_MSFT) {
                    DEBUG_PRINT("Anchor cannot be created, likely due to lost positional tracking.");
                } else {
                    CHECK_XRRESULT(result, "xrCreateSpatialAnchorMSFT");
                }
            } else {
                // If the anchor extension is not available, place it in the scene space.
                // This works fine as long as user doesn't move far away from scene space origin.
                XrReferenceSpaceCreateInfo createInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
                createInfo.referenceSpaceType = m_sceneSpaceType;
                createInfo.poseInReferenceSpace = poseInScene;
                CHECK_XRCMD(xrCreateReferenceSpace(m_session.Get(), &createInfo, hologram.Cube.Space.Put()));
            }
            return hologram;
        }

        void PollActions() {
            // Get updated action states.
            std::vector<XrActiveActionSet> activeActionSets = {{m_actionSet.Get(), XR_NULL_PATH}};
            XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
            syncInfo.countActiveActionSets = (uint32_t)activeActionSets.size();
            syncInfo.activeActionSets = activeActionSets.data();
            CHECK_XRCMD(xrSyncActions(m_session.Get(), &syncInfo));

            // Check the state of the actions for left and right hands separately.
            for (uint32_t side : {LeftSide, RightSide}) {
                const XrPath subactionPath = m_subactionPaths[side];

                // Apply a tiny vibration to the corresponding hand to indicate that action is detected.
                auto ApplyVibration = [this, subactionPath] {
                    XrHapticActionInfo actionInfo{XR_TYPE_HAPTIC_ACTION_INFO};
                    actionInfo.action = m_vibrateAction.Get();
                    actionInfo.subactionPath = subactionPath;

                    XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
                    vibration.amplitude = 0.5f;
                    vibration.duration = XR_MIN_HAPTIC_DURATION;
                    vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
                    CHECK_XRCMD(xrApplyHapticFeedback(m_session.Get(), &actionInfo, (XrHapticBaseHeader*)&vibration));
                };

                XrActionStateBoolean placeActionValue{XR_TYPE_ACTION_STATE_BOOLEAN};
                {
                    XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
                    getInfo.action = m_placeAction.Get();
                    getInfo.subactionPath = subactionPath;
                    CHECK_XRCMD(xrGetActionStateBoolean(m_session.Get(), &getInfo, &placeActionValue));
                }

                // When select button is pressed, place the cube at the location of corresponding hand.
                if (placeActionValue.isActive && placeActionValue.changedSinceLastSync && placeActionValue.currentState) {
                    // Use the poses at the time when action happened to do the placement
                    const XrTime placementTime = placeActionValue.lastChangeTime;

                    // Locate the hand in the scene.
                    XrSpaceLocation handLocation{XR_TYPE_SPACE_LOCATION};
                    CHECK_XRCMD(xrLocateSpace(m_cubesInHand[side].Space.Get(), m_sceneSpace.Get(), placementTime, &handLocation));

                    // Ensure we have tracking before placing a cube in the scene, so that it stays reliably at a physical location.
                    if (!xr::math::Pose::IsPoseValid(handLocation)) {
                        DEBUG_PRINT("Cube cannot be placed when positional tracking is lost.");
                    } else {
                        // Place a new cube at the given location and time, and remember output placement space and anchor.
                        m_holograms.push_back(CreateHologram(handLocation.pose, placementTime));
                    }

                    ApplyVibration();
                }

                // This sample, when menu button is released, requests to quit the session, and therefore quit the application.
                {
                    XrActionStateBoolean exitActionValue{XR_TYPE_ACTION_STATE_BOOLEAN};
                    XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
                    getInfo.action = m_exitAction.Get();
                    getInfo.subactionPath = subactionPath;
                    CHECK_XRCMD(xrGetActionStateBoolean(m_session.Get(), &getInfo, &exitActionValue));

                    if (exitActionValue.isActive && exitActionValue.changedSinceLastSync && !exitActionValue.currentState) {
                        CHECK_XRCMD(xrRequestExitSession(m_session.Get()));
                        ApplyVibration();
                    }
                }
            }
        }

        void RenderFrame() {
            CHECK(m_session.Get() != XR_NULL_HANDLE);

            XrFrameWaitInfo frameWaitInfo{XR_TYPE_FRAME_WAIT_INFO};
            XrFrameState frameState{XR_TYPE_FRAME_STATE};
            CHECK_XRCMD(xrWaitFrame(m_session.Get(), &frameWaitInfo, &frameState));

            XrFrameBeginInfo frameBeginInfo{XR_TYPE_FRAME_BEGIN_INFO};
            CHECK_XRCMD(xrBeginFrame(m_session.Get(), &frameBeginInfo));

            // EndFrame can submit mutiple layers
            std::vector<XrCompositionLayerBaseHeader*> layers;

            // The projection layer consists of projection layer views.
            XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};

            // Inform the runtime to consider alpha channel during composition
            // The primary display on Hololens has additive environment blend mode. It will ignore alpha channel.
            // But mixed reality capture has alpha blend mode display and use alpha channel to blend content to environment.
            layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;

            // Only render when session is visible. otherwise submit zero layers
            if (frameState.shouldRender) {
                // First update the viewState and views using latest predicted display time.
                {
                    XrViewLocateInfo viewLocateInfo{XR_TYPE_VIEW_LOCATE_INFO};
                    viewLocateInfo.viewConfigurationType = m_primaryViewConfigType;
                    viewLocateInfo.displayTime = frameState.predictedDisplayTime;
                    viewLocateInfo.space = m_sceneSpace.Get();

                    // The output view count of xrLocateViews is always same as xrEnumerateViewConfigurationViews
                    // Therefore Views can be preallocated and avoid two call idiom here.
                    uint32_t viewCapacityInput = (uint32_t)m_renderResources->Views.size();
                    uint32_t viewCountOutput;
                    CHECK_XRCMD(xrLocateViews(m_session.Get(),
                                              &viewLocateInfo,
                                              &m_renderResources->ViewState,
                                              viewCapacityInput,
                                              &viewCountOutput,
                                              m_renderResources->Views.data()));

                    CHECK(viewCountOutput == viewCapacityInput);
                    CHECK(viewCountOutput == m_renderResources->ConfigViews.size());
                    CHECK(viewCountOutput == m_renderResources->ColorSwapchain.ArraySize);
                    CHECK(viewCountOutput == m_renderResources->DepthSwapchain.ArraySize);
                }

                // Then render projection layer into each view.
                if (RenderLayer(frameState.predictedDisplayTime, layer)) { //TODO: dx12
                    layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer));
                }
            }

            // Submit the composition layers for the predicted display time.
            XrFrameEndInfo frameEndInfo{XR_TYPE_FRAME_END_INFO};
            frameEndInfo.displayTime = frameState.predictedDisplayTime;
            frameEndInfo.environmentBlendMode = m_environmentBlendMode;
            frameEndInfo.layerCount = (uint32_t)layers.size();
            frameEndInfo.layers = layers.data();
            CHECK_XRCMD(xrEndFrame(m_session.Get(), &frameEndInfo));
        }

        uint32_t AquireAndWaitForSwapchainImage(XrSwapchain handle) {
            uint32_t swapchainImageIndex;
            XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            CHECK_XRCMD(xrAcquireSwapchainImage(handle, &acquireInfo, &swapchainImageIndex));

            XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            waitInfo.timeout = XR_INFINITE_DURATION;
            CHECK_XRCMD(xrWaitSwapchainImage(handle, &waitInfo));

            return swapchainImageIndex;
        }

        void UpdateSpinningCube(XrTime predictedDisplayTime) {
            if (!m_mainCubeIndex) {
                // Initialize a big cube 1 meter in front of user.
                Hologram hologram = CreateHologram(xr::math::Pose::Translation({0, 0, -1}), predictedDisplayTime);
                hologram.Cube.Scale = {0.25f, 0.25f, 0.25f};
                m_holograms.push_back(std::move(hologram));
                m_mainCubeIndex = (uint32_t)m_holograms.size() - 1;
            }

            if (!m_spinningCubeIndex) {
                // Initialize a small cube and remember the time when animation is started.
                Hologram hologram = CreateHologram(xr::math::Pose::Translation({0, 0, -1}), predictedDisplayTime);
                hologram.Cube.Scale = {0.1f, 0.1f, 0.1f};
                m_holograms.push_back(std::move(hologram));
                m_spinningCubeIndex = (uint32_t)m_holograms.size() - 1;

                m_spinningCubeStartTime = predictedDisplayTime;
            }

            // Pause spinning cube animation when app lost 3D focus
            if (IsSessionFocused()) {
                auto convertToSeconds = [](XrDuration nanoSeconds) {
                    using namespace std::chrono;
                    return duration_cast<duration<float>>(duration<XrDuration, std::nano>(nanoSeconds)).count();
                };

                const XrDuration duration = predictedDisplayTime - m_spinningCubeStartTime;
                const float seconds = convertToSeconds(duration);
                const float angle = DirectX::XM_PIDIV2 * seconds; // Rotate 90 degrees per second
                const float radius = 0.5f;                        // Rotation radius in meters

                // Let spinning cube rotate around the main cube at y axis.
                XrPosef pose;
                pose.position = {radius * std::sin(angle), 0, radius * std::cos(angle)};
                pose.orientation = xr::math::Quaternion::RotationAxisAngle({0, 1, 0}, angle);
                m_holograms[m_spinningCubeIndex.value()].Cube.PoseInSpace = pose;
            }
        }

        bool RenderLayer(XrTime predictedDisplayTime, XrCompositionLayerProjection& layer) {
            const uint32_t viewCount = (uint32_t)m_renderResources->ConfigViews.size();

            if (!xr::math::Pose::IsPoseValid(m_renderResources->ViewState)) {
                DEBUG_PRINT("xrLocateViews returned an invalid pose.");
                return false; // Skip rendering layers if view location is invalid
            }

            std::vector<const sample::Cube*> visibleCubes;

            auto UpdateVisibleCube = [&](sample::Cube& cube) {
                if (cube.Space.Get() != XR_NULL_HANDLE) {
                    XrSpaceLocation cubeSpaceInScene{XR_TYPE_SPACE_LOCATION};
                    CHECK_XRCMD(xrLocateSpace(cube.Space.Get(), m_sceneSpace.Get(), predictedDisplayTime, &cubeSpaceInScene));

                    // Update cubes location with latest space relation
                    if (xr::math::Pose::IsPoseValid(cubeSpaceInScene)) {
                        if (cube.PoseInSpace.has_value()) {
                            cube.PoseInScene = xr::math::Pose::Multiply(cube.PoseInSpace.value(), cubeSpaceInScene.pose);
                        } else {
                            cube.PoseInScene = cubeSpaceInScene.pose;
                        }
                        visibleCubes.push_back(&cube);
                    }
                }
            };

            UpdateSpinningCube(predictedDisplayTime);

            UpdateVisibleCube(m_cubesInHand[LeftSide]);
            UpdateVisibleCube(m_cubesInHand[RightSide]);

            for (auto& hologram : m_holograms) {
                UpdateVisibleCube(hologram.Cube);
            }

            m_renderResources->ProjectionLayerViews.resize(viewCount);
            if (m_optionalExtensions.DepthExtensionSupported) {
                m_renderResources->DepthInfoViews.resize(viewCount);
            }

            // Swapchain is acquired, rendered to, and released together for all views as texture array
            const SwapchainD3D12& colorSwapchain = m_renderResources->ColorSwapchain;
            const SwapchainD3D12& depthSwapchain = m_renderResources->DepthSwapchain;

            // Use the full range of recommended image size to achieve optimum resolution
            const XrRect2Di imageRect = {{0, 0}, {(int32_t)colorSwapchain.Width, (int32_t)colorSwapchain.Height}};
            CHECK(colorSwapchain.Width == depthSwapchain.Width);
            CHECK(colorSwapchain.Height == depthSwapchain.Height);

            const uint32_t colorSwapchainImageIndex = AquireAndWaitForSwapchainImage(colorSwapchain.Handle.Get());
            const uint32_t depthSwapchainImageIndex = AquireAndWaitForSwapchainImage(depthSwapchain.Handle.Get());

            // Prepare rendering parameters of each view for swapchain texture arrays
            std::vector<xr::math::ViewProjection> viewProjections(viewCount);
            for (uint32_t i = 0; i < viewCount; i++) {
                viewProjections[i] = {m_renderResources->Views[i].pose, m_renderResources->Views[i].fov, m_nearFar};

                m_renderResources->ProjectionLayerViews[i] = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
                m_renderResources->ProjectionLayerViews[i].pose = m_renderResources->Views[i].pose;
                m_renderResources->ProjectionLayerViews[i].fov = m_renderResources->Views[i].fov;
                m_renderResources->ProjectionLayerViews[i].subImage.swapchain = colorSwapchain.Handle.Get();
                m_renderResources->ProjectionLayerViews[i].subImage.imageRect = imageRect;
                m_renderResources->ProjectionLayerViews[i].subImage.imageArrayIndex = i;

                if (m_optionalExtensions.DepthExtensionSupported) {
                    m_renderResources->DepthInfoViews[i] = {XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR};
                    m_renderResources->DepthInfoViews[i].minDepth = 0;
                    m_renderResources->DepthInfoViews[i].maxDepth = 1;
                    m_renderResources->DepthInfoViews[i].nearZ = m_nearFar.Near;
                    m_renderResources->DepthInfoViews[i].farZ = m_nearFar.Far;
                    m_renderResources->DepthInfoViews[i].subImage.swapchain = depthSwapchain.Handle.Get();
                    m_renderResources->DepthInfoViews[i].subImage.imageRect = imageRect;
                    m_renderResources->DepthInfoViews[i].subImage.imageArrayIndex = i;

                    // Chain depth info struct to the corresponding projection layer views's next
                    m_renderResources->ProjectionLayerViews[i].next = &m_renderResources->DepthInfoViews[i];
                }
            }

            // For Hololens additive display, best to clear render target with transparent black color (0,0,0,0)
            constexpr DirectX::XMVECTORF32 opaqueColor = {0.184313729f, 0.309803933f, 0.309803933f, 1.000000000f};
            constexpr DirectX::XMVECTORF32 transparent = {0.000000000f, 0.000000000f, 0.000000000f, 0.000000000f};
            const DirectX::XMVECTORF32 renderTargetClearColor =
                (m_environmentBlendMode == XR_ENVIRONMENT_BLEND_MODE_OPAQUE) ? opaqueColor : transparent;

            m_graphicsPlugin->RenderView(imageRect,
                                         renderTargetClearColor,
                                         viewProjections,
                                         colorSwapchain.Format,
                                         colorSwapchain.Images[colorSwapchainImageIndex].texture,
                                         depthSwapchain.Format,
                                         depthSwapchain.Images[depthSwapchainImageIndex].texture,
                                         visibleCubes); //TODO: dx12

            XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            CHECK_XRCMD(xrReleaseSwapchainImage(colorSwapchain.Handle.Get(), &releaseInfo));
            CHECK_XRCMD(xrReleaseSwapchainImage(depthSwapchain.Handle.Get(), &releaseInfo));

            layer.space = m_sceneSpace.Get();
            layer.viewCount = (uint32_t)m_renderResources->ProjectionLayerViews.size();
            layer.views = m_renderResources->ProjectionLayerViews.data();
            return true;
        }

        void PrepareSessionRestart() {
            m_mainCubeIndex = m_spinningCubeIndex = {};
            m_holograms.clear();
            m_renderResources.reset();
            m_session.Reset();
            m_systemId = XR_NULL_SYSTEM_ID;
        }

        constexpr bool IsSessionFocused() const {
            return m_sessionState == XR_SESSION_STATE_FOCUSED;
        }

        XrPath GetXrPath(const char* string) const {
            return xr::StringToPath(m_instance.Get(), string);
        }

    private:
        constexpr static XrFormFactor m_formFactor{XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY};
        constexpr static XrViewConfigurationType m_primaryViewConfigType{XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO};
        constexpr static uint32_t m_stereoViewCount = 2; // PRIMARY_STEREO view configuration always has 2 views

        const std::string m_applicationName;
        const std::unique_ptr<sample::IGraphicsPluginD3D12> m_graphicsPlugin;

        xr::InstanceHandle m_instance;
        xr::SessionHandle m_session;
        uint64_t m_systemId{XR_NULL_SYSTEM_ID};
        xr::ExtensionDispatchTable m_extensions;

        struct {
            bool DepthExtensionSupported{false};
            bool UnboundedRefSpaceSupported{false};
            bool SpatialAnchorSupported{false};
        } m_optionalExtensions;

        xr::SpaceHandle m_sceneSpace;
        XrReferenceSpaceType m_sceneSpaceType{};

        struct Hologram {
            sample::Cube Cube;
            xr::SpatialAnchorHandle Anchor;
        };
        std::vector<Hologram> m_holograms;

        std::optional<uint32_t> m_mainCubeIndex;
        std::optional<uint32_t> m_spinningCubeIndex;
        XrTime m_spinningCubeStartTime;

        constexpr static uint32_t LeftSide = 0;
        constexpr static uint32_t RightSide = 1;
        std::array<XrPath, 2> m_subactionPaths{};
        std::array<sample::Cube, 2> m_cubesInHand{};

        xr::ActionSetHandle m_actionSet;
        xr::ActionHandle m_placeAction;
        xr::ActionHandle m_exitAction;
        xr::ActionHandle m_poseAction;
        xr::ActionHandle m_vibrateAction;

        XrEnvironmentBlendMode m_environmentBlendMode{};
        xr::math::NearFar m_nearFar{};

        struct SwapchainD3D12 {
            xr::SwapchainHandle Handle;
            DXGI_FORMAT Format{DXGI_FORMAT_UNKNOWN};
            uint32_t Width{0};
            uint32_t Height{0};
            uint32_t ArraySize{0};
            std::vector<XrSwapchainImageD3D12KHR> Images;
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

        std::unique_ptr<RenderResources> m_renderResources{};

        bool m_sessionRunning{false};
        XrSessionState m_sessionState{XR_SESSION_STATE_UNKNOWN};
    };
} // namespace

namespace sample {
    std::unique_ptr<sample::IOpenXrProgram> CreateOpenXrProgram(std::string applicationName,
                                                                std::unique_ptr<sample::IGraphicsPluginD3D12> graphicsPlugin) {
        return std::make_unique<ImplementOpenXrProgram>(std::move(applicationName), std::move(graphicsPlugin));
    }
} // namespace sample
