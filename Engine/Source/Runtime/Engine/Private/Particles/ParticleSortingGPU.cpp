// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

/*==============================================================================
	ParticleSortingGPU.cpp: Sorting GPU particles.
==============================================================================*/

#include "EnginePrivate.h"
#include "ParticleSortingGPU.h"
#include "ParticleSimulationGPU.h"
#include "ParticleHelper.h"
#include "UniformBuffer.h"
#include "ShaderParameters.h"
#include "GlobalShader.h"
#include "../GPUSort.h"

/*------------------------------------------------------------------------------
	Shaders used to generate particle sort keys.
------------------------------------------------------------------------------*/

/** The number of threads per group used to generate particle keys. */
#define PARTICLE_KEY_GEN_THREAD_COUNT 64

/**
 * Uniform buffer parameters for generating particle sort keys.
 */
BEGIN_UNIFORM_BUFFER_STRUCT( FParticleKeyGenParameters, )
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER( FVector4, ViewOrigin )
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER( uint32, ChunksPerGroup )
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER( uint32, ExtraChunkCount )
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER( uint32, OutputOffset )
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER( uint32, EmitterKey )
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER( uint32, KeyCount )
END_UNIFORM_BUFFER_STRUCT( FParticleKeyGenParameters )

IMPLEMENT_UNIFORM_BUFFER_STRUCT(FParticleKeyGenParameters,TEXT("ParticleKeyGen"));

typedef TUniformBufferRef<FParticleKeyGenParameters> FParticleKeyGenUniformBufferRef;

/**
 * Compute shader used to generate particle sort keys.
 */
class FParticleSortKeyGenCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FParticleSortKeyGenCS,Global);

public:

	static bool ShouldCache( EShaderPlatform Platform )
	{
		return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment( EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment )
	{
		FGlobalShader::ModifyCompilationEnvironment( Platform, OutEnvironment );
		OutEnvironment.SetDefine( TEXT("THREAD_COUNT"), PARTICLE_KEY_GEN_THREAD_COUNT );
		OutEnvironment.SetDefine( TEXT("TEXTURE_SIZE_X"), GParticleSimulationTextureSizeX );
		OutEnvironment.SetDefine( TEXT("TEXTURE_SIZE_Y"), GParticleSimulationTextureSizeY );
	}

	/** Default constructor. */
	FParticleSortKeyGenCS()
	{
	}

	/** Initialization constructor. */
	explicit FParticleSortKeyGenCS( const ShaderMetaType::CompiledShaderInitializerType& Initializer )
		: FGlobalShader(Initializer)
	{
		InParticleIndices.Bind( Initializer.ParameterMap, TEXT("InParticleIndices") );
		PositionTexture.Bind( Initializer.ParameterMap, TEXT("PositionTexture") );
		PositionTextureSampler.Bind( Initializer.ParameterMap, TEXT("PositionTextureSampler") );
		PositionZWTexture.Bind( Initializer.ParameterMap, TEXT("PositionZWTexture") );
		PositionZWTextureSampler.Bind( Initializer.ParameterMap, TEXT("PositionZWTextureSampler") );
		OutKeys.Bind( Initializer.ParameterMap, TEXT("OutKeys") );
		OutParticleIndices.Bind( Initializer.ParameterMap, TEXT("OutParticleIndices") );
	}

	/** Serialization. */
	virtual bool Serialize( FArchive& Ar ) OVERRIDE
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize( Ar );
		Ar << InParticleIndices;
		Ar << PositionTexture;
		Ar << PositionTextureSampler;
		Ar << PositionZWTexture;
		Ar << PositionZWTextureSampler;
		Ar << OutKeys;
		Ar << OutParticleIndices;
		return bShaderHasOutdatedParameters;
	}

	/**
	 * Set output buffers for this shader.
	 */
	void SetOutput( FUnorderedAccessViewRHIParamRef OutKeysUAV, FUnorderedAccessViewRHIParamRef OutIndicesUAV )
	{
		FComputeShaderRHIParamRef ComputeShaderRHI = GetComputeShader();
		if ( OutKeys.IsBound() )
		{
			RHISetUAVParameter( ComputeShaderRHI, OutKeys.GetBaseIndex(), OutKeysUAV );
		}
		if ( OutParticleIndices.IsBound() )
		{
			RHISetUAVParameter( ComputeShaderRHI, OutParticleIndices.GetBaseIndex(), OutIndicesUAV );
		}
	}

	/**
	 * Set input parameters.
	 */
	void SetParameters(
		FParticleKeyGenUniformBufferRef& UniformBuffer,
		FShaderResourceViewRHIParamRef InIndicesSRV
		)
	{
		FComputeShaderRHIParamRef ComputeShaderRHI = GetComputeShader();
		SetUniformBufferParameter( ComputeShaderRHI, GetUniformBufferParameter<FParticleKeyGenParameters>(), UniformBuffer );
		if ( InParticleIndices.IsBound() )
		{
			RHISetShaderResourceViewParameter( ComputeShaderRHI, InParticleIndices.GetBaseIndex(), InIndicesSRV );
		}
	}

	/**
	 * Set the texture from which particle positions can be read.
	 */
	void SetPositionTextures(FTexture2DRHIParamRef PositionTextureRHI, FTexture2DRHIParamRef PositionZWTextureRHI)
	{
		FComputeShaderRHIParamRef ComputeShaderRHI = GetComputeShader();
		if (PositionTexture.IsBound())
		{
			RHISetShaderTexture(ComputeShaderRHI, PositionTexture.GetBaseIndex(), PositionTextureRHI);
		}
		if (PositionZWTexture.IsBound())
		{
			RHISetShaderTexture(ComputeShaderRHI, PositionZWTexture.GetBaseIndex(), PositionZWTextureRHI);
		}
	}

	/**
	 * Unbinds any buffers that have been bound.
	 */
	void UnbindBuffers()
	{
		FComputeShaderRHIParamRef ComputeShaderRHI = GetComputeShader();
		if ( InParticleIndices.IsBound() )
		{
			RHISetShaderResourceViewParameter( ComputeShaderRHI, InParticleIndices.GetBaseIndex(), FShaderResourceViewRHIParamRef() );
		}
		if ( OutKeys.IsBound() )
		{
			RHISetUAVParameter( ComputeShaderRHI, OutKeys.GetBaseIndex(), FUnorderedAccessViewRHIParamRef() );
		}
		if ( OutParticleIndices.IsBound() )
		{
			RHISetUAVParameter( ComputeShaderRHI, OutParticleIndices.GetBaseIndex(), FUnorderedAccessViewRHIParamRef() );
		}
	}

