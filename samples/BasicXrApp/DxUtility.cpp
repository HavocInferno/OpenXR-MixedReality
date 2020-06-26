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
#include "DxUtility.h"
#include <D3Dcompiler.h>
#pragma comment(lib, "D3DCompiler.lib")
#include <filesystem>

namespace sample::dx {
    //-----------------------------------------------------------------------------
    // Purpose: Outputs a set of optional arguments to debugging output, using
    //          the printf format setting specified in fmt*.
    //-----------------------------------------------------------------------------
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

    winrt::com_ptr<IDXGIAdapter1> GetAdapter(LUID adapterId) {
        // Create the DXGI factory.
        winrt::com_ptr<IDXGIFactory1> dxgiFactory;
        CHECK_HRCMD(CreateDXGIFactory1(winrt::guid_of<IDXGIFactory1>(), dxgiFactory.put_void()));

        for (UINT adapterIndex = 0;; adapterIndex++) {
            // EnumAdapters1 will fail with DXGI_ERROR_NOT_FOUND when there are no more adapters to enumerate.
            winrt::com_ptr<IDXGIAdapter1> dxgiAdapter;
            CHECK_HRCMD(dxgiFactory->EnumAdapters1(adapterIndex, dxgiAdapter.put()));

            DXGI_ADAPTER_DESC1 adapterDesc;
            CHECK_HRCMD(dxgiAdapter->GetDesc1(&adapterDesc));
            if (memcmp(&adapterDesc.AdapterLuid, &adapterId, sizeof(adapterId)) == 0) {
                DEBUG_PRINT("Using graphics adapter %ws", adapterDesc.Description);
                return dxgiAdapter;
            }
        }
    }

    /*void CreateD3D11DeviceAndContext(IDXGIAdapter1* adapter,
                                     const std::vector<D3D_FEATURE_LEVEL>& featureLevels,
                                     ID3D11Device** device,
                                     ID3D11DeviceContext** deviceContext) {
        UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

#ifdef _DEBUG
        creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        // Create the Direct3D 11 API device object and a corresponding context.
        D3D_DRIVER_TYPE driverType = adapter == nullptr ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_UNKNOWN;

    TryAgain:
        const HRESULT hr = D3D11CreateDevice(adapter,
                                             driverType,
                                             0,
                                             creationFlags,
                                             featureLevels.data(),
                                             (UINT)featureLevels.size(),
                                             D3D11_SDK_VERSION,
                                             device,
                                             nullptr,
                                             deviceContext);

        if (FAILED(hr)) {
            // If initialization failed, it may be because device debugging isn't supprted, so retry without that.
            if ((creationFlags & D3D11_CREATE_DEVICE_DEBUG) && (hr == DXGI_ERROR_SDK_COMPONENT_MISSING)) {
                creationFlags &= ~D3D11_CREATE_DEVICE_DEBUG;
                goto TryAgain;
            }

            // If the initialization still fails, fall back to the WARP device.
            // For more information on WARP, see: http://go.microsoft.com/fwlink/?LinkId=286690
            if (driverType != D3D_DRIVER_TYPE_WARP) {
                driverType = D3D_DRIVER_TYPE_WARP;
                goto TryAgain;
            }
        }
    }*/

    winrt::com_ptr<ID3DBlob> CompileShader(const char* hlsl, const char* entrypoint, const char* shaderTarget) {
        winrt::com_ptr<ID3DBlob> compiled;
        winrt::com_ptr<ID3DBlob> errMsgs;
        DWORD flags = D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR | D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS;

#ifdef _DEBUG
        flags |= D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_DEBUG;
#else
        flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

        HRESULT hr =
            D3DCompile(hlsl, strlen(hlsl), nullptr, nullptr, nullptr, entrypoint, shaderTarget, flags, 0, compiled.put(), errMsgs.put());
        if (FAILED(hr)) {
            std::string errMsg((const char*)errMsgs->GetBufferPointer(), errMsgs->GetBufferSize());
            DEBUG_PRINT("D3DCompile failed %X: %s", hr, errMsg.c_str());
            CHECK_HRESULT(hr, "D3DCompile failed");
        }

        return compiled;
    }

