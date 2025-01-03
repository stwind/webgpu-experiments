#include <SDL3/SDL.h>
#include "wgpu.hpp"
#include "imgui.hpp"
#include "math.hpp"

struct CameraUniform {
  std::array<float, 16> view;
  std::array<float, 16> proj;
};

class GnomonData {
public:
  std::vector<float> vertices;

  GnomonData(float s = 1.) {
    vertices = {
      0, 0, 0, 1, 0, 0,
      s, 0, 0, 1, 0, 0, // x
      0, 0, 0, 0, 1, 0,
      0, s, 0, 0, 1, 0, // y
      0, 0, 0, 0, 0, 1,
      0, 0, s, 0, 0, 1, // z
    };
  }
};

class CubeData {
public:
  std::vector<float> vertices;
  std::vector<uint16_t> indices{
    0, 1, 2,
    0, 2, 3,
    4, 5, 6,
    4, 6, 7,
    8, 9, 10,
    8, 10, 11,
    12, 13, 14,
    12, 14, 15,
    16, 17, 18,
    16, 18, 19,
    20, 21, 22,
    20, 22, 23
  };

  CubeData(float s = .5) {
    vertices = {
      s, s, -s, 1, 0, 0,
      s, s, s, 1, 0, 0,
      s, -s, s, 1, 0, 0,
      s, -s, -s, 1, 0, 0,
      -s, s, s, -1, 0, 0,
      -s, s, -s, -1, 0, 0,
      -s, -s, -s, -1, 0, 0,
      -s, -s, s, -1, 0, 0,
      -s, s, s, 0, 1, 0,
      s, s, s, 0, 1, 0,
      s, s, -s, 0, 1, 0,
      -s, s, -s, 0, 1, 0,
      -s, -s, -s, 0, -1, 0,
      s, -s, -s, 0, -1, 0,
      s, -s, s, 0, -1, 0,
      -s, -s, s, 0, -1, 0,
      s, s, s, 0, 0, 1,
      -s, s, s, 0, 0, 1,
      -s, -s, s, 0, 0, 1,
      s, -s, s, 0, 0, 1,
      -s, s, -s, 0, 0, -1,
      s, s, -s, 0, 0, -1,
      s, -s, -s, 0, 0, -1,
      -s, -s, -s, 0, 0, -1,
    };
  }
};

class GnomonGeometry {
private:
  const char* source = R"(
  struct Camera {
    view : mat4x4f,
    proj : mat4x4f,
  }

  struct VSOutput {
      @builtin(position) position: vec4f,
      @location(0) color: vec3f,
  };

  @group(0) @binding(0) var<uniform> camera : Camera;
  @group(0) @binding(1) var<uniform> model : mat4x4f;

  @vertex fn vs(
    @location(0) position: vec3f,
    @location(1) color: vec3f,
    ) -> VSOutput {

    var pos = camera.proj * camera.view * model * vec4f(position, 1);
    return VSOutput(pos, color);
  }

  @fragment fn fs(@location(0) color: vec3f) -> @location(0) vec4f {
    return vec4f(pow(color, vec3f(2.2)), 1.);
  }
  )";

