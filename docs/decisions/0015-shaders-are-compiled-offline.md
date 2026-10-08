# 0015: The screen's shaders are GLSL, compiled offline to SPIR-V and MSL

**Status:** accepted

## Context

[0008](0008-the-renderer-is-sdl-gpu.md) left the format of our own shaders to M8. SDL_GPU does
not compile shader source: a shader comes to it as SPIR-V for Vulkan, MSL or metallib for Metal,
or DXIL for Direct3D 12. The owner chose to write the shaders once and commit what they compile
to, so that building the application needs no shader compiler.

SDL_shadercross, which 0008 named, is in no package manager the workspace uses. It needs DXC,
Microsoft's compiler, to read HLSL, and DXC is a long LLVM build. Homebrew has glslang and
SPIRV-Cross, which together take a shader to SPIR-V and to MSL. HLSL through glslang leaves
SDL_GPU's Vulkan backend without the combined image samplers it binds. Vulkan's GLSL has them,
and SPIRV-Cross carries them over to MSL.

## Decision

- The screen's shaders are Vulkan GLSL, in `src/pgm_app/shaders`, bound as SDL_GPU lays out SPIR-V
  resources: a fragment shader's sampled textures in set 2 and its uniforms in set 3.
- `scripts/compile-shaders.sh` compiles each with glslangValidator to SPIR-V, and with SPIRV-Cross
  to MSL, keeping the binding numbers as Metal's indices. Both are committed under
  `src/pgm_app/shaders/compiled` as C array initialisers that the renderer includes.
- The GPU device is created for SPIR-V, MSL and metallib, and no longer for DXIL: Windows runs on
  Vulkan.

## Consequences

- A shader is changed in its GLSL and compiled again with the script (`brew install glslang
  spirv-cross`), and both are committed together; the build does not check that they agree.
- Direct3D 12 needs DXC, and a record of its own, should Windows ever want it.
- The presets are our own shaders, not ports of libretro's, whose licences vary.
