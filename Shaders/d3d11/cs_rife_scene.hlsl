// Validates NVOFA forward/backward motion vectors. A vector is an inlier when
// it lands inside the opposite map, its NVOFA cost is low, and the forward +
// backward round trip closes within two pixels. A hard cut has almost no such
// correspondences; ordinary motion and illumination changes retain them.

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

ByteAddressBuffer flowBuf : register(t0); // forward plane, then backward plane
ByteAddressBuffer costBuf : register(t1); // forward plane, then backward plane
RWByteAddressBuffer statsBuf : register(u0);

groupshared uint groupTotal;
groupshared uint groupInBounds;
groupshared uint groupInliers;
groupshared uint groupCost;

int2 LoadFlow(uint byteOffset)
{
    const uint packed = flowBuf.Load(byteOffset);
    return int2(int(packed << 16) >> 16, int(packed) >> 16);
}

uint LoadCost(uint byteOffset)
{
    const uint alignedOffset = byteOffset & ~3u;
    const uint shift = (byteOffset & 3u) * 8;
    return (costBuf.Load(alignedOffset) >> shift) & 255u;
}

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
    if (groupIndex == 0) {
        groupTotal = 0;
        groupInBounds = 0;
        groupInliers = 0;
        groupCost = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    const uint flowHeight = (height + 3) / 4;
    if (id.x < flowWidth && id.y < flowHeight) {
        const uint index = id.y * flowWidth + id.x;
        const uint flowPlaneBytes = flowWidth * flowHeight * 4;
        const uint costPitch = (flowWidth + 3) & ~3u;
        const uint costPlaneBytes = costPitch * flowHeight;
        const int2 forwardRaw = LoadFlow(index * 4);
        const float2 forward = float2(forwardRaw) / 32.0;
        const int2 target = int2(floor(float2(id.xy) + forward / 4.0 + 0.5));

        InterlockedAdd(groupTotal, 1);
        if (all(target >= 0) && target.x < int(flowWidth) && target.y < int(flowHeight)) {
            const uint targetIndex = target.y * flowWidth + target.x;
            const int2 backwardRaw = LoadFlow(flowPlaneBytes + targetIndex * 4);
            const float roundTripError = length(float2(forwardRaw + backwardRaw) / 32.0);
            const uint pairCost = max(LoadCost(id.y * costPitch + id.x),
                LoadCost(costPlaneBytes + target.y * costPitch + target.x));
            InterlockedAdd(groupInBounds, 1);
            InterlockedAdd(groupCost, pairCost);
            if (pairCost <= 16 && roundTripError <= 2.0) {
                InterlockedAdd(groupInliers, 1);
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (groupIndex == 0) {
        statsBuf.InterlockedAdd(0, groupTotal);
        statsBuf.InterlockedAdd(4, groupInBounds);
        statsBuf.InterlockedAdd(8, groupInliers);
        statsBuf.InterlockedAdd(12, groupCost);
    }
}