private:

	/** Input buffer containing particle indices. */
	FShaderResourceParameter InParticleIndices;
	/** Texture containing particle positions. */
	FShaderResourceParameter PositionTexture;
	FShaderResourceParameter PositionTextureSampler;
	FShaderResourceParameter PositionZWTexture;
	FShaderResourceParameter PositionZWTextureSampler;
	/** Output key buffer. */
	FShaderResourceParameter OutKeys;
	/** Output indices buffer. */
	FShaderResourceParameter OutParticleIndices;
};
IMPLEMENT_SHADER_TYPE(,FParticleSortKeyGenCS,TEXT("ParticleSortKeyGen"),TEXT("GenerateParticleSortKeys"),SF_Compute);

/**
 * Generate sort keys for a list of particles.
 * @param KeyBufferUAV - Unordered access view of the buffer where sort keys will be stored.
 * @param SortedVertexBufferUAV - Unordered access view of the vertex buffer where particle indices will be stored.
 * @param PositionTextureRHI - The texture containing world space positions for all particles.
 * @param SimulationsToSort - A list of simulations to generate sort keys for.
 * @returns the total number of particles being sorted.
 */
static int32 GenerateParticleSortKeys(
	FUnorderedAccessViewRHIParamRef KeyBufferUAV,
	FUnorderedAccessViewRHIParamRef SortedVertexBufferUAV,
	FTexture2DRHIParamRef PositionTextureRHI,
	FTexture2DRHIParamRef PositionZWTextureRHI,
	const TArray<FParticleSimulationSortInfo>& SimulationsToSort
	)
{
	SCOPED_DRAW_EVENT(ParticleSortKeyGen, DEC_PARTICLE);

	check(GRHIFeatureLevel == ERHIFeatureLevel::SM5);

	FParticleKeyGenParameters KeyGenParameters;
	FParticleKeyGenUniformBufferRef KeyGenUniformBuffer;
	const uint32 MaxGroupCount = 128;
	int32 TotalParticleCount = 0;

	// Grab the shader, set output.
	TShaderMapRef<FParticleSortKeyGenCS> KeyGenCS(GetGlobalShaderMap());
	RHISetComputeShader(KeyGenCS->GetComputeShader());

	KeyGenCS->SetOutput(KeyBufferUAV, SortedVertexBufferUAV);
	KeyGenCS->SetPositionTextures(PositionTextureRHI, PositionZWTextureRHI);

	// For each simulation, generate keys and store them in the sorting buffers.
	const int32 SimulationCount = SimulationsToSort.Num();
	for (int32 SimulationIndex = 0; SimulationIndex < SimulationCount; ++SimulationIndex)
	{
		const FParticleSimulationSortInfo& SortInfo = SimulationsToSort[SimulationIndex];

		// Create the uniform buffer.
		const uint32 ParticleCount = SortInfo.ParticleCount;
		const uint32 AlignedParticleCount = ((ParticleCount + PARTICLE_KEY_GEN_THREAD_COUNT - 1) & (~(PARTICLE_KEY_GEN_THREAD_COUNT - 1)));
		const uint32 ChunkCount = AlignedParticleCount / PARTICLE_KEY_GEN_THREAD_COUNT;
		const uint32 GroupCount = FMath::Clamp<uint32>( ChunkCount, 1, MaxGroupCount );
		KeyGenParameters.ViewOrigin = SortInfo.ViewOrigin;
		KeyGenParameters.ChunksPerGroup = ChunkCount / GroupCount;
		KeyGenParameters.ExtraChunkCount = ChunkCount % GroupCount;
		KeyGenParameters.OutputOffset = TotalParticleCount;
		KeyGenParameters.EmitterKey = SimulationIndex << 16;
		KeyGenParameters.KeyCount = ParticleCount;
		KeyGenUniformBuffer = FParticleKeyGenUniformBufferRef::CreateUniformBufferImmediate( KeyGenParameters, UniformBuffer_SingleUse );

		// Dispatch.
		KeyGenCS->SetParameters(KeyGenUniformBuffer, SortInfo.VertexBufferSRV);
		DispatchComputeShader(*KeyGenCS, GroupCount, 1, 1);

		// Update offset in to the buffer.
		TotalParticleCount += ParticleCount;
	}

	// Clear the output buffer.
	KeyGenCS->UnbindBuffers();

	return TotalParticleCount;
}

