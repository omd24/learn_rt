RaytracingAccelerationStructure g_scene : register(t0);
RWTexture2D<float4> g_output : register(u0);

float3 linear_to_srgb (float3 c) {
    float3 sq1 = sqrt(c);
    float3 sq2 = sqrt(sq1);
    float3 sq3 = sqrt(sq2);
    return 0.66 * sq1 + 0.68 * sq2 - 0.32 * sq3 - 0.2 * c;
}

struct RayPayload {
    float3 color;
};

[shader("raygeneration")]
void RGS () {
    uint3 launch_index = DispatchRaysIndex();
    uint3 launch_dims = DispatchRaysDimensions();

    float2 crds = float2(launch_index.xy);
    float2 dims = float2(launch_dims.xy);

    float2 d = ((crds / dims) * 2.0f - 1.0f);
    float aspect_ratio = dims.x / dims.y;

    RayDesc ray;
    ray.Origin = float3(0, 0, -2);
    ray.Direction = normalize(float3(d.x * aspect_ratio, -d.y, 1));
    ray.TMax = 100000;
    ray.TMin = 0;

    RayPayload payload;
    TraceRay(
        g_scene, 0 /*flags*/, 0xFF /*masks*/,
        0 /* ray index aka RayContributionToHitGroupIndex */,
        0 /* MultiplierForGeometryContributionToHitGroupIndex */,
        0 /* miss shader index */,
        ray /* RayDesc */,
        payload
    );
    float3 color = linear_to_srgb(payload.color);
    g_output[launch_index.xy] = float4(color, 1);
}

[shader("miss")]
void Miss (inout RayPayload payload) {
    payload.color = float3(0.4, 0.6, 0.2);
}

[shader("closesthit")]
void CHS (inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs) {
    float3 barycentrics = float3(
        1.0 - attribs.barycentrics.x - attribs.barycentrics.y,
        attribs.barycentrics.x,
        attribs.barycentrics.y
    );

    uint instance_id = InstanceID();

    const float3 A = float3(1, 0, 0);
    const float3 B = float3(0, 1, 0);
    const float3 C = float3(0, 0, 1);

    switch (instance_id) {
    case 0:
        payload.color = A * barycentrics.x + B * barycentrics.y + C * barycentrics.z;
        break;
    case 1:
        payload.color = B * barycentrics.x + C * barycentrics.y + A * barycentrics.z;
        break;
    case 2:
        payload.color = C * barycentrics.x + A * barycentrics.y + B * barycentrics.z;
        break;
    }
}