    bool CreateAllShaders(winrt::com_ptr<ID3D12Device>& pDevice,
                          winrt::com_ptr<ID3D12RootSignature>& pRootSignature,
                          int nMSAASampleCount,
                          winrt::com_ptr<ID3D12PipelineState>& pScenePipelineState,
                          //winrt::com_ptr<ID3D12PipelineState>& pCompanionPipelineState, //TODO: exclude companion window items for now
                          winrt::com_ptr<ID3D12PipelineState>& pAxesPipelineState,
                          winrt::com_ptr<ID3D12PipelineState>& pRenderModelPipelineState) {
        // adapted from Valve OpenVR sample CMainApplication::CreateAllShaders()

        // Root signature
        {
            D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
            if (FAILED(pDevice->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData)))) {
                featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
            }

            CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
            CD3DX12_ROOT_PARAMETER1 rootParameters[2];

            ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
            ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);
            rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_VERTEX);
            rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);

            D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
                D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

            D3D12_STATIC_SAMPLER_DESC sampler = {};
            sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
            sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
            sampler.MaxLOD = D3D12_FLOAT32_MAX;
            sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
            rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, &sampler, rootSignatureFlags);
            winrt::com_ptr<ID3DBlob> signature;
            winrt::com_ptr<ID3DBlob> error;
            D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, signature.put(), error.put());
            pDevice->CreateRootSignature(
                0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(pRootSignature.put()));
        }

        // Scene shader
        {
            winrt::com_ptr<ID3DBlob> vertexShader;
            winrt::com_ptr<ID3DBlob> pixelShader;
            UINT compileFlags = 0;

            std::string shaderPath = std::filesystem::current_path().string() + "/shaders/scene.hlsl"; // Path_MakeAbsolute("../shaders/scene.hlsl", sExecutableDirectory);
            std::wstring shaderPathW = std::wstring(shaderPath.begin(), shaderPath.end());
            winrt::com_ptr<ID3DBlob> error;
            if (FAILED(D3DCompileFromFile(
                    shaderPathW.c_str(), nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, vertexShader.put(), error.put()))) {
                dprintf("Failed compiling vertex shader '%s':\n%s\n", shaderPath.c_str(), (char*)error->GetBufferPointer());
                return false;
            }
            if (FAILED(D3DCompileFromFile(
                    shaderPathW.c_str(), nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, pixelShader.put(), error.put()))) {
                dprintf("Failed compiling pixel shader '%s':\n%s\n", shaderPath.c_str(), (char*)error->GetBufferPointer());
                return false;
            }

            // Define the vertex input layout.
            D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            };

            // Describe and create the graphics pipeline state object (PSO).
            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
            psoDesc.InputLayout = {inputElementDescs, _countof(inputElementDescs)};
            psoDesc.pRootSignature = pRootSignature.get();
            psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.get());
            psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.get());
            psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
            psoDesc.RasterizerState.MultisampleEnable = TRUE;
            psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            psoDesc.SampleDesc.Count = nMSAASampleCount;
            psoDesc.SampleDesc.Quality = 0;
            if (FAILED(pDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(pScenePipelineState.put())))) {
                dprintf("Error creating D3D12 pipeline state.\n");
                return false;
            }
        }

        // Companion shader //TODO: exclude companion window items for now
        {
            /*
            winrt::com_ptr<ID3DBlob> vertexShader;
            winrt::com_ptr<ID3DBlob> pixelShader;
            UINT compileFlags = 0;

            std::string shaderPath = std::filesystem::current_path().string() + "/shaders/companion.hlsl"; // Path_MakeAbsolute("../shaders/companion.hlsl", sExecutableDirectory);
            std::wstring shaderPathW = std::wstring(shaderPath.begin(), shaderPath.end());
            winrt::com_ptr<ID3DBlob> error;
            if (FAILED(D3DCompileFromFile(
                    shaderPathW.c_str(), nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, vertexShader.put(), error.put()))) {
                dprintf("Failed compiling vertex shader '%s':\n%s\n", shaderPath.c_str(), (char*)error->GetBufferPointer());
                return false;
            }
            if (FAILED(D3DCompileFromFile(
                    shaderPathW.c_str(), nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, pixelShader.put(), error.put()))) {
                dprintf("Failed compiling pixel shader '%s':\n%s\n", shaderPath.c_str(), (char*)error->GetBufferPointer());
                return false;
            }

            // Define the vertex input layout.
            D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            };

            // Describe and create the graphics pipeline state object (PSO).
            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
            psoDesc.InputLayout = {inputElementDescs, _countof(inputElementDescs)};
            psoDesc.pRootSignature = m_pRootSignature.get();
            psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.get());
            psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.get());
            psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
            psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            psoDesc.DepthStencilState.DepthEnable = FALSE;
            psoDesc.DepthStencilState.StencilEnable = FALSE;
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            psoDesc.SampleDesc.Count = 1;
            if (FAILED(pDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(pCompanionPipelineState.put())))) {
                dprintf("Error creating D3D12 pipeline state.\n");
                return false;
            }
            */
        }

        // Axes shader
        {
            winrt::com_ptr<ID3DBlob> vertexShader;
            winrt::com_ptr<ID3DBlob> pixelShader;
            UINT compileFlags = 0;

            std::string shaderPath = std::filesystem::current_path().string() + "/shaders/axes.hlsl"; // Path_MakeAbsolute("../shaders/axes.hlsl", sExecutableDirectory);
            std::wstring shaderPathW = std::wstring(shaderPath.begin(), shaderPath.end());
            winrt::com_ptr<ID3DBlob> error;
            if (FAILED(D3DCompileFromFile(
                    shaderPathW.c_str(), nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, vertexShader.put(), error.put()))) {
                dprintf("Failed compiling vertex shader '%s':\n%s\n", shaderPath.c_str(), (char*)error->GetBufferPointer());
                return false;
            }
            if (FAILED(D3DCompileFromFile(
                    shaderPathW.c_str(), nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, pixelShader.put(), error.put()))) {
                dprintf("Failed compiling pixel shader '%s':\n%s\n", shaderPath.c_str(), (char*)error->GetBufferPointer());
                return false;
            }

            // Define the vertex input layout.
            D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            };

            // Describe and create the graphics pipeline state object (PSO).
            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
            psoDesc.InputLayout = {inputElementDescs, _countof(inputElementDescs)};
            psoDesc.pRootSignature = pRootSignature.get();
            psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.get());
            psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.get());
            psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
            psoDesc.RasterizerState.MultisampleEnable = TRUE;
            psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            psoDesc.SampleDesc.Count = nMSAASampleCount;
            psoDesc.SampleDesc.Quality = 0;
            if (FAILED(pDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(pAxesPipelineState.put())))) {
                dprintf("Error creating D3D12 pipeline state.\n");
                return false;
            }
        }

        // Render Model shader
        {
            winrt::com_ptr<ID3DBlob> vertexShader;
            winrt::com_ptr<ID3DBlob> pixelShader;
            UINT compileFlags = 0;

            std::string shaderPath = std::filesystem::current_path().string() + "/shaders/rendermodel.hlsl"; //Path_MakeAbsolute("../shaders/rendermodel.hlsl", sExecutableDirectory);
            std::wstring shaderPathW = std::wstring(shaderPath.begin(), shaderPath.end());
            winrt::com_ptr<ID3DBlob> error;
            if (FAILED(D3DCompileFromFile(
                    shaderPathW.c_str(), nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, vertexShader.put(), error.put()))) {
                dprintf("Failed compiling vertex shader '%s':\n%s\n", shaderPath.c_str(), (char*)error->GetBufferPointer());
                return false;
            }
            if (FAILED(
                    D3DCompileFromFile(shaderPathW.c_str(), nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, pixelShader.put(), error.put()))) {
                dprintf("Failed compiling pixel shader '%s':\n%s\n", shaderPath.c_str(), (char*)error->GetBufferPointer());
                return false;
            }

            // Define the vertex input layout.
            D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            };

            // Describe and create the graphics pipeline state object (PSO).
            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
            psoDesc.InputLayout = {inputElementDescs, _countof(inputElementDescs)};
            psoDesc.pRootSignature = pRootSignature.get();
            psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.get());
            psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.get());
            psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
            psoDesc.RasterizerState.MultisampleEnable = TRUE;
            psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            psoDesc.SampleDesc.Count = nMSAASampleCount;
            psoDesc.SampleDesc.Quality = 0;
            if (FAILED(pDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(pRenderModelPipelineState.put())))) {
                dprintf("Error creating D3D12 pipeline state.\n");
                return false;
            }
        }

        return true;
    }
} // namespace sample::dx