/*------------------------------------------------------------------------------
	Buffers used to hold sorting results.
------------------------------------------------------------------------------*/

/**
 * Initialize RHI resources.
 */
void FParticleSortBuffers::InitRHI()
{
	if (GRHIFeatureLevel == ERHIFeatureLevel::SM5)
	{
		for (int32 BufferIndex = 0; BufferIndex < 2; ++BufferIndex)
		{
			FRHIResourceCreateInfo CreateInfo;

			KeyBuffers[BufferIndex] = RHICreateVertexBuffer( BufferSize * sizeof(uint32), BUF_Static | BUF_ShaderResource | BUF_UnorderedAccess, CreateInfo);
			KeyBufferSRVs[BufferIndex] = RHICreateShaderResourceView( KeyBuffers[BufferIndex], /*Stride=*/ sizeof(uint32), PF_R32_UINT );
			KeyBufferUAVs[BufferIndex] = RHICreateUnorderedAccessView( KeyBuffers[BufferIndex], PF_R32_UINT );

			VertexBuffers[BufferIndex] = RHICreateVertexBuffer( BufferSize * sizeof(uint32), BUF_Static | BUF_ShaderResource | BUF_UnorderedAccess, CreateInfo);
			VertexBufferSRVs[BufferIndex] = RHICreateShaderResourceView( VertexBuffers[BufferIndex], /*Stride=*/ sizeof(FFloat16)*2, PF_G16R16F );
			VertexBufferUAVs[BufferIndex] = RHICreateUnorderedAccessView( VertexBuffers[BufferIndex], PF_G16R16F );
			VertexBufferSortSRVs[BufferIndex] = RHICreateShaderResourceView( VertexBuffers[BufferIndex], /*Stride=*/ sizeof(uint32), PF_R32_UINT );
			VertexBufferSortUAVs[BufferIndex] = RHICreateUnorderedAccessView( VertexBuffers[BufferIndex], PF_R32_UINT );
		}
	}
}

