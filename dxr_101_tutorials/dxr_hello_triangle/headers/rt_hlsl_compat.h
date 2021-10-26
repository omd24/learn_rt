#ifndef RT_HLSL_COMPAT_H
#define RT_HLSL_COMPAT_H

struct Viewport {
    float left;
    float top;
    float right;
    float bottom;
};
struct RayGenCBuffer {
    Viewport viewport;
    Viewport stencil;
};

#endif // !RT_HLSL_COMPAT_H


