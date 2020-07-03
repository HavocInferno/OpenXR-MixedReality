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
#include "GraphicsCore.h"
#include "DxUtility.h"

#include <filesystem>

#include "shared/lodepng.h"



bool GraphicsCore::InitializeD3D12Device(LUID adapterLuid) {
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

ID3D12Device* GraphicsCore::InitializeD3D12(LUID adapterLuid, std::unique_ptr<sample::IOpenXrProgram::RenderResources>& renderresc) {
    bool ret = InitializeD3D12Device(adapterLuid);
    ret = InitializeD3DResources(renderresc);

    return m_pDevice.get();
}

bool GraphicsCore::InitializeD3DResources(std::unique_ptr<sample::IOpenXrProgram::RenderResources>& renderresc) {
    /*
    adapted from former DX11 resource init

    //CHECK_MSG(options.VPAndRTArrayIndexFromAnyShaderFeedingRasterizer,
    //          "This sample requires VPRT support. Adjust sample shaders on GPU without VRPT.");
    */
    D3D12_FEATURE_DATA_D3D12_OPTIONS options;
    m_pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    CHECK_MSG(options.VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation,
              "This sample requires VPRT support. Adjust sample shaders on GPU without VRPT.");

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
        if (FAILED(m_pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(m_pCommandAllocators[nFrame].put())))) {
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

    if (!sample::dx::CreateAllShaders(
            m_pDevice, m_pRootSignature, m_nMSAASampleCount, m_pScenePipelineState, m_pAxesPipelineState, m_pRenderModelPipelineState))
        return false;

    // Create command list
    m_pDevice->CreateCommandList(0,
                                 D3D12_COMMAND_LIST_TYPE_DIRECT,
                                 m_pCommandAllocators[m_nFrameIndex].get(),
                                 m_pScenePipelineState.get(),
                                 IID_PPV_ARGS(m_pCommandList.put()));

    /*SetupTexturemaps();
    SetupScene(); 
    //SetupCameras();   //ImplementOpenXrProgram::RenderLayer l. 780ff queries updated viewProjections from OpenXR, may be able to skip HelloVR's variant of getting view projection matrices
    SetupStereoRenderTargets(renderresc); 
    //SetupCompanionWindow(); //TODO: exclude companion window items for now
    SetupRenderModels(); 

    // Do any work that was queued up during loading
    m_pCommandList->Close();
    ID3D12CommandList* ppCommandLists[] = {m_pCommandList.get()};
    m_pCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Wait for it to finish
    m_pCommandQueue->Signal(m_pFence.get(), m_nFenceValues[m_nFrameIndex]);
    m_pFence->SetEventOnCompletion(m_nFenceValues[m_nFrameIndex], m_fenceEvent);
    WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
    m_nFenceValues[m_nFrameIndex]++;*/

    return true;
}

void GraphicsCore::InitializeResources2(std::unique_ptr<sample::IOpenXrProgram::RenderResources>& renderresc) {
    SetupTexturemaps();
    SetupScene();
    // SetupCameras();   //ImplementOpenXrProgram::RenderLayer l. 780ff queries updated viewProjections from OpenXR, may be able to skip
    // HelloVR's variant of getting view projection matrices
    SetupStereoRenderTargets(renderresc);
    // SetupCompanionWindow(); //TODO: exclude companion window items for now
    SetupRenderModels();

    // Do any work that was queued up during loading
    m_pCommandList->Close();
    ID3D12CommandList* ppCommandLists[] = {m_pCommandList.get()};
    m_pCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Wait for it to finish
    m_pCommandQueue->Signal(m_pFence.get(), m_nFenceValues[m_nFrameIndex]);
    m_pFence->SetEventOnCompletion(m_nFenceValues[m_nFrameIndex], m_fenceEvent);
    WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
    m_nFenceValues[m_nFrameIndex]++;
}

bool GraphicsCore::SetupTexturemaps() {
    std::string sExecutableDirectory = std::filesystem::current_path().string();
    std::string strFullPath = sExecutableDirectory + "/tex/cube_texture.png"; 

    std::vector<unsigned char> imageRGBA;
    unsigned nImageWidth, nImageHeight;
    unsigned nError = lodepng::decode(imageRGBA, nImageWidth, nImageHeight, strFullPath.c_str());

    if (nError != 0)
        return false;

    // Store level 0
    std::vector<D3D12_SUBRESOURCE_DATA> mipLevelData;
    UINT8* pBaseData = new UINT8[nImageWidth * nImageHeight * 4];
    memcpy(pBaseData, &imageRGBA[0], sizeof(UINT8) * nImageWidth * nImageHeight * 4);

    D3D12_SUBRESOURCE_DATA textureData = {};
    textureData.pData = &pBaseData[0];
    textureData.RowPitch = nImageWidth * 4;
    textureData.SlicePitch = textureData.RowPitch * nImageHeight;
    mipLevelData.push_back(textureData);

    // Generate mipmaps for the image
    int nPrevImageIndex = 0;
    int nMipWidth = nImageWidth;
    int nMipHeight = nImageHeight;

    while (nMipWidth > 1 && nMipHeight > 1) {
        UINT8* pNewImage;
        GenMipMapRGBA((UINT8*)mipLevelData[nPrevImageIndex].pData, &pNewImage, nMipWidth, nMipHeight, &nMipWidth, &nMipHeight);

        D3D12_SUBRESOURCE_DATA mipData = {};
        mipData.pData = pNewImage;
        mipData.RowPitch = nMipWidth * 4;
        mipData.SlicePitch = textureData.RowPitch * nMipHeight;
        mipLevelData.push_back(mipData);

        nPrevImageIndex++;
    }

    D3D12_RESOURCE_DESC textureDesc = {};
    textureDesc.MipLevels = (UINT16)mipLevelData.size();
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.Width = nImageWidth;
    textureDesc.Height = nImageHeight;
    textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.SampleDesc.Quality = 0;
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    
    auto heap_props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT); 
    m_pDevice->CreateCommittedResource(&heap_props,
                                       D3D12_HEAP_FLAG_NONE,
                                       &textureDesc,
                                       D3D12_RESOURCE_STATE_COPY_DEST,
                                       nullptr,
                                       IID_PPV_ARGS(m_pTexture.put()));

    // Create shader resource view
    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_pCBVSRVHeap->GetCPUDescriptorHandleForHeapStart());
    srvHandle.Offset(SRV_TEXTURE_MAP, m_nCBVSRVDescriptorSize);
    m_pDevice->CreateShaderResourceView(m_pTexture.get(), nullptr, srvHandle);
    m_textureShaderResourceView = srvHandle;

    const UINT64 nUploadBufferSize = GetRequiredIntermediateSize(m_pTexture.get(), 0, textureDesc.MipLevels);

    // Create the GPU upload buffer.
    heap_props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD); 
    auto resource_desc = CD3DX12_RESOURCE_DESC::Buffer(nUploadBufferSize); 
    m_pDevice->CreateCommittedResource(&heap_props,
                                       D3D12_HEAP_FLAG_NONE,
                                       &resource_desc,
                                       D3D12_RESOURCE_STATE_GENERIC_READ,
                                       nullptr,
                                       IID_PPV_ARGS(m_pTextureUploadHeap.put()));

    UpdateSubresources(m_pCommandList.get(), m_pTexture.get(), m_pTextureUploadHeap.get(), 0, 0, (UINT)mipLevelData.size(), &mipLevelData[0]);
    auto resource_barrier =
        CD3DX12_RESOURCE_BARRIER::Transition(m_pTexture.get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE); 
    m_pCommandList->ResourceBarrier(1, &resource_barrier);

    // Free mip pointers
    for (size_t nMip = 0; nMip < mipLevelData.size(); nMip++) {
        delete[] mipLevelData[nMip].pData;
    }
    return true;
}