/**
 * Release RHI resources.
 */
void FParticleSortBuffers::ReleaseRHI()
{
	for ( int32 BufferIndex = 0; BufferIndex < 2; ++BufferIndex )
	{
		KeyBufferUAVs[BufferIndex].SafeRelease();
		KeyBufferSRVs[BufferIndex].SafeRelease();
		KeyBuffers[BufferIndex].SafeRelease();

		VertexBufferSortUAVs[BufferIndex].SafeRelease();
		VertexBufferSortSRVs[BufferIndex].SafeRelease();
		VertexBufferUAVs[BufferIndex].SafeRelease();
		VertexBufferSRVs[BufferIndex].SafeRelease();
		VertexBuffers[BufferIndex].SafeRelease();
	}
}

/**
 * Retrieve buffers needed to sort on the GPU.
 */
FGPUSortBuffers FParticleSortBuffers::GetSortBuffers()
{
	FGPUSortBuffers SortBuffers;
		 
	for ( int32 BufferIndex = 0; BufferIndex < 2; ++BufferIndex )
	{
		SortBuffers.RemoteKeySRVs[BufferIndex] = KeyBufferSRVs[BufferIndex];
		SortBuffers.RemoteKeyUAVs[BufferIndex] = KeyBufferUAVs[BufferIndex];
		SortBuffers.RemoteValueSRVs[BufferIndex] = VertexBufferSortSRVs[BufferIndex];
		SortBuffers.RemoteValueUAVs[BufferIndex] = VertexBufferSortUAVs[BufferIndex];
	}

	return SortBuffers;
}

/*------------------------------------------------------------------------------
	Public interface.
------------------------------------------------------------------------------*/

/**
 * Sort particles on the GPU.
 * @param ParticleSortBuffers - Buffers to use while sorting GPU particles.
 * @param PositionTextureRHI - Texture containing world space position for all particles.
 * @param SimulationsToSort - A list of simulations that must be sorted.
 * @returns the buffer index in which sorting results are stored.
 */
int32 SortParticlesGPU(
	FParticleSortBuffers& ParticleSortBuffers,
	FTexture2DRHIParamRef PositionTextureRHI,
	FTexture2DRHIParamRef PositionZWTextureRHI,
	const TArray<FParticleSimulationSortInfo>& SimulationsToSort
	)
{
	SCOPED_DRAW_EVENTF(ParticleSort, DEC_PARTICLE, TEXT("ParticleSort_%d"), SimulationsToSort.Num());

	check(GRHIFeatureLevel == ERHIFeatureLevel::SM5);

	// Ensure the sorted vertex buffers are not currently bound as input streams.
	// They should only ever be bound to streams 0 or 1, so clear them.
	{
		const int32 StreamCount = 2;
		for (int32 StreamIndex = 0; StreamIndex < StreamCount; ++StreamIndex)
		{
			RHISetStreamSource(StreamIndex, FVertexBufferRHIParamRef(), 0, 0);
		}
	}

	// First generate keys for each emitter to be sorted.
	const int32 TotalParticleCount = GenerateParticleSortKeys(
		ParticleSortBuffers.GetKeyBufferUAV(),
		ParticleSortBuffers.GetVertexBufferUAV(),
		PositionTextureRHI,
		PositionZWTextureRHI,
		SimulationsToSort
		);

	// Update stats.
	INC_DWORD_STAT_BY( STAT_SortedGPUEmitters, SimulationsToSort.Num() );
	INC_DWORD_STAT_BY( STAT_SortedGPUParticles, TotalParticleCount );

	// Now sort the particles based on the generated keys.
	const uint32 EmitterKeyMask = (1 << FMath::CeilLogTwo( SimulationsToSort.Num() )) - 1;
	const uint32 KeyMask = (EmitterKeyMask << 16) | 0xFFFF;
	FGPUSortBuffers SortBuffers = ParticleSortBuffers.GetSortBuffers();
	return SortGPUBuffers( SortBuffers, 0, KeyMask, TotalParticleCount );
}
