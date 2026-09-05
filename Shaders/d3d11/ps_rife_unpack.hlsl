// Reads the RIFE network output tensor (NCHW float32 [1, 3, Hp, Wp]) and writes
// the visible (cropped) frame into the render target.

cbuffer PARAMS : register(b0)
{
    uint width;
    uint height;
    uint paddedWidth;
    uint paddedHeight;
    float timestep;
    uint mode;
    uint flowWidth;
    uint lumaPitch;
};

ByteAddressBuffer tensor : register(t0);

float4 main(float4 pos : SV_Position) : SV_Target
{
    const uint2 p = uint2(pos.xy);

    const uint plane = paddedWidth * paddedHeight;
    const uint pixel = p.y * paddedWidth + p.x;
    const float3 rgb = float3(
        asfloat(tensor.Load((0 * plane + pixel) * 4)),
        asfloat(tensor.Load((1 * plane + pixel) * 4)),
        asfloat(tensor.Load((2 * plane + pixel) * 4)));

    return float4(saturate(rgb), 1.0);
}