bool GraphicsCore::SetupScene() {
    //TODO: replace this with openxr based check?
    /*if (!m_pHMD)
        return;*/

    std::vector<float> vertdataarray;

    //add matrix of cubes
    Eigen::Affine3f matTransform = Eigen::Affine3f::Identity();
    matTransform.matrix().array().rowwise() *= Eigen::RowVector4f(m_fScale, m_fScale, m_fScale, 1.0).array(); 
    matTransform.translate(Eigen::Vector3f(-((float)m_iSceneVolumeWidth * m_fScaleSpacing) / 2.f,
                                           -((float)m_iSceneVolumeHeight * m_fScaleSpacing) / 2.f,
                                           -((float)m_iSceneVolumeDepth * m_fScaleSpacing) / 2.f)); 
    
    for (int z = 0; z < m_iSceneVolumeDepth; z++) {
        for (int y = 0; y < m_iSceneVolumeHeight; y++) {
            for (int x = 0; x < m_iSceneVolumeWidth; x++) {
                AddCubeToScene(matTransform.matrix(), vertdataarray);
                matTransform.translate(Eigen::Vector3f(m_fScaleSpacing, 0, 0));
            }
            matTransform.translate(Eigen::Vector3f(-((float)m_iSceneVolumeWidth) * m_fScaleSpacing, m_fScaleSpacing, 0));
        }
        matTransform.translate(Eigen::Vector3f(0, -((float)m_iSceneVolumeHeight) * m_fScaleSpacing, m_fScaleSpacing));
    }
    m_uiVertcount = (unsigned int)(vertdataarray.size()) / 5;

    auto heap_props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD); 
    auto resource_desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(float) * vertdataarray.size()); 
    m_pDevice->CreateCommittedResource(&heap_props,
                                       D3D12_HEAP_FLAG_NONE,
                                       &resource_desc,
                                       D3D12_RESOURCE_STATE_GENERIC_READ,
                                       nullptr,
                                       IID_PPV_ARGS(m_pSceneVertexBuffer.put()));

    UINT8* pMappedBuffer;
    CD3DX12_RANGE readRange(0, 0);
    m_pSceneVertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pMappedBuffer));
    memcpy(pMappedBuffer, &vertdataarray[0], sizeof(float) * vertdataarray.size());
    m_pSceneVertexBuffer->Unmap(0, nullptr);

    m_sceneVertexBufferView.BufferLocation = m_pSceneVertexBuffer->GetGPUVirtualAddress();
    m_sceneVertexBufferView.StrideInBytes = sizeof(VertexDataScene); //TODO: make sure this sizeof comes out as 3+2 floats
    m_sceneVertexBufferView.SizeInBytes = sizeof(float) * (unsigned int)(vertdataarray.size());

    return true;
}

