RaytracingAccelerationStructure g_raytracing_scene : register(t0);
RWTexture2D<float4> g_output : register(u0);

float3 linear_to_srgb (float3 c) {
    // -- based on http://chilliant.blogspot.com/2012/08/srgb-approximations-for-hlsl.html
    float3 sq1 = sqrt(c);
    float3 sq2 = sqrt(sq1);
    float3 sq3 = sqrt(sq2);
    float3 srgb = 0.662002687 * sq1 + 0.684122060 * sq2 - 0.323583601 * sq3 + 0.0225411470 * c;
    return srgb;
}

[shader("raygeneration")]
void raygen () {
    uint3 launch_idx = DispatchRaysIndex();
    float3 color = linear_to_srgb(float3(0.4, 0.6, 0.2));
    g_output[launch_idx.xy] = float4(color, 1);
}
struct Payload {
    bool hit; // sizeof(float)
};
[shader("miss")]
void miss (inout Payload payload) {
    payload.hit = false;
}
[shader("closesthit")]
void chs (inout Payload payload, in BuiltInTriangleIntersectionAttributes attribs) {
    payload.hit = true;
}