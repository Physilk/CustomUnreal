#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "Runtime/Renderer/Private/PostProcess/RenderingCompositionGraph.h"


// ePId_Input0: HDR SceneColor
// derives from TRenderingCompositePassBase<InputCount, OutputCount> 
class FRCPassPostProcessVisualizePencilEffect : public TRenderingCompositePassBase<1, 1>
{
public:
    // interface FRenderingCompositePass ---------
    virtual void Process(FRenderingCompositePassContext& Context) override;
    virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;
    virtual void Release() override { delete this; }
};