bool GraphicsCore::SetupCameras() {
    //TODO: get projection matrices from openxr
    //m_renderResources->Views[i].pose, m_renderResources->Views[i].fov, m_nearFar
    //xr::math::ViewProjection
    //ComposeProjectionMatrix(viewProjections[k].Fov, viewProjections[k].NearFar)
    
    //m_mat4ProjectionLeft = GetHMDMatrixProjectionEye(vr::Eye_Left);
    //m_mat4ProjectionRight = GetHMDMatrixProjectionEye(vr::Eye_Right);
    //m_mat4eyePosLeft = GetHMDMatrixPoseEye(vr::Eye_Left);
    //m_mat4eyePosRight = GetHMDMatrixPoseEye(vr::Eye_Right);

    return true;
}

bool GraphicsCore::SetupStereoRenderTargets(std::unique_ptr<sample::IOpenXrProgram::RenderResources>& renderresc) {
    // TODO: replace this with openxr based check?
    /*if (!m_pHMD)
        return;*/

    // TODO: get rendertarget info from openxr
    //m_pHMD->GetRecommendedRenderTargetSize(&m_nRenderWidth, &m_nRenderHeight);
    //m_nRenderWidth = (uint32_t)(m_flSuperSampleScale * (float)m_nRenderWidth);
    //m_nRenderHeight = (uint32_t)(m_flSuperSampleScale * (float)m_nRenderHeight);

    for (int i = 0; i < renderresc->ColorSwapchain.Images.size(); i++) {
        CreateFrameBuffer(renderresc->ConfigViews[0].recommendedImageRectWidth,
                          renderresc->ConfigViews[0].recommendedImageRectHeight,
                          renderresc->ColorSwapchain.ArraySize,
                          renderresc->ColorSwapchain.Images[i].texture,
                          renderresc->ColorSwapchain.ViewHandles[i],
                          renderresc->DepthSwapchain.Images[i].texture,
                          renderresc->DepthSwapchain.ViewHandles[i],
                          RTV_LEFT_EYE);
        /*CreateFrameBuffer(renderresc->ConfigViews[0].recommendedImageRectWidth,
                          renderresc->ConfigViews[0].recommendedImageRectHeight,
                          renderresc->ColorSwapchain.ArraySize,
                          renderresc->ColorSwapchain.Images[i].texture,
                          renderresc->ColorSwapchain.ViewHandles[i],
                          renderresc->DepthSwapchain.Images[i].texture,
                          renderresc->DepthSwapchain.ViewHandles[i],
                          RTV_RIGHT_EYE);*/
    }
    
    return true;
}

