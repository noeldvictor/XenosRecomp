#ifndef SHADER_COMMON_H_INCLUDED
#define SHADER_COMMON_H_INCLUDED

#define SPEC_CONSTANT_R11G11B10_NORMAL  (1 << 0)
#define SPEC_CONSTANT_ALPHA_TEST        (1 << 1)

#ifdef UNLEASHED_RECOMP
    #define SPEC_CONSTANT_BICUBIC_GI_FILTER (1 << 2)
    #define SPEC_CONSTANT_ALPHA_TO_COVERAGE (1 << 3)
    #define SPEC_CONSTANT_REVERSE_Z         (1 << 4)
#endif

// SPIR-V vertex input locations, shared with reblue's host input-layout
// builder. A semantic missing here gets no location from the emitter.
#define REBLUE_VERTEX_INPUT_LOCATIONS(X) \
    X(Position,  0,  0) \
    X(Position,  1,  1) \
    X(Position,  2,  2) \
    X(Position,  3,  3) \
    X(Position,  4,  4) \
    X(Normal,    0,  5) \
    X(Tangent,   0,  6) \
    X(TexCoord,  0,  7) \
    X(TexCoord,  1,  8) \
    X(TexCoord,  2,  9) \
    X(Color,     0, 10)

// SPEC_CONSTANTS_ONLY keeps the HLSL below out of host C++ TUs, which IntelliSense would otherwise parse.
#if (!defined(__cplusplus) || defined(__INTELLISENSE__)) && !defined(SHADER_COMMON_SPEC_CONSTANTS_ONLY)

#define FLT_MIN asfloat(0xff7fffff)
#define FLT_MAX asfloat(0x7f7fffff)

#ifdef __spirv__

// Guest constants reach the shader as three uniform buffers, bound once and
// re-based per draw with a dynamic offset.
//
// They used to be vk::RawBufferLoad from a 64-bit device address in a push
// constant. That is a raw global memory load per invocation: on Adreno it
// bypasses the constant path entirely, measured at ~5x under the hardware's
// fill floor on a Quest 2, and it forced OpCapability Int64, which an Adreno
// 740 cannot compile at all. DXIL never did this - the #else branch has always
// emitted a real cbuffer - which is why the cost was invisible on the desktop.
//
// A dynamic uniform buffer is what a D3D12 root descriptor already gives us:
// one large buffer, a per-draw base offset, no descriptor write per draw.
//
// Sizes match the guest register files: 256 vertex float4s, 224 pixel, and the
// 352-byte shared block as 22 uint4s. uint4 rather than float4 for the shared
// block because it carries descriptor indices and swap masks as raw bits, and
// a float register may flush a denormal on load.
[[vk::binding(0, 0)]] cbuffer VertexShaderConstantsBuf { float4 g_VSC[256]; };
[[vk::binding(1, 0)]] cbuffer PixelShaderConstantsBuf  { float4 g_PSC[224]; };
[[vk::binding(2, 0)]] cbuffer SharedConstantsBuf       { uint4  g_SHC[22];  };

// Byte offset into the shared block -> element and component. Both fold at
// compile time wherever OFF is a literal, which is everywhere except
// g_Booleans.
#define BD_SHARED_U(OFF)  (g_SHC[(OFF) >> 4][((OFF) >> 2) & 3])
#define BD_SHARED_F(OFF)  asfloat(BD_SHARED_U(OFF))
#define BD_SHARED_F2(OFF) float2(BD_SHARED_F(OFF), BD_SHARED_F((OFF) + 4))

