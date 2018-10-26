// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UniformBuffer.h"
#include "VertexFactory.h"

/**
 * Uniform buffer to hold parameters for point cloud rendering
 */
BEGIN_UNIFORM_BUFFER_STRUCT( FPointCloudVertexFactoryParameters, )
	UNIFORM_MEMBER_SRV(Buffer<float>, VertexFetch_PointLocationBuffer)
	UNIFORM_MEMBER_SRV(Buffer<float4>, VertexFetch_PointColorBuffer)
END_UNIFORM_BUFFER_STRUCT( FPointCloudVertexFactoryParameters )
typedef TUniformBufferRef<FPointCloudVertexFactoryParameters> FPointCloudVertexFactoryBufferRef;

/**
 * Vertex factory for point cloud rendering. This base version uses the dummy color buffer
 */
class FPointCloudVertexFactory :
	public FVertexFactory
{
	DECLARE_VERTEX_FACTORY_TYPE(FPointCloudVertexFactory);

public:
	FPointCloudVertexFactory(ERHIFeatureLevel::Type InFeatureLevel)
		: FVertexFactory(InFeatureLevel)
	{
	}

	/**
	 * Constructs render resources for this vertex factory.
	 */
	virtual void InitRHI() override;

	/**
	 * Release render resources for this vertex factory.
	 */
	virtual void ReleaseRHI() override;

	/**
	 * Should we cache the material's shadertype on this platform with this vertex factory?
	 */
	static bool ShouldCompilePermutation(EShaderPlatform Platform, const class FMaterial* Material, const class FShaderType* ShaderType);

	/**
	 * Construct shader parameters for this type of vertex factory.
	 */
	static FVertexFactoryShaderParameters* ConstructShaderParameters(EShaderFrequency ShaderFrequency);

	/**
	 * Set parameters for this vertex factory instance.
	 */
	void SetParameters(const FPointCloudVertexFactoryParameters& InUniformParameters, const uint32 InMask, float InSize);

	inline const FUniformBufferRHIRef GetPointCloudVertexFactoryUniformBuffer() const
	{
		return UniformBuffer;
	}

	inline const uint32 GetColorMask() const
	{
		return ColorMask;
	}

	inline const float GetPointSize() const
	{
		return PointSize;
	}

private:
	/** Buffers to read from */
	FUniformBufferRHIRef UniformBuffer;
	/** Mask of zero when using a global color or all bits when using a stream */
	uint32 ColorMask;
	/** The point size to use when rendering separate from the component scale for zooming operations, etc. */
	float PointSize;
};
