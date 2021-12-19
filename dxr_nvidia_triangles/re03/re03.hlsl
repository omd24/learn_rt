RaytracingAccelerationStructure g_scene : register(t0);
RWTexture2D<float4> g_output : register(u0);

float3 linear_to_srgb (float3 c) {
    float3 sq1 = sqrt(c);
    float3 sq2 = sqrt(sq1);
    float3 sq3 = sqrt(sq2);
    return 0.66 * sq1 + 0.68 * sq2 - 0.32 * sq3 - 0.2 * c;
}

[shader("raygeneration")]
void RGS () {
    uint3 launch_index = DispatchRaysIndex();
    float3 color = linear_to_srgb(float3(0.4, 0.6, 0.2));
    g_output[launch_index.xy] = float4(color, 1);
}

struct Payload {
    bool hit;
};

[shader("miss")]
void Miss (inout Payload payload) {
    payload.hit = false;
}

[shader("closesthit")]
void CHS (inout Payload payload, in BuiltInTriangleIntersectionAttributes attribs) {
    payload.hit = true;
}