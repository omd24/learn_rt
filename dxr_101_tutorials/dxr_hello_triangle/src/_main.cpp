#include "stdafx.h"
#include "dxr_hello_triangle.h"

_Use_decl_annotations_
int WINAPI WinMain (HINSTANCE inst, HINSTANCE, LPSTR, int cmdshow) {
    DXRHelloTriangle sample(1280, 720, L"D3D12 Raytracing - Hello Triangle");
    return Win32App::Run(&sample, inst, cmdshow);
}
