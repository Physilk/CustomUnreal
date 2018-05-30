#include "PostProcessTooning.h"
#include "Runtime/Renderer/Private/PostProcess/PostProcessing.h"
#include "StaticBoundShaderState.h"
#include "SceneUtils.h"
#include "Runtime/Renderer/Private/PostProcess/SceneRenderTargets.h"
#include "Runtime/Renderer/Private/PostProcess/SceneFilterRendering.h"
#include "Runtime/Renderer/Private/PostProcess/PostProcessing.h"
#include "Runtime/Renderer/Private/PostProcess/PostProcessEyeAdaptation.h"
#include "ClearQuad.h"
#include "PipelineStateCache.h"

class FPostProcessTooningPS : public FGlobalShader
{
public:
    DECLARE_EXPORTED_SHADER_TYPE(FPostProcessTooningPS, Global, /*MYMODULE_API*/);

    static bool ShouldCache(EShaderPlatform Platform)
    {
        return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM4);
    }

    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& PermutationParams, FShaderCompilerEnvironment& OutEnvironment)
    {
        FGlobalShader::ModifyCompilationEnvironment(PermutationParams, OutEnvironment);
        OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL"), 1);
    }

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return true;
    }

    FPostProcessTooningPS() {}

    FPostProcessTooningPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
        : FGlobalShader(Initializer)
    {
        PostprocessParameter.Bind(Initializer.ParameterMap);
        ColorSteps.Bind(Initializer.ParameterMap, TEXT("colorSteps"));
        DeferredParameters.Bind(Initializer.ParameterMap);
    }

    template <typename TRHICmdList>
    void SetParameters(TRHICmdList& RHICmdList, const FRenderingCompositePassContext& Context)
    {
        const FPixelShaderRHIParamRef ShaderRHI = GetPixelShader();

        FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);

        PostprocessParameter.SetPS(RHICmdList, ShaderRHI, Context, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
        {
            float Value = 7.0f;
            SetShaderValue(RHICmdList, ShaderRHI, ColorSteps, Value);
        }
        DeferredParameters.Set(RHICmdList, ShaderRHI, Context.View, MD_PostProcess);
    }

    virtual bool Serialize(FArchive& Ar) override
    {
        bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
        Ar << PostprocessParameter << ColorSteps << DeferredParameters;
        return bShaderHasOutdatedParameters;
    }

    static const TCHAR* GetSourceFilename()
    {
        return TEXT("/Engine/Private/PostProcessTooning.usf");
    }

    static const TCHAR* GetFunctionName()
    {
        return TEXT("MainTooningPS");
    }

private:
    FPostProcessPassParameters PostprocessParameter;
    FShaderParameter ColorSteps;
    FDeferredPixelShaderParameters DeferredParameters;
};

IMPLEMENT_SHADER_TYPE3(FPostProcessTooningPS, SF_Pixel)


void FRCPassPostProcessTooning::Process(FRenderingCompositePassContext& Context)
{
    SCOPED_DRAW_EVENT(Context.RHICmdList, VisualizeEdge);

    const FPooledRenderTargetDesc* InputDesc = GetInputDesc(ePId_Input0);

    check(InputDesc && "Input is not hooked up correctly");

    const FViewInfo& View = Context.View;

    FIntPoint SrcSize = InputDesc->Extent;
    FIntPoint DestSize = PassOutputs[0].RenderTargetDesc.Extent;

    // e.g. 4 means the input texture is 4x smaller than the buffer size
    uint32 ScaleFactor = FMath::DivideAndRoundUp(FSceneRenderTargets::Get(Context.RHICmdList).GetBufferSizeXY().Y, SrcSize.Y);

    FIntRect SrcRect = FIntRect::DivideAndRoundUp(View.ViewRect, ScaleFactor);
    FIntRect DestRect = SrcRect;

    const FSceneRenderTargetItem& DestRenderTarget = PassOutputs[0].RequestSurface(Context);
    FRHIRenderTargetView RtView = FRHIRenderTargetView(DestRenderTarget.TargetableTexture, ERenderTargetLoadAction::ENoAction);
    FRHISetRenderTargetsInfo Info(1, &RtView, FRHIDepthRenderTargetView());
    Context.RHICmdList.SetRenderTargetsAndClear(Info);
    Context.SetViewportAndCallRHI(DestRect.Min.X, DestRect.Min.Y, 0.0f, DestRect.Width(), DestRect.Height(), 1.0f);

    FGraphicsPipelineStateInitializer GraphicsPSOInit;
    Context.RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

    // set the state
    GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
    GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
    GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

    TShaderMapRef<FPostProcessVS> VertexShader(Context.GetShaderMap());
    TShaderMapRef<FPostProcessTooningPS> PixelShader(Context.GetShaderMap());

    GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
    GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
    GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
    GraphicsPSOInit.PrimitiveType = PT_TriangleList;

    SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);

    PixelShader->SetParameters(Context.RHICmdList, Context);
    VertexShader->SetParameters(Context);

    // Draw a quad mapping scene color to the view's render target
    DrawRectangle(
        Context.RHICmdList,
        DestRect.Min.X, DestRect.Min.Y,
        DestRect.Width(), DestRect.Height(),
        SrcRect.Min.X, SrcRect.Min.Y,
        SrcRect.Width(), SrcRect.Height(),
        DestRect.Size(),
        SrcSize,
        *VertexShader,
        EDRF_UseTriangleOptimization);

    Context.RHICmdList.CopyToResolveTarget(DestRenderTarget.TargetableTexture, DestRenderTarget.ShaderResourceTexture, false, FResolveParams());
    // we no longer need the GBuffer
    FSceneRenderTargets::Get(Context.RHICmdList).AdjustGBufferRefCount(Context.RHICmdList, -1);
}

FPooledRenderTargetDesc FRCPassPostProcessTooning::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
    FPooledRenderTargetDesc Ret = GetInput(ePId_Input0)->GetOutput()->RenderTargetDesc;

    Ret.Reset();
    Ret.TargetableFlags &= ~TexCreate_UAV;
    Ret.TargetableFlags |= TexCreate_RenderTargetable;
    Ret.DebugName = TEXT("Tooning");

    return Ret;
}