public:
  GnomonData data;
  WGPU::Buffer vertexBuffer;

  WGPU::Geometry geom;
  WGPU::RenderPipeline pipeline;

  GnomonGeometry(WGPU::Context& ctx, const std::vector<WGPU::RenderPipeline::BindGroupEntry>& bindGroups) :
    vertexBuffer(ctx, {
      .label = "vertex",
      .size = data.vertices.size() * sizeof(float),
      .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex,
      .mappedAtCreation = false
      }),
    geom{
      .primitive = {
        .topology = WGPUPrimitiveTopology_LineList,
        .stripIndexFormat = WGPUIndexFormat_Undefined,
        .frontFace = WGPUFrontFace_CCW,
        .cullMode = WGPUCullMode_None,
      },
      .vertexBuffers = {
        {
          .buffer = vertexBuffer,
          .attributes = {
            {.shaderLocation = 0, .format = WGPUVertexFormat_Float32x3, .offset = 0 },
            {.shaderLocation = 1, .format = WGPUVertexFormat_Float32x3, .offset = 3 * sizeof(float) }
          },
          .arrayStride = 6 * sizeof(float),
          .stepMode = WGPUVertexStepMode_Vertex
        }
      },
      .count = 6
      },
    pipeline(ctx, {
      .source = source,
      .bindGroups = bindGroups,
      .vertex = {
        .entryPoint = "vs",
        .buffers = geom.vertexBuffers,
      },
      .primitive = geom.primitive,
      .fragment = {
        .entryPoint = "fs",
        .targets = {
          {
            .format = ctx.surfaceFormat,
            .blend = std::make_unique<WGPUBlendState>(WGPUBlendState{
              .color = {
                .srcFactor = WGPUBlendFactor_SrcAlpha,
                .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha,
                .operation = WGPUBlendOperation_Add
              },
              .alpha = {
                .srcFactor = WGPUBlendFactor_Zero,
                .dstFactor = WGPUBlendFactor_One,
                .operation = WGPUBlendOperation_Add
              }
            }).get(),
            .writeMask = WGPUColorWriteMask_All
          }
        }
    },
    .multisample = {
      .count = 1,
      .mask = ~0u,
      .alphaToCoverageEnabled = false
    }
      })
  {
    geom.vertexBuffers[0].buffer.write(data.vertices.data());
  }

  void draw(WGPU::RenderPass& pass) {
    pass.setPipeline(pipeline);
    pass.draw(geom);
  }
};

class CubeGeometry {
private:
  const char* shaderSource = R"(
  struct Camera {
    view : mat4x4f,
    proj : mat4x4f,
  }

  struct VSOutput {
      @builtin(position) position: vec4f,
      @location(0) normal: vec3f,
  };

  @group(0) @binding(0) var<uniform> camera : Camera;
  @group(0) @binding(1) var<uniform> model : mat4x4f;

  @vertex fn vs(
    @location(0) position: vec3f,
    @location(1) normal: vec3f) -> VSOutput {

    let pos = camera.proj * camera.view * model * vec4f(position, 1);
    return VSOutput(pos, normal);
  }

  @fragment fn fs(@location(0) normal: vec3f) -> @location(0) vec4f {
    return vec4f(pow(normalize(normal) * .5 + .5, vec3f(2.2)), 1.);
  }
  )";
public:
  CubeData data;

  WGPU::Buffer vertexBuffer;
  WGPU::Buffer indexBuffer;
  WGPU::IndexedGeometry geom;

  WGPU::RenderPipeline pipeline;

  CubeGeometry(WGPU::Context& ctx, const std::vector<WGPU::RenderPipeline::BindGroupEntry>& bindGroups)
    : vertexBuffer(ctx, {
      .label = "vertex",
      .size = data.vertices.size() * sizeof(float),
      .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex,
      .mappedAtCreation = false
      }),
    indexBuffer(ctx, {
      .label = "index",
      .size = (data.indices.size() * sizeof(uint16_t) + 3) & ~3, // round up to the next multiple of 4
      .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Index,
      .mappedAtCreation = false
      }),
    geom{
      .primitive = {
        .topology = WGPUPrimitiveTopology_TriangleList,
        .stripIndexFormat = WGPUIndexFormat_Undefined,
        .frontFace = WGPUFrontFace_CCW,
        .cullMode = WGPUCullMode_Back,
      },
      .vertexBuffers = {
        {
          .buffer = vertexBuffer,
          .attributes = {
            {.shaderLocation = 0, .format = WGPUVertexFormat_Float32x3, .offset = 0 },
            {.shaderLocation = 1, .format = WGPUVertexFormat_Float32x3, .offset = 3 * sizeof(float) }
          },
          .arrayStride = 6 * sizeof(float),
          .stepMode = WGPUVertexStepMode_Vertex
        }
      },
      .indexBuffer = indexBuffer,
      .count = static_cast<uint32_t>(data.indices.size()),
      },
    pipeline(ctx, {
      .source = shaderSource,
      .bindGroups = bindGroups,
      .vertex = {
        .entryPoint = "vs",
        .buffers = geom.vertexBuffers,
      },
      .primitive = geom.primitive,
      .fragment = {
        .entryPoint = "fs",
        .targets = {
          {
            .format = ctx.surfaceFormat,
            .blend = std::make_unique<WGPUBlendState>(WGPUBlendState{
              .color = {
                .srcFactor = WGPUBlendFactor_SrcAlpha,
                .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha,
                .operation = WGPUBlendOperation_Add
              },
              .alpha = {
                .srcFactor = WGPUBlendFactor_Zero,
                .dstFactor = WGPUBlendFactor_One,
                .operation = WGPUBlendOperation_Add
              }
            }).get(),
            .writeMask = WGPUColorWriteMask_All
          }
        }
      },
      .multisample = {
        .count = 1,
        .mask = ~0u,
        .alphaToCoverageEnabled = false
      }
      }
    )
  {
    geom.vertexBuffers[0].buffer.write(data.vertices.data());
    geom.indexBuffer.write(data.indices.data());
  }

  void draw(WGPU::RenderPass& pass) {
    pass.setPipeline(pipeline);
    pass.draw(geom);
  }
};