bool GraphicsCore::SetupRenderModels() {
    // TODO: adapt from CMainApplication

    return true;
}

bool GraphicsCore::GenMipMapRGBA(const UINT8* pSrc, UINT8** ppDst, int nSrcWidth, int nSrcHeight, int* pDstWidthOut, int* pDstHeightOut) {
    *pDstWidthOut = nSrcWidth / 2;
    if (*pDstWidthOut <= 0) {
        *pDstWidthOut = 1;
    }
    *pDstHeightOut = nSrcHeight / 2;
    if (*pDstHeightOut <= 0) {
        *pDstHeightOut = 1;
    }

    *ppDst = new UINT8[4 * (*pDstWidthOut) * (*pDstHeightOut)];
    for (int y = 0; y < *pDstHeightOut; y++) {
        for (int x = 0; x < *pDstWidthOut; x++) {
            int nSrcIndex[4];
            float r = 0.0f;
            float g = 0.0f;
            float b = 0.0f;
            float a = 0.0f;

            nSrcIndex[0] = (((y * 2) * nSrcWidth) + (x * 2)) * 4;
            nSrcIndex[1] = (((y * 2) * nSrcWidth) + (x * 2 + 1)) * 4;
            nSrcIndex[2] = ((((y * 2) + 1) * nSrcWidth) + (x * 2)) * 4;
            nSrcIndex[3] = ((((y * 2) + 1) * nSrcWidth) + (x * 2 + 1)) * 4;

            // Sum all pixels
            for (int nSample = 0; nSample < 4; nSample++) {
                r += pSrc[nSrcIndex[nSample]];
                g += pSrc[nSrcIndex[nSample] + 1];
                b += pSrc[nSrcIndex[nSample] + 2];
                a += pSrc[nSrcIndex[nSample] + 3];
            }

            // Average results
            r /= 4.0;
            g /= 4.0;
            b /= 4.0;
            a /= 4.0;

            // Store resulting pixels
            (*ppDst)[(y * (*pDstWidthOut) + x) * 4] = (UINT8)(r);
            (*ppDst)[(y * (*pDstWidthOut) + x) * 4 + 1] = (UINT8)(g);
            (*ppDst)[(y * (*pDstWidthOut) + x) * 4 + 2] = (UINT8)(b);
            (*ppDst)[(y * (*pDstWidthOut) + x) * 4 + 3] = (UINT8)(a);
        }
    }

    return true;
}

bool GraphicsCore::AddCubeVertex(float fl0, float fl1, float fl2, float fl3, float fl4, std::vector<float>& vertdata) {
    vertdata.push_back(fl0);
    vertdata.push_back(fl1);
    vertdata.push_back(fl2);
    vertdata.push_back(fl3);
    vertdata.push_back(fl4);

    return true;
}

