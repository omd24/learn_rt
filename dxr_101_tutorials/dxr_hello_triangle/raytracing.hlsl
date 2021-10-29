#ifndef RAYTRACING_HLSL
#define RAYTRACING_HLSL

#include "headers/rt_hlsl_compat.h"

RaytracingAccelerationStructure Scene : register(t0, space0);
RWTexture2D<float4> RenderTarget: register(u0);
ConstantBuffer<RayGenCBuffer> g_raygen_cb : register(b0);

typedef BuiltInTriangleIntersectionAttributes MyAttributes;
struct RayPayload {
    float4 color;
};

bool IsInsideViewport (float2 p, Viewport vp) {
    return (p.x >= vp.left && p.x <= vp.right) &&
        (p.y >= vp.top && p.y <= vp.bottom);
}

[shader("raygeneration")]
void MyRaygenShader () {
    float2 lerp_vals = (float2)DispatchRaysIndex() / (float2)DispatchRaysDimensions();

    // -- orthographic projection since we're raytracing in screen-space:
    float3 ray_dir = float3(0,0,1);
    float3 origin = float3(
        lerp(g_raygen_cb.viewport.left, g_raygen_cb.viewport.right, lerp_vals.x),
        lerp(g_raygen_cb.viewport.top, g_raygen_cb.viewport.bottom, lerp_vals.y),
        0.0f
    );
    if (IsInsideViewport(origin.xy, g_raygen_cb.stencil)) {
        // -- trace the ray
        // -- set ray's extents
        RayDesc ray;
        ray.Origin = origin;
        ray.Direction = ray_dir;
        // -- set TMin to a non-zero small value to avoid aliasing issues due to floating-point errors
        // -- TMin should be kept small to prevent missing geometries at close contact areas
        ray.TMin = 0.001;
        ray.TMax = 10000.0;
        RayPayload payload = {float4(0,0,0,0)};

        TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, ray, payload);

        // -- write raytraced color to the output texture
        RenderTarget[DispatchRaysIndex().xy] = payload.color;
    } else {
        // -- render interpolated DispatchRaysIndex outside the stencil window
        RenderTarget[DispatchRaysIndex().xy] = float4(lerp_vals, 0, 1);
    }
}

[shader("closesthit")]
void MyClosestHitShader (inout RayPayload payload, in MyAttributes attr) {
    float3 barycentrics =
        float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
    payload.color = float4(barycentrics, 1);
}

[shader("miss")]
void MyMissShader (inout RayPayload payload) {
    payload.color = float4(0, 0, 0, 1);
}
#endif  // RAYTRACING_HLSL