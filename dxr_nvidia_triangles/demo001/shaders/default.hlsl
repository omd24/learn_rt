RaytracingAccelerationStructure g_raytracing_scene : register(t0);
RWTexture2D<float4> g_output : register(u0);

cbuffer PerFrame : register(b0) {
    float3 A;
    float3 B;
    float3 C;
}

float3 linear_to_srgb (float3 c) {
    // -- based on http://chilliant.blogspot.com/2012/08/srgb-approximations-for-hlsl.html
    float3 sq1 = sqrt(c);
    float3 sq2 = sqrt(sq1);
    float3 sq3 = sqrt(sq2);
    float3 srgb = 0.662002687 * sq1 + 0.684122060 * sq2 - 0.323583601 * sq3 + 0.0225411470 * c;
    return srgb;
}

struct RayPayload {
    float3 color;
};

[shader("raygeneration")]
void raygen () {
    uint3 launch_idx = DispatchRaysIndex();
    uint3 launch_dim = DispatchRaysDimensions();

    float2 coords = float2(launch_idx.xy);
    float2 dims = float2(launch_dim.xy);

    // -- normalize and shift/scale (from [0,1] to [-1,1]):
    float2 normalized_coord = ((coords / dims) * 2.0f - 1.0f);
    
    float aspect_ratio = dims.x / dims.y;

    RayDesc ray;
    ray.Origin = float3(0, 0, -2);
    ray.Direction = normalize(float3(
        normalized_coord.x * aspect_ratio,
        -normalized_coord.y, // y increases in the opposite direction of window
        1.0
    ));
    ray.TMin = 0; // TODO(omid): in practice, this should be an epsilon value
    ray.TMax = 100000;

    RayPayload payload;
    TraceRay(
        g_raytracing_scene,
        0 /*ray flags*/, 0xFF /* no object culling */, 0 /*ray index*/,
        1 /* MultiplierForGeometryContributionToShaderIndex */, 0,
        ray, payload
    );

    float3 color = linear_to_srgb(payload.color);
    g_output[launch_idx.xy] = float4(color, 1);
}

[shader("miss")]
void miss (inout RayPayload payload) {
    payload.color = float3(0.4, 0.6, 0.2);
}
[shader("closesthit")]
void chs_triangle (inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs) {
    float3 barycentrics = float3(
        1.0 - attribs.barycentrics.x - attribs.barycentrics.y,
        attribs.barycentrics.x,
        attribs.barycentrics.y
    );

    uint instance_id = InstanceID();
    payload.color = A * barycentrics.x + B * barycentrics.y + C * barycentrics.z;
}
/*
[shader("closesthit")]
void chs_old (inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs) {
    float3 barycentrics = float3(
        1.0 - attribs.barycentrics.x - attribs.barycentrics.y,
        attribs.barycentrics.x,
        attribs.barycentrics.y
    );

    uint instance_id = InstanceID();
    payload.color =
        A[instance_id] * barycentrics.x +
        B[instance_id] * barycentrics.y +
        C[instance_id] * barycentrics.z;
        
}
*/
[shader("closesthit")]
void chs_plane (inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs) {
    payload.color = 0.9f;
}