bool GraphicsCore::AddCubeToScene(Eigen::Matrix4f mat, std::vector<float>& vertdata) {
    // Matrix4 mat( outermat.data() );

    Eigen::Vector4f A = mat * Eigen::Vector4f(0, 0, 0, 1);
    Eigen::Vector4f B = mat * Eigen::Vector4f(1, 0, 0, 1);
    Eigen::Vector4f C = mat * Eigen::Vector4f(1, 1, 0, 1);
    Eigen::Vector4f D = mat * Eigen::Vector4f(0, 1, 0, 1);
    Eigen::Vector4f E = mat * Eigen::Vector4f(0, 0, 1, 1);
    Eigen::Vector4f F = mat * Eigen::Vector4f(1, 0, 1, 1);
    Eigen::Vector4f G = mat * Eigen::Vector4f(1, 1, 1, 1);
    Eigen::Vector4f H = mat * Eigen::Vector4f(0, 1, 1, 1);

    // triangles instead of quads
    AddCubeVertex(E.x(), E.y(), E.z(), 0, 1, vertdata); // Front
    AddCubeVertex(F.x(), F.y(), F.z(), 1, 1, vertdata);
    AddCubeVertex(G.x(), G.y(), G.z(), 1, 0, vertdata);
    AddCubeVertex(G.x(), G.y(), G.z(), 1, 0, vertdata);
    AddCubeVertex(H.x(), H.y(), H.z(), 0, 0, vertdata);
    AddCubeVertex(E.x(), E.y(), E.z(), 0, 1, vertdata);

    AddCubeVertex(B.x(), B.y(), B.z(), 0, 1, vertdata); // Back
    AddCubeVertex(A.x(), A.y(), A.z(), 1, 1, vertdata);
    AddCubeVertex(D.x(), D.y(), D.z(), 1, 0, vertdata);
    AddCubeVertex(D.x(), D.y(), D.z(), 1, 0, vertdata);
    AddCubeVertex(C.x(), C.y(), C.z(), 0, 0, vertdata);
    AddCubeVertex(B.x(), B.y(), B.z(), 0, 1, vertdata);

    AddCubeVertex(H.x(), H.y(), H.z(), 0, 1, vertdata); // Top
    AddCubeVertex(G.x(), G.y(), G.z(), 1, 1, vertdata);
    AddCubeVertex(C.x(), C.y(), C.z(), 1, 0, vertdata);
    AddCubeVertex(C.x(), C.y(), C.z(), 1, 0, vertdata);
    AddCubeVertex(D.x(), D.y(), D.z(), 0, 0, vertdata);
    AddCubeVertex(H.x(), H.y(), H.z(), 0, 1, vertdata);

    AddCubeVertex(A.x(), A.y(), A.z(), 0, 1, vertdata); // Bottom
    AddCubeVertex(B.x(), B.y(), B.z(), 1, 1, vertdata);
    AddCubeVertex(F.x(), F.y(), F.z(), 1, 0, vertdata);
    AddCubeVertex(F.x(), F.y(), F.z(), 1, 0, vertdata);
    AddCubeVertex(E.x(), E.y(), E.z(), 0, 0, vertdata);
    AddCubeVertex(A.x(), A.y(), A.z(), 0, 1, vertdata);

    AddCubeVertex(A.x(), A.y(), A.z(), 0, 1, vertdata); // Left
    AddCubeVertex(E.x(), E.y(), E.z(), 1, 1, vertdata);
    AddCubeVertex(H.x(), H.y(), H.z(), 1, 0, vertdata);
    AddCubeVertex(H.x(), H.y(), H.z(), 1, 0, vertdata);
    AddCubeVertex(D.x(), D.y(), D.z(), 0, 0, vertdata);
    AddCubeVertex(A.x(), A.y(), A.z(), 0, 1, vertdata);

    AddCubeVertex(F.x(), F.y(), F.z(), 0, 1, vertdata); // Right
    AddCubeVertex(B.x(), B.y(), B.z(), 1, 1, vertdata);
    AddCubeVertex(C.x(), C.y(), C.z(), 1, 0, vertdata);
    AddCubeVertex(C.x(), C.y(), C.z(), 1, 0, vertdata);
    AddCubeVertex(G.x(), G.y(), G.z(), 0, 0, vertdata);
    AddCubeVertex(F.x(), F.y(), F.z(), 0, 1, vertdata);

    return true; 
}