#ifdef REBLUE_RECOMP
// 256-bit boolean register file (BD bool addresses reach ~158), then per-usage 16-bit-pair swap masks.
#define g_Booleans(i)              BD_SHARED_U(256 + (i)*4)
#define g_SwappedTexcoords         BD_SHARED_U(288)
#define g_HalfPixelOffset          BD_SHARED_F2(292)
#define g_AlphaThreshold           BD_SHARED_F(300)
#define g_SwappedNormals           BD_SHARED_U(304)
#define g_SwappedBinormals         BD_SHARED_U(308)
#define g_SwappedTangents          BD_SHARED_U(312)
#define g_SwappedBlendWeights      BD_SHARED_U(316)
#define g_SwappedPositions         BD_SHARED_U(320)
#define g_SintTexcoords            BD_SHARED_U(324)
#define g_ShadowPcfScale           BD_SHARED_F(328)
// Multiview stereo. Both zero unless bd_stereo is on, so the per-eye skew every
// vertex shader ends with costs two loads and a multiply-add and changes
// nothing.
#define g_StereoSeparation         BD_SHARED_F(332)
#define g_StereoConvergence        BD_SHARED_F(344)
#else
#define g_Booleans                 BD_SHARED_U(256)
#define g_SwappedTexcoords         BD_SHARED_U(260)
#define g_HalfPixelOffset          BD_SHARED_F2(264)
#define g_AlphaThreshold           BD_SHARED_F(272)
#endif

[[vk::constant_id(0)]] const uint g_SpecConstants = 0;

#define g_SpecConstants() g_SpecConstants

#else

#ifdef REBLUE_RECOMP
#define DEFINE_SHARED_CONSTANTS() \
    uint4 g_BooleansArr[2] : packoffset(c16); \
    uint g_SwappedTexcoords : packoffset(c18.x); \
    float2 g_HalfPixelOffset : packoffset(c18.y); \
    float g_AlphaThreshold : packoffset(c18.w); \
    uint g_SwappedNormals : packoffset(c19.x); \
    uint g_SwappedBinormals : packoffset(c19.y); \
    uint g_SwappedTangents : packoffset(c19.z); \
    uint g_SwappedBlendWeights : packoffset(c19.w); \
    uint g_SwappedPositions : packoffset(c20.x); \
    uint g_SintTexcoords : packoffset(c20.y); \
    float g_ShadowPcfScale : packoffset(c20.z); \
    float g_StereoSeparation : packoffset(c20.w); \
    float g_StereoConvergence : packoffset(c21.z);

#define g_Booleans(i) (g_BooleansArr[(i) / 4][(i) % 4])
#else
#define DEFINE_SHARED_CONSTANTS() \
    uint g_Booleans : packoffset(c16.x); \
    uint g_SwappedTexcoords : packoffset(c16.y); \
    float2 g_HalfPixelOffset : packoffset(c16.z); \
    float g_AlphaThreshold : packoffset(c17.x);
#endif

uint g_SpecConstants();

#endif

#ifdef REBLUE_RECOMP
// Test Xenos boolean register N in the unified VS(0..127)/PS(128..255) file.
#define BOOL_BIT(n) ((g_Booleans((n) / 32u) & (1u << ((n) & 31u))) != 0)
#endif

#ifdef __spirv__
// Guest constant chunks, bound ahead of the texture arrays in the same
// descriptor sets.
//
// They cannot have a set of their own: the pipeline layout already binds four
// (spaces 0/1/2 all point at the texture set, 3 at the samplers) and Adreno's
// maxBoundDescriptorSets is exactly 4. They have to come before the texture
// array because that array is the boundless range and the boundless range must
// be last. Hence the explicit vk::binding on the heaps below, which shifts them
// to binding 1 for SPIR-V and leaves the D3D12 register mapping alone -
// [[vk::binding]] is ignored when DXC targets DXIL.
//
// Why this exists at all: reading a guest constant through a 64-bit device
// address makes every shader declare OpCapability Int64, and an Adreno 740
// reports shaderInt64=0 and compiles none of them. See
// research/20260830_0820_arm64-the-thor-renders-nothing.md.

