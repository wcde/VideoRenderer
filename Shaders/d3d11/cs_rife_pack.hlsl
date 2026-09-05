// Packs two RGB frames into the RIFE network input tensor (vs-mlrt "rife v1" layout):
// NCHW float32 [1, 11, Hp, Wp], channels:
//   0-2 img0 RGB, 3-5 img1 RGB, 6 timestep plane,
//   7 horizontal grid 2*x/(Wp-1)-1, 8 vertical grid 2*y/(Hp-1)-1,
//   9 constant 2/(Wp-1), 10 constant 2/(Hp-1).
// The frame is padded to (Wp, Hp) by edge replication. The grid uses the padded
// size, like Practical-RIFE's warplayer.py does on padded tensors.

cbuffer PARAMS : register(b0)
{
    uint width;         // visible frame size
    uint height;
    uint paddedWidth;   // tensor size
    uint paddedHeight;
    float timestep;
    uint mode;          // 1 = write image channels, 2 = write timestep plane, 4 = write grid channels
    uint flowWidth;     // NVOFA output width (unused here)
    uint lumaPitch;     // byte-aligned pitch of one NVOFA input plane
};

Texture2D<float4> img0 : register(t0);
Texture2D<float4> img1 : register(t1);
RWByteAddressBuffer tensor : register(u0);
RWByteAddressBuffer nvofLuma : register(u1);

uint Luma8(Texture2D<float4> image, uint2 p)
{
    const float3 weights = float3(0.299, 0.587, 0.114);
    return (uint)(dot(saturate(image.Load(int3(p, 0)).rgb), weights) * 255.0 + 0.5);
}

uint PackLuma4(Texture2D<float4> image, uint x, uint y)
{
    uint packed = 0;
    [unroll]
    for (uint i = 0; i < 4; i++) {
        packed |= Luma8(image, uint2(min(x + i, width - 1), y)) << (i * 8);
    }
    return packed;
}

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= paddedWidth || id.y >= paddedHeight) {
        return;
    }

    const uint plane = paddedWidth * paddedHeight;
    const uint pixel = id.y * paddedWidth + id.x;

    if (mode & 1) {
        const int3 src = int3(min(id.x, width - 1), min(id.y, height - 1), 0);
        const float3 a = saturate(img0.Load(src).rgb);
        const float3 b = saturate(img1.Load(src).rgb);
        tensor.Store((0 * plane + pixel) * 4, asuint(a.r));
        tensor.Store((1 * plane + pixel) * 4, asuint(a.g));
        tensor.Store((2 * plane + pixel) * 4, asuint(a.b));
        tensor.Store((3 * plane + pixel) * 4, asuint(b.r));
        tensor.Store((4 * plane + pixel) * 4, asuint(b.g));
        tensor.Store((5 * plane + pixel) * 4, asuint(b.b));

        if (id.x < width && id.y < height && (id.x & 3) == 0) {
            const uint byteOffset = id.y * lumaPitch + id.x;
            const uint lumaPlaneBytes = lumaPitch * height;
            nvofLuma.Store(byteOffset, PackLuma4(img0, id.x, id.y));
            nvofLuma.Store(lumaPlaneBytes + byteOffset, PackLuma4(img1, id.x, id.y));
        }
    }

    if (mode & 2) {
        tensor.Store((6 * plane + pixel) * 4, asuint(timestep));
    }

    if (mode & 4) {
        const float mulH = 2.0 / (paddedWidth - 1);
        const float mulV = 2.0 / (paddedHeight - 1);
        tensor.Store((7 * plane + pixel) * 4, asuint(id.x * mulH - 1.0));
        tensor.Store((8 * plane + pixel) * 4, asuint(id.y * mulV - 1.0));
        tensor.Store((9 * plane + pixel) * 4, asuint(mulH));
        tensor.Store((10 * plane + pixel) * 4, asuint(mulV));
    }
}
