#ifndef RT_HLSL_COMPAT_H
#define RT_HLSL_COMPAT_H

struct Viewport {
    float Left;
    float Top;
    float Right;
    float Bottom;
};
struct RayGenCBuffer {
    Viewport viewport;
    Viewport stencil;
};

#endif // !RT_HLSL_COMPAT_H