[[vk::binding(3, 0)]] Texture2DArray<float4> g_Texture2DDescriptorHeap[] : register(t0, space0);
[[vk::binding(3, 1)]] Texture3D<float4> g_Texture3DDescriptorHeap[] : register(t0, space1);
[[vk::binding(3, 2)]] TextureCube<float4> g_TextureCubeDescriptorHeap[] : register(t0, space2);
SamplerState g_SamplerDescriptorHeap[] : register(s0, space3);
#else
Texture2D<float4> g_Texture2DDescriptorHeap[] : register(t0, space0);
Texture3D<float4> g_Texture3DDescriptorHeap[] : register(t0, space1);
TextureCube<float4> g_TextureCubeDescriptorHeap[] : register(t0, space2);
SamplerState g_SamplerDescriptorHeap[] : register(s0, space3);
#endif

// The 2D heap is an ARRAY heap under Vulkan, and every 2D read carries an array
// layer of g_ViewIndex.
//
// This is what lets multiview work without a resolve. A two-layer render target
// is sampled directly, each eye reading its own layer, so the five
// full-resolution passes that existed only to flatten arrays into a
// side-by-side companion disappear - and with them the reason the multiview
// frame was black, which was that nothing could sample a two-layer image
// through a Texture2D view at all.
//
// Ordinary textures are unaffected: Vulkan CLAMPS the array layer to
// [0, layers-1] when sampling, so a one-layer texture read at layer 1 returns
// layer 0. No per-texture knowledge is needed in the shader, which is the whole
// reason this is tractable.
//
// DXIL keeps plain Texture2D - D3D12 has no multiview here and the descriptor
// heap is declared 2D on that side.
#ifdef __spirv__
#define BD_TEX2D Texture2DArray<float4>
#define BD_UV(uv) float3((uv), float(g_ViewIndex))
#define BD_LOAD(c, mip) int4((c), int(g_ViewIndex), (mip))
static uint g_ViewIndex = 0;
#else
#define BD_TEX2D Texture2D<float4>
#define BD_UV(uv) (uv)
#define BD_LOAD(c, mip) int3((c), (mip))
#endif

uint2 getTexture2DDimensions(BD_TEX2D texture)
{
    uint2 dimensions;
#ifdef __spirv__
    uint elements;
    texture.GetDimensions(dimensions.x, dimensions.y, elements);
#else
    texture.GetDimensions(dimensions.x, dimensions.y);
#endif
    return dimensions;
}

float4 tfetch2D(uint resourceDescriptorIndex, uint samplerDescriptorIndex, float2 texCoord, float2 offset)
{
    BD_TEX2D texture = g_Texture2DDescriptorHeap[resourceDescriptorIndex];
    return texture.Sample(g_SamplerDescriptorHeap[samplerDescriptorIndex], BD_UV(texCoord + offset / getTexture2DDimensions(texture)));
}

float2 getWeights2D(uint resourceDescriptorIndex, uint samplerDescriptorIndex, float2 texCoord, float2 offset)
{
    BD_TEX2D texture = g_Texture2DDescriptorHeap[resourceDescriptorIndex];
    return select(isnan(texCoord), 0.0, frac(texCoord * getTexture2DDimensions(texture) + offset - 0.5));
}

#ifdef REBLUE_RECOMP
// Bilinear-filtered shadow compare over the four neighboring depth texels.
float shadowCmp2D(uint resourceDescriptorIndex, float2 texCoord, float ref)
{
    BD_TEX2D texture = g_Texture2DDescriptorHeap[resourceDescriptorIndex];
    int2 dimensions = int2(getTexture2DDimensions(texture));
    float2 coord = texCoord * dimensions - 0.5;
    float2 weights = frac(coord);
    int2 base = int2(floor(coord));
    int2 c0 = clamp(base, int2(0, 0), dimensions - 1);
    int2 c1 = clamp(base + 1, int2(0, 0), dimensions - 1);
    float4 taps = float4(
        texture.Load(BD_LOAD(c0, 0)).x,
        texture.Load(BD_LOAD(int2(c1.x, c0.y), 0)).x,
        texture.Load(BD_LOAD(int2(c0.x, c1.y), 0)).x,
        texture.Load(BD_LOAD(c1, 0)).x) > ref;
    return lerp(lerp(taps.x, taps.y, weights.x), lerp(taps.z, taps.w, weights.x), weights.y);
}
#endif

