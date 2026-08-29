#include "dxc_compiler.h"

DxcCompiler::DxcCompiler()
{
    HRESULT hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
    assert(SUCCEEDED(hr));
}

DxcCompiler::~DxcCompiler()
{
    dxcCompiler->Release();
}

IDxcBlob* DxcCompiler::compile(const std::string& shaderSource, bool compilePixelShader, bool compileLibrary, bool compileSpirv)
{
    DxcBuffer source{};
    source.Ptr = shaderSource.c_str();
    source.Size = shaderSource.size();

    const wchar_t* args[32]{};
    uint32_t argCount = 0;

    const wchar_t* target = nullptr;
    if (compileLibrary)
    {
        assert(!compileSpirv);
        target = L"-T lib_6_3";
    }
    else
    {
        if (compilePixelShader)
            target = L"-T ps_6_0";
        else
            // 6.1, not 6.0: SV_ViewID is a shader model 6.1 semantic and the
            // multiview stereo path declares it in every vertex shader. DXIL
            // rejects it at 6.0 outright, and SPIR-V needs the same profile so
            // both backends stay in step.
            target = L"-T vs_6_1";
    }

    args[argCount++] = target;
    args[argCount++] = L"-HV 2021";
    args[argCount++] = L"-all-resources-bound";

    if (compileSpirv)
    {
        args[argCount++] = L"-spirv";
        args[argCount++] = L"-fvk-use-dx-layout";
        // DXC defaults its SPIR-V target to vulkan1.0, where the MultiView
        // capability does not exist - so SV_ViewID lowers to a gl_ViewIndex
        // the module is not allowed to declare, and the view index reads as
        // zero for every view. That is the silent failure behind a multiview
        // pass rendering view 0 and leaving every other layer cleared. Every
        // target this runs on is Vulkan 1.1 or later (Adreno 650 included).
        args[argCount++] = L"-fspv-target-env=vulkan1.1";

        if (!compilePixelShader)
            args[argCount++] = L"-fvk-invert-y";
    }
    else
    {
        args[argCount++] = L"-Wno-ignored-attributes";
        args[argCount++] = L"-Qstrip_reflect";
    }

    args[argCount++] = L"-Qstrip_debug";

    // Keep the HLSL preprocessor in step with how this tool was built so
    // shader_common.h selects the matching constant layout.
#ifdef REBLUE_RECOMP
    args[argCount++] = L"-DREBLUE_RECOMP";
#elif defined(UNLEASHED_RECOMP)
    args[argCount++] = L"-DUNLEASHED_RECOMP";
#endif

    IDxcResult* result = nullptr;
    HRESULT hr = dxcCompiler->Compile(&source, args, argCount, nullptr, IID_PPV_ARGS(&result));

    IDxcBlob* object = nullptr;
    if (SUCCEEDED(hr))
    {
        assert(result != nullptr);

        HRESULT status;
        hr = result->GetStatus(&status);
        assert(SUCCEEDED(hr));

        if (FAILED(status))
        {
            if (result->HasOutput(DXC_OUT_ERRORS))
            {
                IDxcBlobUtf8* errors = nullptr;
                hr = result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
                assert(SUCCEEDED(hr) && errors != nullptr);

                fputs(errors->GetStringPointer(), stderr);

                errors->Release();
            }
        }
        else
        {
            hr = result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr);
            assert(SUCCEEDED(hr) && object != nullptr);
        }

        result->Release();
    }
    else
    {
        assert(result == nullptr);
    }

    return object;
}