class Application {
public:
  WGPU::Context ctx = WGPU::Context(1280, 720);

  WGPU::Buffer camera;
  WGPU::Buffer model;

  GnomonGeometry gnomon;
  CubeGeometry cube;

  WGPUTexture depthTexture;

  struct {
    bool isDown = false;
    ImVec2 downPos = { -1,-1 };
    ImVec2 delta = { 0,0 };

    float phi = 0;
    float theta = M_PI_2;
  } state;

  Application() : camera(ctx, {
    .label = "camera",
      .size = sizeof(CameraUniform),
      .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform,
      .mappedAtCreation = false,
    }),
    model(ctx, {
      .label = "model",
      .size = 16 * sizeof(float),
      .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform,
      .mappedAtCreation = false,
      }),

      gnomon(ctx, {
        {
          .label = "camera",
          .entries = {
            {
              .binding = 0,
              .buffer = &camera,
              .offset = 0,
              .visibility = WGPUShaderStage_Vertex,
              .layout = {
                .type = WGPUBufferBindingType_Uniform,
                .hasDynamicOffset = false,
                .minBindingSize = camera.size,
                }
            },
            {
              .binding = 1,
              .buffer = &model,
              .offset = 0,
              .visibility = WGPUShaderStage_Vertex,
              .layout = {
                .type = WGPUBufferBindingType_Uniform,
                .hasDynamicOffset = false,
                .minBindingSize = model.size,
              }
            }
          }
        }
        }),
    cube(ctx, {
        {
          .label = "camera",
          .entries = {
            {
              .binding = 0,
              .buffer = &camera,
              .offset = 0,
              .visibility = WGPUShaderStage_Vertex,
              .layout = {
                .type = WGPUBufferBindingType_Uniform,
                .hasDynamicOffset = false,
                .minBindingSize = camera.size,
                }
            },
            {
              .binding = 1,
              .buffer = &model,
              .offset = 0,
              .visibility = WGPUShaderStage_Vertex,
              .layout = {
                .type = WGPUBufferBindingType_Uniform,
                .hasDynamicOffset = false,
                .minBindingSize = model.size,
              }
            }
          }
        }
      })
  {
    if (!ImGui_init(&ctx)) throw std::runtime_error("ImGui_init failed");

    WGPUTextureFormat depthTextureFormat = WGPUTextureFormat_Depth24Plus;
    WGPUTextureDescriptor depthTextureDesc{
      .dimension = WGPUTextureDimension_2D,
      .format = depthTextureFormat,
      .mipLevelCount = 1,
      .sampleCount = 1,
      .size{ std::get<0>(ctx.size), std::get<1>(ctx.size), 1 },
      .usage = WGPUTextureUsage_RenderAttachment,
      .viewFormatCount = 1,
      .viewFormats = &depthTextureFormat,
    };
    depthTexture = wgpuDeviceCreateTexture(ctx.device, &depthTextureDesc);

    CameraUniform uniformData{};
    perspective(uniformData.proj, radians(45), ctx.aspect, .1, 100);
    lookAt(uniformData.view, { 0, 0, 5 }, { 0, 0, -1 }, { 0,1,0 });

    camera.write(&uniformData);
  }

  ~Application() {
    wgpuTextureDestroy(depthTexture);
    wgpuTextureRelease(depthTexture);

    ImGui_ImplWGPU_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
  }

  void processEvent(const SDL_Event* event) {
    ImGui_ImplSDL3_ProcessEvent(event);
  }