float w0(float a)
{
    return (1.0f / 6.0f) * (a * (a * (-a + 3.0f) - 3.0f) + 1.0f);
}

float w1(float a)
{
    return (1.0f / 6.0f) * (a * a * (3.0f * a - 6.0f) + 4.0f);
}

float w2(float a)
{
    return (1.0f / 6.0f) * (a * (a * (-3.0f * a + 3.0f) + 3.0f) + 1.0f);
}

float w3(float a)
{
    return (1.0f / 6.0f) * (a * a * a);
}

float g0(float a)
{
    return w0(a) + w1(a);
}

float g1(float a)
{
    return w2(a) + w3(a);
}

float h0(float a)
{
    return -1.0f + w1(a) / (w0(a) + w1(a)) + 0.5f;
}

float h1(float a)
{
    return 1.0f + w3(a) / (w2(a) + w3(a)) + 0.5f;
}

float4 tfetch2DBicubic(uint resourceDescriptorIndex, uint samplerDescriptorIndex, float2 texCoord, float2 offset)
{
    BD_TEX2D texture = g_Texture2DDescriptorHeap[resourceDescriptorIndex];
    SamplerState samplerState = g_SamplerDescriptorHeap[samplerDescriptorIndex];
    uint2 dimensions = getTexture2DDimensions(texture);
    
    float x = texCoord.x * dimensions.x + offset.x;
    float y = texCoord.y * dimensions.y + offset.y;

    x -= 0.5f;
    y -= 0.5f;
    float px = floor(x);
    float py = floor(y);
    float fx = x - px;
    float fy = y - py;

    float g0x = g0(fx);
    float g1x = g1(fx);
    float h0x = h0(fx);
    float h1x = h1(fx);
    float h0y = h0(fy);
    float h1y = h1(fy);

    float4 r =
        g0(fy) * (g0x * texture.Sample(samplerState, BD_UV(float2(px + h0x, py + h0y) / float2(dimensions))) +
            g1x * texture.Sample(samplerState, BD_UV(float2(px + h1x, py + h0y) / float2(dimensions)))) +
        g1(fy) * (g0x * texture.Sample(samplerState, BD_UV(float2(px + h0x, py + h1y) / float2(dimensions))) +
            g1x * texture.Sample(samplerState, BD_UV(float2(px + h1x, py + h1y) / float2(dimensions))));

    return r;
}

float4 tfetch3D(uint resourceDescriptorIndex, uint samplerDescriptorIndex, float3 texCoord)
{
    return g_Texture3DDescriptorHeap[resourceDescriptorIndex].Sample(g_SamplerDescriptorHeap[samplerDescriptorIndex], texCoord);
}

struct CubeMapData
{
	#ifdef REBLUE_RECOMP
    // BD's cube-shadow PCF issues up to 9 cube ops per shader (bd_mirror_cs_ps /
    // bd_glass_cs_ps); overflowing this array drops the stored directions and
    // every tfetchCube reads OOB -> point-light shadows vanish.
    float3 cubeMapDirections[9];
    #else
    float3 cubeMapDirections[2];
    #endif
    uint cubeMapIndex;
};

float4 tfetchCube(uint resourceDescriptorIndex, uint samplerDescriptorIndex, float3 texCoord, inout CubeMapData cubeMapData)
{
    return g_TextureCubeDescriptorHeap[resourceDescriptorIndex].Sample(g_SamplerDescriptorHeap[samplerDescriptorIndex], cubeMapData.cubeMapDirections[texCoord.z]);
}