const std::vector<DXGI_FORMAT>& GraphicsCore::SupportedColorFormats() const {
    const static std::vector<DXGI_FORMAT> SupportedColorFormats = {
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_B8G8R8A8_UNORM,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
    };
    return SupportedColorFormats;
}

const std::vector<DXGI_FORMAT>& GraphicsCore::SupportedDepthFormats() const {
    const static std::vector<DXGI_FORMAT> SupportedDepthFormats = {
        DXGI_FORMAT_D32_FLOAT,
        DXGI_FORMAT_D16_UNORM,
        DXGI_FORMAT_D24_UNORM_S8_UINT,
        DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
    };
    return SupportedDepthFormats;
}

bool GraphicsCore::CreateFrameBuffer(int nWidth,
                       int nHeight, 
                       int viewCount,
                       ID3D12Resource* framebufferColorTexture,
                       CD3DX12_CPU_DESCRIPTOR_HANDLE& renderTargetViewHandle,
                       ID3D12Resource* framebufferDepthStencil,
                       CD3DX12_CPU_DESCRIPTOR_HANDLE& depthStencilViewHandle,
                       RTVIndex_t nRTVIndex) {
    D3D12_RESOURCE_DESC textureDesc = {};
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    textureDesc.Width = nWidth;
    textureDesc.Height = nHeight;
    textureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    textureDesc.DepthOrArraySize = viewCount;
    textureDesc.SampleDesc.Count = m_nMSAASampleCount;
    textureDesc.SampleDesc.Quality = 0;
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

    const float clearColor[] = {1.0f, 0.0f, 0.0f, 1.0f};

    // Create color target
    m_pDevice->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                                       D3D12_HEAP_FLAG_NONE,
                                       &textureDesc,
                                       D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                       &CD3DX12_CLEAR_VALUE(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, clearColor),
                                       IID_PPV_ARGS(&framebufferColorTexture));

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_pRTVHeap->GetCPUDescriptorHandleForHeapStart());
    rtvHandle.Offset(nRTVIndex, m_nRTVDescriptorSize);
    m_pDevice->CreateRenderTargetView(framebufferColorTexture, nullptr, rtvHandle);
    renderTargetViewHandle = rtvHandle;

    // Create shader resource view
    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_pCBVSRVHeap->GetCPUDescriptorHandleForHeapStart());
    srvHandle.Offset(SRV_LEFT_EYE + nRTVIndex, m_nCBVSRVDescriptorSize);
    m_pDevice->CreateShaderResourceView(framebufferColorTexture, nullptr, srvHandle);

    // Create depth
    D3D12_RESOURCE_DESC depthDesc = textureDesc;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    m_pDevice->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                                       D3D12_HEAP_FLAG_NONE,
                                       &depthDesc,
                                       D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                       &CD3DX12_CLEAR_VALUE(DXGI_FORMAT_D32_FLOAT, 1.0f, 0),
                                       IID_PPV_ARGS(&framebufferDepthStencil));

    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_pDSVHeap->GetCPUDescriptorHandleForHeapStart());
    dsvHandle.Offset(nRTVIndex, m_nDSVDescriptorSize);
    m_pDevice->CreateDepthStencilView(framebufferDepthStencil, nullptr, dsvHandle);
    depthStencilViewHandle = dsvHandle;
    return true;
}

bool GraphicsCore::RenderScene(int eyeIndex) {
    // TODO: adapt from CMainApplication

    return true;
}

