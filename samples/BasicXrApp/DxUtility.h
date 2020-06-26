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

#include <d3dcommon.h>  //ID3DBlob

namespace sample::dx {
    //-----------------------------------------------------------------------------
    // Purpose: Outputs a set of optional arguments to debugging output, using
    //          the printf format setting specified in fmt*.
    //-----------------------------------------------------------------------------
    static bool g_bPrintf = true;
    void dprintf(const char* fmt, ...); 

    winrt::com_ptr<IDXGIAdapter1> GetAdapter(LUID adapterId);

    /*void CreateD3D11DeviceAndContext(IDXGIAdapter1* adapter,
                                     const std::vector<D3D_FEATURE_LEVEL>& featureLevels,
                                     ID3D11Device** device,
                                     ID3D11DeviceContext** deviceContext);*/

    winrt::com_ptr<ID3DBlob> CompileShader(const char* hlsl, const char* entrypoint, const char* shaderTarget);

    bool CreateAllShaders(winrt::com_ptr<ID3D12Device>& pDevice,
                          winrt::com_ptr<ID3D12RootSignature>& pRootSignature,
                          int nMSAASampleCount,
                          winrt::com_ptr<ID3D12PipelineState>& pScenePipelineState,
                          //winrt::com_ptr<ID3D12PipelineState>& pCompanionPipelineState, //TODO: exclude companion window items for now
                          winrt::com_ptr<ID3D12PipelineState>& pAxesPipelineState,
                          winrt::com_ptr<ID3D12PipelineState>& m_pRenderModelPipelineState);
} // namespace sample::dx