#ifdef REBLUE_RECOMP
// DEC3N normal decode; IA binds as R32_UINT so lane .x carries the raw bits (asuint recovers them).
float4 tfetchR11G11B10(float4 value)
{
    if (g_SpecConstants() & SPEC_CONSTANT_R11G11B10_NORMAL)
    {
        uint v = asuint(value.x);
        return float4(
            (v & 0x00000400 ? -1.0 : 0.0) + ((v & 0x3FF) / 1024.0),
            (v & 0x00200000 ? -1.0 : 0.0) + (((v >> 11) & 0x3FF) / 1024.0),
            (v & 0x80000000 ? -1.0 : 0.0) + (((v >> 22) & 0x1FF) / 512.0),
            0.0);
    }
    return value;
}

// Undo the engine bswap32 16-bit-pair swap (.yxwz) for any 16-bit-packed semantic flagged in the mask.
float4 swapFloats(uint swappedMask, float4 value, uint semanticIndex)
{
    return (swappedMask & (1u << semanticIndex)) != 0 ? value.yxwz : value;
}

// Recover X360 integer-cast-to-float TEXCOORDs from R16G16(B16A16)_UINT bindings (sign-extend low 16 bits).
float4 sintTexcoord(uint mask, float4 value, uint semanticIndex)
{
    if ((mask & (1u << semanticIndex)) != 0)
    {
        // An SNORM binding means the driver has already converted; undo the
        // normalisation rather than sign-extending raw bits out of a float.
        // Binding an integer format against the float4 these shaders declare is
        // a numeric-class mismatch that the Adreno 740 rejects outright
        // (VUID-VkGraphicsPipelineCreateInfo-Input-08733), and SSCALED - the
        // exact replacement - is not exposed as a vertex format there at all
        // (VUID-VkVertexInputAttributeDescription-format-00623). Exact for
        // every deliverable value: 32767 * (v / 32767) round-trips inside a
        // float32 mantissa. Only -32768 is lost, clamping to -32767.
        return value * 32767.0f;
    }
    return value;
}
#else
float4 tfetchR11G11B10(uint4 value)
{
    if (g_SpecConstants() & SPEC_CONSTANT_R11G11B10_NORMAL)
    {
        return float4(
            (value.x & 0x00000400 ? -1.0 : 0.0) + ((value.x & 0x3FF) / 1024.0),
            (value.x & 0x00200000 ? -1.0 : 0.0) + (((value.x >> 11) & 0x3FF) / 1024.0),
            (value.x & 0x80000000 ? -1.0 : 0.0) + (((value.x >> 22) & 0x1FF) / 512.0),
            0.0);
    }
    else
    {
        return asfloat(value);
    }
}

float4 tfetchTexcoord(uint swappedTexcoords, float4 value, uint semanticIndex)
{
    return (swappedTexcoords & (1ull << semanticIndex)) != 0 ? value.yxwz : value;
}
#endif

float4 cube(float4 value, inout CubeMapData cubeMapData)
{
    uint index = cubeMapData.cubeMapIndex;
    cubeMapData.cubeMapDirections[index] = value.xyz;
    ++cubeMapData.cubeMapIndex;
    
    return float4(0.0, 0.0, 0.0, index);
}

float4 dst(float4 src0, float4 src1)
{
    float4 dest;
    dest.x = 1.0;
    dest.y = src0.y * src1.y;
    dest.z = src0.z;
    dest.w = src1.w;
    return dest;
}

float4 max4(float4 src0)
{
    return max(max(src0.x, src0.y), max(src0.z, src0.w));
}

float2 getPixelCoord(uint resourceDescriptorIndex, float2 texCoord)
{
    return getTexture2DDimensions(g_Texture2DDescriptorHeap[resourceDescriptorIndex]) * texCoord;
}

float computeMipLevel(float2 pixelCoord)
{
    float2 dx = ddx(pixelCoord);
    float2 dy = ddy(pixelCoord);
    float deltaMaxSqr = max(dot(dx, dx), dot(dy, dy));
    return max(0.0, 0.5 * log2(deltaMaxSqr));
}

#endif

#endif
