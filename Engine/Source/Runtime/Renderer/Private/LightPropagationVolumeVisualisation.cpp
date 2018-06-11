//-----------------------------------------------------------------------------
// File:		LightPropagationVolumeVisualisation.cpp
//
// Summary:		Light Propagation Volume visualisation support 
//
// Created:		2013-03-01
//
// Author:		mailto:benwood@microsoft.com
//
//				Copyright (C) Microsoft. All rights reserved.
//-----------------------------------------------------------------------------

#include "CoreMinimal.h"
#include "ShaderParameters.h"
#include "ShaderParameterUtils.h"
#include "Shader.h"
#include "StaticBoundShaderState.h"
#include "SceneUtils.h"
#include "RHIStaticStates.h"
#include "GlobalShader.h"
#include "SceneRendering.h"
#include "LightPropagationVolume.h"
#include "PipelineStateCache.h"

// ----------------------------------------------------------------------------

class FLpvVisualiseBase : public FGlobalShader
{
public:
	// Default constructor
	FLpvVisualiseBase()	{	}

	// Initialization constructor 
	explicit FLpvVisualiseBase( const ShaderMetaType::CompiledShaderInitializerType& Initializer )
		: FGlobalShader(Initializer)
	{
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)				
	{ 
		OutEnvironment.SetDefine( TEXT("LPV_MULTIPLE_BOUNCES"), (uint32)LPV_MULTIPLE_BOUNCES );
		OutEnvironment.SetDefine( TEXT("LPV_GV_SH_ORDER"),		(uint32)LPV_GV_SH_ORDER );

		FGlobalShader::ModifyCompilationEnvironment( Parameters, OutEnvironment ); 
	}
};


class FLpvVisualiseGS : public FLpvVisualiseBase
{
public:
	DECLARE_SHADER_TYPE(FLpvVisualiseGS,Global);

	FLpvVisualiseGS()																												{}
	explicit FLpvVisualiseGS( const ShaderMetaType::CompiledShaderInitializerType& Initializer ) : FLpvVisualiseBase(Initializer)	{}
	virtual bool Serialize( FArchive& Ar ) override																					{ return FLpvVisualiseBase::Serialize( Ar ); }

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && RHISupportsGeometryShaders(Parameters.Platform) && IsLPVSupported(Parameters.Platform);
	}

	void SetParameters(
		FRHICommandList& RHICmdList, 
		const FSceneView& View )
	{
		FGeometryShaderRHIParamRef ShaderRHI = GetGeometryShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);
	}
};

class FLpvVisualiseVS : public FLpvVisualiseBase
{
public:
	DECLARE_SHADER_TYPE(FLpvVisualiseVS,Global);

	FLpvVisualiseVS()	{	}
	explicit FLpvVisualiseVS( const ShaderMetaType::CompiledShaderInitializerType& Initializer ) : FLpvVisualiseBase(Initializer) {}
	virtual bool Serialize( FArchive& Ar ) override																					{ return FLpvVisualiseBase::Serialize( Ar ); }

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)		{ return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && IsLPVSupported(Parameters.Platform); }

	void SetParameters(
		FRHICommandList& RHICmdList, 
		const FSceneView& View )
	{
		FVertexShaderRHIParamRef ShaderRHI = GetVertexShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);
	}
};

class FLpvVisualisePS : public FLpvVisualiseBase
{
public:
	DECLARE_SHADER_TYPE(FLpvVisualisePS,Global);

	FLpvVisualisePS()	{	}
	explicit FLpvVisualisePS( const ShaderMetaType::CompiledShaderInitializerType& Initializer ) : FLpvVisualiseBase(Initializer) 
	{
		for ( int i = 0; i < 7; i++ )
		{
			LpvBufferSRVParameters[i].Bind( Initializer.ParameterMap, LpvVolumeTextureSRVNames[i] );
		}

		LpvVolumeTextureSampler.Bind(Initializer.ParameterMap, TEXT("gLpv3DTextureSampler"));
		for ( int i = 0; i < NUM_GV_TEXTURES; i++ ) 
		{
			GvBufferSRVParameters[i].Bind( Initializer.ParameterMap, LpvGvVolumeTextureSRVNames[i] );
		}
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)		{ return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && IsLPVSupported(Parameters.Platform); }

	void SetParameters(
		FRHICommandList& RHICmdList, 
		const FLightPropagationVolume* LPV,
		const FSceneView& View )
	{
		FPixelShaderRHIParamRef ShaderRHI = GetPixelShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);
		