  void render() {
    Vec3 vec;
    Quaternion rot;
    betweenZ(rot, sph2cart(vec, { state.phi, state.theta,1. }));

    Mat44 m;
    rotation(m, rot);
    model.write(&m);

    WGPUTextureView view = ctx.surfaceTextureCreateView();
    std::vector<WGPUCommandBuffer> commands;

    {
      WGPUCommandEncoderDescriptor encoderDescriptor{};
      WGPU::CommandEncoder encoder(ctx, &encoderDescriptor);

      WGPURenderPassColorAttachment colorAttachment{
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .view = view,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = WGPUColor{ 0., 0., 0., 1. }
      };

      WGPUTextureViewDescriptor depthTextureViewDesc{
        .aspect = WGPUTextureAspect_DepthOnly,
        .baseArrayLayer = 0,
        .arrayLayerCount = 1,
        .baseMipLevel = 0,
        .mipLevelCount = 1,
        .dimension = WGPUTextureViewDimension_2D,
        .format = wgpuTextureGetFormat(depthTexture),
      };
      WGPUTextureView depthTextureView = wgpuTextureCreateView(depthTexture, &depthTextureViewDesc);
      WGPURenderPassDepthStencilAttachment depthStencilAttachment{
        .view = depthTextureView,
        .depthClearValue = 1.0f,
        .depthLoadOp = WGPULoadOp_Clear,
        .depthStoreOp = WGPUStoreOp_Store,
        .depthReadOnly = false,
        .stencilClearValue = 0,
        .stencilLoadOp = WGPULoadOp_Clear,
        .stencilStoreOp = WGPUStoreOp_Store,
        .stencilReadOnly = true,
      };

      WGPURenderPassDescriptor passDescriptor{
        .colorAttachmentCount = 1,
        .colorAttachments = &colorAttachment,
        .depthStencilAttachment = &depthStencilAttachment,
      };
      WGPU::RenderPass pass = encoder.renderPass(&passDescriptor);
      gnomon.draw(pass);
      cube.draw(pass);
      pass.end();

      WGPUCommandBufferDescriptor commandDescriptor{};
      commands.push_back(encoder.finish(&commandDescriptor));

      wgpuTextureViewRelease(depthTextureView);
    }

    ImGui_ImplWGPU_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    ImGuiIO& io = ImGui::GetIO();

    if (state.isDown != ImGui::IsMouseDown(0)) {
      if (!state.isDown) {
        state.downPos.x = io.MousePos.x;
        state.downPos.y = io.MousePos.y;
      }
      else {
        state.downPos.x = -1.;
        state.downPos.y = -1.;
      }
    }
    state.isDown = ImGui::IsMouseDown(0);
    if (state.isDown) {
      state.delta.x = io.MousePos.x - state.downPos.x;
      state.delta.y = io.MousePos.y - state.downPos.y;
    }
    else {
      state.delta.x = 0.;
      state.delta.y = 0.;
    }

    {
      ImGui::SetNextWindowPos(ImVec2(10, 120), ImGuiCond_Once);
      ImGui::SetNextWindowSize(ImVec2(200, 0), ImGuiCond_Once);
      ImGui::Begin("Controls");
      ImGui::SliderFloat("phi", &state.phi, 0.0f, M_PI * 2.);
      ImGui::SliderFloat("theta", &state.theta, -M_PI_2, M_PI_2);

      ImGui::End();
    }

    {
      ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Once);
      ImGui::SetNextWindowSize(ImVec2(200, 70), ImGuiCond_Once);
      ImGui::Begin("Info", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

      if (ImGui::IsMousePosValid())
        ImGui::Text("Mouse pos: (%g, %g)", io.MousePos.x, io.MousePos.y);

      ImGui::Text("down pos: (%g, %g)", state.downPos.x, state.downPos.y);
      ImGui::Text("delta: (%g, %g)", state.delta.x, state.delta.y);

      ImGui::End();
    }
    ImGui::Render();
    commands.push_back(ImGui_command(ctx, view));
    wgpuTextureViewRelease(view);

    ctx.submitCommands(commands);
    ctx.releaseCommands(commands);

    ctx.present();
  }
};

int main(int argc, char** argv) try {
  Application app;

  SDL_Event event;
  for (bool running = true; running;) {
    while (SDL_PollEvent(&event)) {
      app.processEvent(&event);
      if (event.type == SDL_EVENT_QUIT) running = false;
    }

    app.render();
  }

  SDL_Log("Quit");
}
catch (std::exception const& e) {
  std::cerr << e.what() << std::endl;
  return EXIT_FAILURE;
}
