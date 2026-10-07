# 0008: The renderer is SDL_GPU

**Status:** accepted

## Context

The desktop frontend draws two things: the emulated screen, magnified, and Dear ImGui over it.
Later it will also run post-processing shaders, such as CRT and sharp-bilinear scaling. SDL3 offers
three ways to draw:

- **OpenGL**, which Gearlynx uses. It has a mature GLSL shader ecosystem, but it is deprecated on
  macOS.
- **SDL_Renderer**, which the RTL simulator uses. It is the simplest, but it offers no shaders of
  our own.
- **SDL_GPU**, SDL3's modern API over Metal, Vulkan and Direct3D 12.

The owner chose SDL_GPU.

## Decision

- The frontend renders through SDL_GPU, with ImGui's `imgui_impl_sdl3` and `imgui_impl_sdlgpu3`
  backends.
- The device is created with every shader format the ImGui backend ships bytecode for (SPIR-V,
  DXIL, MSL and metallib), so that SDL picks the platform's native API. On macOS that is Metal.
- The emulated screen is an `R8G8B8A8_UNORM` texture. Each frame is staged through one transfer
  buffer, created once and cycled, and is shown with `ImGui::Image`.
- The screen is magnified with nearest sampling, selected through ImGui's standard
  `DrawCallback_SetSamplerNearest` callback.
- ImGui is fetched at a pinned tag, and its core and the two backends are compiled from that tree
  by a target in `cmake/Dependencies.cmake`. This settles the question
  [0006](0006-dependencies.md) left open for M0.

## Consequences

- macOS runs on Metal and is not exposed to the deprecation of OpenGL.
- Shaders of our own must be supplied in more than one format, because SDL_GPU does not translate
  shader source. When post-processing is built (M8), it needs either SDL_shadercross or bytecode
  compiled offline for each format. That cost was accepted with the choice.
- Gearlynx's GLSL shader chain is no model for ours, beyond its preset idea.