		for ( int i = 0; i < 7; i++ )
		{
			FTextureRHIParamRef LpvBufferSrv = LPV->LpvVolumeTextures[ 1-LPV->mWriteBufferIndex ][i]->GetRenderTargetItem().ShaderResourceTexture;
			if ( LpvBufferSRVParameters[i].IsBound() )
			{
				RHICmdList.SetShaderTexture(ShaderRHI, LpvBufferSRVParameters[i].GetBaseIndex(), LpvBufferSrv);
			}
			SetTextureParameter(RHICmdList, ShaderRHI, LpvBufferSRVParameters[i], LpvVolumeTextureSampler, TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(), LpvBufferSrv );
		}
		
		for ( int i = 0; i < NUM_GV_TEXTURES; i++ ) 
		{
			FTextureRHIParamRef GvBufferSrv = LPV->GvVolumeTextures[i]->GetRenderTargetItem().ShaderResourceTexture;
			if ( GvBufferSRVParameters[i].IsBound() )
			{
				RHICmdList.SetShaderTexture(ShaderRHI, GvBufferSRVParameters[i].GetBaseIndex(), GvBufferSrv);
			}
			SetTextureParameter(RHICmdList, ShaderRHI, GvBufferSRVParameters[i], LpvVolumeTextureSampler, TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(), GvBufferSrv );
		}

	}


	// Serialization
	virtual bool Serialize( FArchive& Ar ) override
	{
		bool bShaderHasOutdatedParameters = FLpvVisualiseBase::Serialize( Ar );
		
		for ( int i = 0; i < 7; i++ )
		{
			Ar << LpvBufferSRVParameters[i];
		}

		Ar << LpvVolumeTextureSampler;

		for ( int i = 0; i < NUM_GV_TEXTURES; i++ ) 
		{
			Ar << GvBufferSRVParameters[i];
		}
		return bShaderHasOutdatedParameters;
	}
	FShaderResourceParameter LpvBufferSRVParameters[7];

	FShaderResourceParameter LpvVolumeTextureSampler;

	FShaderResourceParameter GvBufferSRVParameters[3];

	void UnbindBuffers(FRHICommandList& RHICmdList)
	{
		// TODO: Is this necessary here?
		FPixelShaderRHIParamRef ShaderRHI = GetPixelShader();
		for ( int i = 0; i < 7; i++ )
		{
			if ( LpvBufferSRVParameters[i].IsBound() )
			{
				RHICmdList.SetShaderTexture(ShaderRHI, LpvBufferSRVParameters[i].GetBaseIndex(), FTextureRHIParamRef());
			}
		}
		
		for ( int i = 0; i < NUM_GV_TEXTURES; i++ ) 
		{
			if ( GvBufferSRVParameters[i].IsBound() )
			{
				RHICmdList.SetShaderTexture(ShaderRHI, GvBufferSRVParameters[i].GetBaseIndex(), FTextureRHIParamRef());
			}
		}
	}

};


IMPLEMENT_SHADER_TYPE(,FLpvVisualiseGS,TEXT("/Engine/Private/LPVVisualise.usf"),TEXT("GShader"),SF_Geometry);
IMPLEMENT_SHADER_TYPE(,FLpvVisualiseVS,TEXT("/Engine/Private/LPVVisualise.usf"),TEXT("VShader"),SF_Vertex);
IMPLEMENT_SHADER_TYPE(,FLpvVisualisePS,TEXT("/Engine/Private/LPVVisualise.usf"),TEXT("PShader"),SF_Pixel);


void FLightPropagationVolume::Visualise(FRHICommandList& RHICmdList, const FViewInfo& View) const
{
	SCOPED_DRAW_EVENT(RHICmdList, LpvVisualise);
	check(View.GetFeatureLevel() == ERHIFeatureLevel::SM5);

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
	GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_One>::GetRHI();

	TShaderMapRef<FLpvVisualiseVS> VertexShader(View.ShaderMap);
	TOptionalShaderMapRef<FLpvVisualiseGS> GeometryShader(View.ShaderMap);
	TShaderMapRef<FLpvVisualisePS> PixelShader(View.ShaderMap);

	EPrimitiveType PrimType = PT_PointList;
	uint32 NumPrims = 1;
	if (!RHISupportsGeometryShaders(View.GetShaderPlatform()))
	{
		PrimType = PT_TriangleList;
		NumPrims = 2;
	}
	
	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GSimpleElementVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
	GraphicsPSOInit.BoundShaderState.GeometryShaderRHI = GETSAFERHISHADER_GEOMETRY(*GeometryShader);
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
	GraphicsPSOInit.PrimitiveType = PrimType;

	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

	VertexShader->SetParameters(RHICmdList, View);
	if (GeometryShader.IsValid())
	{
		GeometryShader->SetParameters(RHICmdList, View);
	}
	PixelShader->SetParameters(RHICmdList, this, View);

	RHICmdList.SetStreamSource(0, NULL, 0);
	RHICmdList.DrawPrimitive(PrimType, 0, NumPrims, 32 * 3);

	PixelShader->UnbindBuffers(RHICmdList);
}