bool GraphicsCore::RenderStereoTargets(const XrRect2Di& imageRect, ID3D12Resource* colorTexture, ID3D12Resource* depthTexture) {
    D3D12_VIEWPORT viewport = {0.0f, 0.0f, (FLOAT)imageRect.extent.width, (FLOAT)imageRect.extent.height, 0.0f, 1.0f};
    D3D12_RECT scissor = {0, 0, (LONG)imageRect.extent.width, (LONG)imageRect.extent.height};

    m_pCommandList->RSSetViewports(1, &viewport);
    m_pCommandList->RSSetScissorRects(1, &scissor);

    //----------//
    // Left Eye //
    //----------//
    // Transition to RENDER_TARGET
    m_pCommandList->ResourceBarrier(1,
                                    &CD3DX12_RESOURCE_BARRIER::Transition(colorTexture,
                                                                          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                                          D3D12_RESOURCE_STATE_RENDER_TARGET));
    m_pCommandList->OMSetRenderTargets(1, &m_leftEyeDesc.m_renderTargetViewHandle, FALSE, &m_leftEyeDesc.m_depthStencilViewHandle); //TODO

    const float clearColor[] = {0.0f, 0.0f, 0.0f, 1.0f};
    m_pCommandList->ClearRenderTargetView(m_leftEyeDesc.m_renderTargetViewHandle, clearColor, 0, nullptr);
    m_pCommandList->ClearDepthStencilView(m_leftEyeDesc.m_depthStencilViewHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0, 0, 0, nullptr);

    RenderScene(0); //left eye = 0

    // Transition to SHADER_RESOURCE to submit to SteamVR
    //m_pCommandList->ResourceBarrier(1,
    //                                &CD3DX12_RESOURCE_BARRIER::Transition(colorTexture,
    //                                                                      D3D12_RESOURCE_STATE_RENDER_TARGET,
    //                                                                      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

    //-----------//
    // Right Eye //
    //-----------//
    // Transition to RENDER_TARGET
    //m_pCommandList->ResourceBarrier(1,
    //                                &CD3DX12_RESOURCE_BARRIER::Transition(colorTexture,
    //                                                                      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
    //                                                                      D3D12_RESOURCE_STATE_RENDER_TARGET));
    m_pCommandList->OMSetRenderTargets(1, &m_rightEyeDesc.m_renderTargetViewHandle, FALSE, &m_rightEyeDesc.m_depthStencilViewHandle); //TODO

    m_pCommandList->ClearRenderTargetView(m_rightEyeDesc.m_renderTargetViewHandle, clearColor, 0, nullptr);
    m_pCommandList->ClearDepthStencilView(m_rightEyeDesc.m_depthStencilViewHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0, 0, 0, nullptr);

    RenderScene(1); // left eye = 1

    // Transition to SHADER_RESOURCE to submit to SteamVR
    m_pCommandList->ResourceBarrier(1,
                                    &CD3DX12_RESOURCE_BARRIER::Transition(colorTexture,
                                                                          D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                                          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
}

void GraphicsCore::RenderView(const XrRect2Di& imageRect,
                              const float renderTargetClearColor[4],
                              const std::vector<xr::math::ViewProjection>& viewProjections,
                              DXGI_FORMAT colorSwapchainFormat,
                              ID3D12Resource* colorTexture,
                              DXGI_FORMAT depthSwapchainFormat,
                              ID3D12Resource* depthTexture,
                              const std::vector<const sample::Cube*>& cubes) {
    //adapted from CMainApplication::RenderFrame
    if (true /*hmd present*/) 
    {
        m_pCommandAllocators[m_nFrameIndex]->Reset();

        m_pCommandList->Reset(m_pCommandAllocators[m_nFrameIndex].get(), m_pScenePipelineState.get());
        m_pCommandList->SetGraphicsRootSignature(m_pRootSignature.get());

        ID3D12DescriptorHeap* ppHeaps[] = {m_pCBVSRVHeap.get()};
        m_pCommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

        //UpdateControllerAxes();
        RenderStereoTargets(imageRect, colorTexture, depthTexture);
        //RenderCompanionWindow();

        m_pCommandList->Close();

        // Execute the command list.
        ID3D12CommandList* ppCommandLists[] = {m_pCommandList.get()};
        m_pCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
    }
    /*
    // Present
    m_pSwapChain->Present(0, 0);

    // Wait for completion
    {
        const UINT64 nCurrentFenceValue = m_nFenceValues[m_nFrameIndex];
        m_pCommandQueue->Signal(m_pFence.Get(), nCurrentFenceValue);

        m_nFrameIndex = m_pSwapChain->GetCurrentBackBufferIndex();
        if (m_pFence->GetCompletedValue() < m_nFenceValues[m_nFrameIndex]) {
            m_pFence->SetEventOnCompletion(m_nFenceValues[m_nFrameIndex], m_fenceEvent);
            WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
        }

        m_nFenceValues[m_nFrameIndex] = nCurrentFenceValue + 1;
    }

    // Spew out the controller and pose count whenever they change.
    if (m_iTrackedControllerCount != m_iTrackedControllerCount_Last || m_iValidPoseCount != m_iValidPoseCount_Last) {
        m_iValidPoseCount_Last = m_iValidPoseCount;
        m_iTrackedControllerCount_Last = m_iTrackedControllerCount;

        dprintf("PoseCount:%d(%s) Controllers:%d\n", m_iValidPoseCount, m_strPoseClasses.c_str(), m_iTrackedControllerCount);
    }

    UpdateHMDMatrixPose();*/



    //###################################################################
    const uint32_t viewInstanceCount = (uint32_t)viewProjections.size();
    CHECK_MSG(viewInstanceCount <= CubeShader::MaxViewInstance,
                "Sample shader supports 2 or fewer view instances. Adjust shader to accommodate more.")

    /*CD3D11_VIEWPORT viewport(
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
    m_deviceContext->PSSetShader(m_pixelShader.get(), nullptr, 0);*/

    CubeShader::ViewProjectionConstantBuffer viewProjectionCBufferData{};

    for (uint32_t k = 0; k < viewInstanceCount; k++) {
        const DirectX::XMMATRIX spaceToView = xr::math::LoadInvertedXrPose(viewProjections[k].Pose);
        const DirectX::XMMATRIX projectionMatrix = ComposeProjectionMatrix(viewProjections[k].Fov, viewProjections[k].NearFar);

        // Set view projection matrix for each view, transpose for shader usage.
        DirectX::XMStoreFloat4x4(&viewProjectionCBufferData.ViewProjection[k],
                                    DirectX::XMMatrixTranspose(spaceToView * projectionMatrix));
    }
    /*m_deviceContext->UpdateSubresource(m_viewProjectionCBuffer.get(), 0, nullptr, &viewProjectionCBufferData, 0, 0);

    // Set cube primitive data.
    const UINT strides[] = {sizeof(CubeShader::Vertex)};
    const UINT offsets[] = {0};
    ID3D11Buffer* vertexBuffers[] = {m_cubeVertexBuffer.get()};
    m_deviceContext->IASetVertexBuffers(0, (UINT)std::size(vertexBuffers), vertexBuffers, strides, offsets);
    m_deviceContext->IASetIndexBuffer(m_cubeIndexBuffer.get(), DXGI_FORMAT_R16_UINT, 0);
    m_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_deviceContext->IASetInputLayout(m_inputLayout.get());*/

    // Render each cube
    for (const sample::Cube* cube : cubes) {
        // Compute and update the model transform for each cube, transpose for shader usage.
        CubeShader::ModelConstantBuffer model;
        const DirectX::XMMATRIX scaleMatrix = DirectX::XMMatrixScaling(cube->Scale.x, cube->Scale.y, cube->Scale.z);
        DirectX::XMStoreFloat4x4(&model.Model, DirectX::XMMatrixTranspose(scaleMatrix * xr::math::LoadXrPose(cube->PoseInScene)));
        //m_deviceContext->UpdateSubresource(m_modelCBuffer.get(), 0, nullptr, &model, 0, 0);

        // Draw the cube.
        //m_deviceContext->DrawIndexedInstanced((UINT)std::size(CubeShader::c_cubeIndices), viewInstanceCount, 0, 0, 0);
    }
}

namespace sample {
    std::unique_ptr<sample::IGraphicsPluginD3D12> CreateGraphicsCore() {
        return std::make_unique<GraphicsCore>();
    }
} // namespace sample
