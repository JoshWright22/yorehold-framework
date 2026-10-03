#include "yorehold/framework/graphics/Renderer.h"

#include "yorehold/framework/graphics/Image.h"

#include "DebugFont.h"

#include <SDL3/SDL.h>
#include <webgpu/webgpu_cpp.h>

#include <cmath>
#include <cstring>
#include <map>
#include <vector>

namespace yh
{

namespace
{

constexpr const char* spriteShader = R"(
struct Uniforms { size: vec2f };
@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(1) @binding(0) var image: texture_2d<f32>;
@group(1) @binding(1) var imageSampler: sampler;

struct VertexOut
{
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) color: vec4f,
};

@vertex fn vertexMain(@location(0) position: vec2f, @location(1) uv: vec2f, @location(2) color: vec4f) -> VertexOut
{
    var out: VertexOut;
    out.position = vec4f(position.x / uniforms.size.x * 2.0 - 1.0, 1.0 - position.y / uniforms.size.y * 2.0, 0.0, 1.0);
    out.uv = uv;
    out.color = color;
    return out;
}

@fragment fn fragmentMain(in: VertexOut) -> @location(0) vec4f
{
    return textureSample(image, imageSampler, in.uv) * in.color;
}
)";

constexpr TextureId windowTarget = UINT32_MAX;
constexpr uint64_t uniformSlot = 256; // dynamic uniform offsets must be 256-byte aligned
constexpr wgpu::TextureFormat targetFormat = wgpu::TextureFormat::RGBA8Unorm;

struct Vertex
{
    float x, y;
    float u, v;
    uint32_t color;
};

// A run of triangles sharing texture, clip, blend mode and target: one draw call.
struct DrawCommand
{
    TextureId texture;
    TextureId target;
    BlendMode blend;
    Rect clip;
    uint32_t firstIndex;
    uint32_t indexCount;
};

struct TransformState
{
    Vec2 offset;
    float scale = 1.0f;
    Rect clip;
    TextureId target = windowTarget;
};

struct GpuTexture
{
    wgpu::Texture texture;
    wgpu::TextureView view;
    wgpu::BindGroup bindGroup;
    float width = 0;
    float height = 0;
    bool isTarget = false;
    std::optional<Color> pendingClear;
};

bool sameRect(const Rect& a, const Rect& b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

wgpu::Color toGpu(Color c)
{
    return {c.r / 255.0, c.g / 255.0, c.b / 255.0, c.a / 255.0};
}

}

struct Renderer::Impl
{
#if defined(SDL_PLATFORM_MACOS) || defined(SDL_PLATFORM_IOS)
    SDL_MetalView metalView = nullptr;
    ~Impl()
    {
        surface = nullptr;
        if (metalView) SDL_Metal_DestroyView(metalView);
    }
#endif
    wgpu::Instance instance;
    wgpu::Adapter adapter;
    wgpu::Device device;
    wgpu::Queue queue;
    wgpu::Surface surface;
    wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
    bool canReadBack = false;
    bool vsync = true;
    int width = 0;
    int height = 0;

    wgpu::ShaderModule shader;
    wgpu::BindGroupLayout uniformLayout;
    wgpu::BindGroupLayout textureLayout;
    wgpu::PipelineLayout pipelineLayout;
    std::map<std::pair<int, int>, wgpu::RenderPipeline> pipelines; // (format, blend)
    wgpu::Buffer uniformBuffer;
    size_t uniformSlots = 0;
    wgpu::BindGroup uniformBindGroup;
    wgpu::Sampler pixelSampler;
    wgpu::Sampler smoothSampler;
    wgpu::Buffer vertexBuffer;
    size_t vertexCapacity = 0;
    wgpu::Buffer indexBuffer;
    size_t indexCapacity = 0;

    std::vector<GpuTexture> textures;
    std::vector<TextureId> releasedTextures;
    RenderStats frameStats;

    // Per frame.
    wgpu::Texture frameTexture;
    wgpu::TextureView frameView;
    bool frameValid = false;
    bool firstPass = true;
    Color clearColor;
    BlendMode blend = BlendMode::Alpha;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<DrawCommand> commands;
    std::vector<TransformState> states;
    // A frame can flush several times (lighting, target clears, overlays). Keep
    // earlier batches' geometry and target sizes intact while their draws run.
    size_t uploadedVertices = 0;
    size_t uploadedIndices = 0;
    size_t uploadedPasses = 0;

    bool createDevice(SDL_Window* window);
    void createLayouts();
    const wgpu::RenderPipeline& pipelineFor(wgpu::TextureFormat target, BlendMode mode);
    void configureSurface();
    TextureId addTexture(int w, int h, const void* rgba, bool smooth, bool renderTarget);
    void ensureBuffers(size_t passes);
    // Starts (or continues) a draw command and returns the index of the first new vertex.
    uint32_t beginShape(TextureId texture);
    void addQuad(TextureId texture, const Vec2 (&corners)[4], const Rect& uv, Color color);
    Vec2 toScreen(Vec2 p) const { return states.back().offset + p * states.back().scale; }
};

Renderer::Renderer()
    : impl_(std::make_shared<Impl>())
{
}

Renderer::~Renderer() = default;

bool Renderer::visible(const Rect& rect) const
{
    const Rect overlap = bounds().intersect(rect);
    return overlap.w > 0 && overlap.h > 0;
}

bool Renderer::Impl::createDevice(SDL_Window* window)
{
    static constexpr wgpu::InstanceFeatureName timedWait = wgpu::InstanceFeatureName::TimedWaitAny;
    wgpu::InstanceDescriptor instanceDesc;
    instanceDesc.requiredFeatureCount = 1;
    instanceDesc.requiredFeatures = &timedWait;
    instance = wgpu::CreateInstance(&instanceDesc);
    if (!instance)
    {
        SDL_Log("WebGPU: no instance");
        return false;
    }

#if defined(SDL_PLATFORM_WINDOWS)
    const SDL_PropertiesID props = SDL_GetWindowProperties(window);
    wgpu::SurfaceSourceWindowsHWND source;
    source.hwnd = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    source.hinstance = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr);
#elif defined(SDL_PLATFORM_EMSCRIPTEN)
    (void)window;
    wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector source;
    source.selector = "#canvas";
#elif defined(SDL_PLATFORM_ANDROID)
    wgpu::SurfaceSourceAndroidNativeWindow source;
    source.window = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr);
#elif defined(SDL_PLATFORM_MACOS) || defined(SDL_PLATFORM_IOS)
    metalView = SDL_Metal_CreateView(window);
    if (!metalView) return false;
    wgpu::SurfaceSourceMetalLayer source;
    source.layer = SDL_Metal_GetLayer(metalView);
#elif defined(SDL_PLATFORM_LINUX)
    const SDL_PropertiesID props = SDL_GetWindowProperties(window);
    wgpu::SurfaceSourceWaylandSurface wayland;
    wayland.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
    wayland.surface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
    wgpu::SurfaceSourceXlibWindow xlib;
    xlib.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
    xlib.window = static_cast<uint64_t>(SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
#else
#error "Unsupported SDL window platform"
#endif
    wgpu::SurfaceDescriptor surfaceDesc;
#if defined(SDL_PLATFORM_LINUX)
    if (wayland.surface) surfaceDesc.nextInChain = &wayland;
    else if (xlib.window) surfaceDesc.nextInChain = &xlib;
    else { SDL_Log("WebGPU requires a Wayland or X11 window"); return false; }
#else
    surfaceDesc.nextInChain = &source;
#endif
    surface = instance.CreateSurface(&surfaceDesc);
    if (!surface) return false;

    wgpu::RequestAdapterOptions adapterOptions;
    adapterOptions.compatibleSurface = surface;
    adapterOptions.powerPreference = wgpu::PowerPreference::HighPerformance;
#if defined(SDL_PLATFORM_WINDOWS)
    adapterOptions.backendType = wgpu::BackendType::D3D12; // skip probing Vulkan, which most Windows PCs don't need
#endif
    instance.WaitAny(instance.RequestAdapter(&adapterOptions, wgpu::CallbackMode::WaitAnyOnly,
        [this](wgpu::RequestAdapterStatus status, wgpu::Adapter result, wgpu::StringView message) {
            if (status == wgpu::RequestAdapterStatus::Success)
                adapter = std::move(result);
            else
                SDL_Log("WebGPU adapter failed: %.*s", static_cast<int>(message.length), message.data);
        }), UINT64_MAX);
    if (!adapter)
        return false;

    wgpu::DeviceDescriptor deviceDesc;
    deviceDesc.SetUncapturedErrorCallback([](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView message) {
        SDL_Log("WebGPU error: %.*s", static_cast<int>(message.length), message.data);
    });
    deviceDesc.SetDeviceLostCallback(wgpu::CallbackMode::AllowSpontaneous,
        [](const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView message) {
            if (reason != wgpu::DeviceLostReason::Destroyed)
                SDL_Log("WebGPU device lost: %.*s", static_cast<int>(message.length), message.data);
        });
    instance.WaitAny(adapter.RequestDevice(&deviceDesc, wgpu::CallbackMode::WaitAnyOnly,
        [this](wgpu::RequestDeviceStatus status, wgpu::Device result, wgpu::StringView message) {
            if (status == wgpu::RequestDeviceStatus::Success)
                device = std::move(result);
            else
                SDL_Log("WebGPU device failed: %.*s", static_cast<int>(message.length), message.data);
        }), UINT64_MAX);
    if (!device)
        return false;

    queue = device.GetQueue();

    wgpu::SurfaceCapabilities capabilities;
    surface.GetCapabilities(adapter, &capabilities);
    if (capabilities.formatCount == 0) return false;
    format = capabilities.formats[0];
    canReadBack = (capabilities.usages & wgpu::TextureUsage::CopySrc) != wgpu::TextureUsage::None;

    wgpu::AdapterInfo info;
    adapter.GetInfo(&info);
    SDL_Log("WebGPU: %.*s (%.*s)", static_cast<int>(info.device.length), info.device.data,
        static_cast<int>(info.description.length), info.description.data);
    return true;
}

void Renderer::Impl::configureSurface()
{
    wgpu::SurfaceConfiguration config;
    config.device = device;
    config.format = format;
    config.usage = wgpu::TextureUsage::RenderAttachment;
    if (canReadBack)
        config.usage |= wgpu::TextureUsage::CopySrc;
    config.width = static_cast<uint32_t>(std::max(width, 1));
    config.height = static_cast<uint32_t>(std::max(height, 1));
    wgpu::SurfaceCapabilities capabilities;
    surface.GetCapabilities(adapter, &capabilities);
    config.presentMode = wgpu::PresentMode::Fifo;
    if (!vsync)
        for (size_t i = 0; i < capabilities.presentModeCount; ++i)
            if (capabilities.presentModes[i] == wgpu::PresentMode::Immediate) config.presentMode = wgpu::PresentMode::Immediate;
    surface.Configure(&config);
}

void Renderer::Impl::createLayouts()
{
    wgpu::ShaderSourceWGSL wgsl;
    wgsl.code = spriteShader;
    wgpu::ShaderModuleDescriptor shaderDesc;
    shaderDesc.nextInChain = &wgsl;
    shader = device.CreateShaderModule(&shaderDesc);

    // Explicit layouts, so texture bind groups work with every pipeline (one per blend mode/target format).
    wgpu::BindGroupLayoutEntry uniformEntry;
    uniformEntry.binding = 0;
    uniformEntry.visibility = wgpu::ShaderStage::Vertex;
    uniformEntry.buffer.type = wgpu::BufferBindingType::Uniform;
    uniformEntry.buffer.hasDynamicOffset = true;
    uniformEntry.buffer.minBindingSize = 16;
    wgpu::BindGroupLayoutDescriptor uniformDesc;
    uniformDesc.entryCount = 1;
    uniformDesc.entries = &uniformEntry;
    uniformLayout = device.CreateBindGroupLayout(&uniformDesc);

    wgpu::BindGroupLayoutEntry textureEntries[2];
    textureEntries[0].binding = 0;
    textureEntries[0].visibility = wgpu::ShaderStage::Fragment;
    textureEntries[0].texture.sampleType = wgpu::TextureSampleType::Float;
    textureEntries[0].texture.viewDimension = wgpu::TextureViewDimension::e2D;
    textureEntries[1].binding = 1;
    textureEntries[1].visibility = wgpu::ShaderStage::Fragment;
    textureEntries[1].sampler.type = wgpu::SamplerBindingType::Filtering;
    wgpu::BindGroupLayoutDescriptor textureDesc;
    textureDesc.entryCount = 2;
    textureDesc.entries = textureEntries;
    textureLayout = device.CreateBindGroupLayout(&textureDesc);

    const wgpu::BindGroupLayout groups[2] = {uniformLayout, textureLayout};
    wgpu::PipelineLayoutDescriptor layoutDesc;
    layoutDesc.bindGroupLayoutCount = 2;
    layoutDesc.bindGroupLayouts = groups;
    pipelineLayout = device.CreatePipelineLayout(&layoutDesc);

    wgpu::SamplerDescriptor samplerDesc;
    samplerDesc.addressModeU = wgpu::AddressMode::ClampToEdge;
    samplerDesc.addressModeV = wgpu::AddressMode::ClampToEdge;
    samplerDesc.magFilter = wgpu::FilterMode::Nearest;
    samplerDesc.minFilter = wgpu::FilterMode::Linear;
    pixelSampler = device.CreateSampler(&samplerDesc);
    samplerDesc.magFilter = wgpu::FilterMode::Linear;
    smoothSampler = device.CreateSampler(&samplerDesc);
}

const wgpu::RenderPipeline& Renderer::Impl::pipelineFor(wgpu::TextureFormat target, BlendMode mode)
{
    const std::pair<int, int> key{static_cast<int>(target), static_cast<int>(mode)};
    if (auto it = pipelines.find(key); it != pipelines.end())
        return it->second;

    const wgpu::VertexAttribute attributes[] = {
        {.format = wgpu::VertexFormat::Float32x2, .offset = offsetof(Vertex, x), .shaderLocation = 0},
        {.format = wgpu::VertexFormat::Float32x2, .offset = offsetof(Vertex, u), .shaderLocation = 1},
        {.format = wgpu::VertexFormat::Unorm8x4, .offset = offsetof(Vertex, color), .shaderLocation = 2},
    };
    wgpu::VertexBufferLayout vertexLayout;
    vertexLayout.stepMode = wgpu::VertexStepMode::Vertex;
    vertexLayout.arrayStride = sizeof(Vertex);
    vertexLayout.attributeCount = std::size(attributes);
    vertexLayout.attributes = attributes;

    using F = wgpu::BlendFactor;
    constexpr auto add = wgpu::BlendOperation::Add;
    wgpu::BlendState blendState;
    switch (mode)
    {
    case BlendMode::Additive:
        blendState.color = {add, F::SrcAlpha, F::One};
        blendState.alpha = {add, F::One, F::One};
        break;
    case BlendMode::Multiply:
        blendState.color = {add, F::Dst, F::Zero};
        blendState.alpha = {add, F::Zero, F::One};
        break;
    case BlendMode::Alpha:
    case BlendMode::Replace:
        blendState.color = {add, F::SrcAlpha, F::OneMinusSrcAlpha};
        blendState.alpha = {add, F::One, F::OneMinusSrcAlpha};
        break;
    }
    wgpu::ColorTargetState colorTarget;
    colorTarget.format = target;
    colorTarget.blend = mode == BlendMode::Replace ? nullptr : &blendState;

    wgpu::FragmentState fragment;
    fragment.module = shader;
    fragment.entryPoint = "fragmentMain";
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    wgpu::RenderPipelineDescriptor pipelineDesc;
    pipelineDesc.label = "sprites";
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shader;
    pipelineDesc.vertex.entryPoint = "vertexMain";
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexLayout;
    pipelineDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    pipelineDesc.fragment = &fragment;
    return pipelines[key] = device.CreateRenderPipeline(&pipelineDesc);
}

TextureId Renderer::Impl::addTexture(int w, int h, const void* rgba, bool smooth, bool renderTarget)
{
    wgpu::TextureDescriptor desc;
    desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    if (renderTarget)
        desc.usage |= wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
    desc.dimension = wgpu::TextureDimension::e2D;
    desc.size = {static_cast<uint32_t>(std::max(w, 1)), static_cast<uint32_t>(std::max(h, 1)), 1};
    desc.format = targetFormat;
    GpuTexture entry;
    entry.texture = device.CreateTexture(&desc);
    entry.view = entry.texture.CreateView();
    entry.width = static_cast<float>(desc.size.width);
    entry.height = static_cast<float>(desc.size.height);
    entry.isTarget = renderTarget;

    if (rgba)
    {
        wgpu::TexelCopyTextureInfo destination;
        destination.texture = entry.texture;
        wgpu::TexelCopyBufferLayout layout;
        layout.bytesPerRow = desc.size.width * 4;
        layout.rowsPerImage = desc.size.height;
        queue.WriteTexture(&destination, rgba, static_cast<size_t>(desc.size.width) * desc.size.height * 4, &layout, &desc.size);
    }

    wgpu::BindGroupEntry entries[2];
    entries[0].binding = 0;
    entries[0].textureView = entry.view;
    entries[1].binding = 1;
    entries[1].sampler = smooth ? smoothSampler : pixelSampler;
    wgpu::BindGroupDescriptor group;
    group.layout = textureLayout;
    group.entryCount = 2;
    group.entries = entries;
    entry.bindGroup = device.CreateBindGroup(&group);

    textures.push_back(std::move(entry));
    return static_cast<TextureId>(textures.size() - 1);
}

bool Renderer::init(SDL_Window* window, bool vsync)
{
    Impl& r = *impl_;
    r.vsync = vsync;
    if (!r.createDevice(window))
        return false;

    SDL_GetWindowSizeInPixels(window, &r.width, &r.height);
    r.configureSurface();
    r.createLayouts();

    // Texture 0: the debug font atlas, whose white block also serves plain shapes.
    const std::vector<unsigned char> atlas = debugfont::buildAtlas();
    r.addTexture(debugfont::atlasWidth, debugfont::atlasHeight, atlas.data(), false, false);
    return true;
}

void Renderer::shutdown()
{
    impl_ = std::make_shared<Impl>();
}

void Renderer::resize(int width, int height)
{
    impl_->width = width;
    impl_->height = height;
    impl_->configureSurface();
}

Vec2 Renderer::outputSize() const
{
    return {static_cast<float>(impl_->width), static_cast<float>(impl_->height)};
}

RenderStats Renderer::stats() const
{
    RenderStats stats = impl_->frameStats;
    for (const GpuTexture& texture : impl_->textures)
    {
        if (!texture.texture) continue;
        ++stats.textures;
        stats.textureBytes += static_cast<size_t>(texture.width) * static_cast<size_t>(texture.height) * 4;
    }
    return stats;
}

void Renderer::beginFrame(Color clearColor)
{
    Impl& r = *impl_;
    for (TextureId id : r.releasedTextures)
        if (id != 0 && id < r.textures.size()) r.textures[id] = {};
    r.releasedTextures.clear();
    r.frameStats = {};
    r.vertices.clear();
    r.indices.clear();
    r.commands.clear();
    r.uploadedVertices = 0;
    r.uploadedIndices = 0;
    r.uploadedPasses = 0;
    r.states.assign(1, TransformState{{}, 1.0f, {0, 0, static_cast<float>(r.width), static_cast<float>(r.height)}, windowTarget});
    r.clearColor = clearColor;
    r.firstPass = true;
    r.blend = BlendMode::Alpha;

    wgpu::SurfaceTexture surfaceTexture;
    r.surface.GetCurrentTexture(&surfaceTexture);
    r.frameValid = surfaceTexture.status == wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal
        || surfaceTexture.status == wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal;
    if (!r.frameValid)
    {
        // Usually a resize or minimise; reconfigure and skip this frame.
        r.configureSurface();
        return;
    }
    r.frameTexture = surfaceTexture.texture;
    r.frameView = r.frameTexture.CreateView();
}

void Renderer::Impl::ensureBuffers(size_t passes)
{
    auto grow = [&](wgpu::Buffer& buffer, size_t& capacity, size_t needed, size_t minimum, wgpu::BufferUsage usage) {
        if (needed <= capacity)
            return;
        capacity = std::max(minimum, capacity);
        while (capacity < needed)
            capacity *= 2;
        wgpu::BufferDescriptor desc;
        desc.usage = usage | wgpu::BufferUsage::CopyDst;
        desc.size = capacity;
        buffer = device.CreateBuffer(&desc);
    };
    grow(vertexBuffer, vertexCapacity, (uploadedVertices + vertices.size()) * sizeof(Vertex), 64 * 1024, wgpu::BufferUsage::Vertex);
    grow(indexBuffer, indexCapacity, (uploadedIndices + indices.size()) * sizeof(uint32_t), 32 * 1024, wgpu::BufferUsage::Index);

    if (passes > uniformSlots)
    {
        uniformSlots = std::max<size_t>(16, uniformSlots);
        while (uniformSlots < passes)
            uniformSlots *= 2;
        wgpu::BufferDescriptor desc;
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        desc.size = uniformSlots * uniformSlot;
        uniformBuffer = device.CreateBuffer(&desc);

        wgpu::BindGroupEntry entry;
        entry.binding = 0;
        entry.buffer = uniformBuffer;
        entry.size = 16;
        wgpu::BindGroupDescriptor group;
        group.layout = uniformLayout;
        group.entryCount = 1;
        group.entries = &entry;
        uniformBindGroup = device.CreateBindGroup(&group);
    }
}

void Renderer::flush()
{
    Impl& r = *impl_;
    if (!r.frameValid || (r.commands.empty() && !r.firstPass))
        return;

    // One render pass per run of commands with the same target.
    struct Pass
    {
        TextureId target;
        size_t first;
        size_t count;
    };
    std::vector<Pass> passes;
    for (size_t i = 0; i < r.commands.size(); i++)
    {
        if (passes.empty() || passes.back().target != r.commands[i].target)
            passes.push_back({r.commands[i].target, i, 0});
        passes.back().count++;
    }
    if (r.firstPass && (passes.empty() || passes.front().target != windowTarget))
        passes.insert(passes.begin(), {windowTarget, 0, 0}); // still clear the window

    r.ensureBuffers(r.uploadedPasses + passes.size());
    const size_t vertexOffset = r.uploadedVertices * sizeof(Vertex);
    const size_t indexOffset = r.uploadedIndices * sizeof(uint32_t);
    const size_t uniformOffset = r.uploadedPasses * uniformSlot;
    if (!r.vertices.empty())
    {
        r.queue.WriteBuffer(r.vertexBuffer, vertexOffset, r.vertices.data(), r.vertices.size() * sizeof(Vertex));
        r.queue.WriteBuffer(r.indexBuffer, indexOffset, r.indices.data(), r.indices.size() * sizeof(uint32_t));
    }
    std::vector<float> uniforms(passes.size() * uniformSlot / sizeof(float), 0.0f);
    for (size_t p = 0; p < passes.size(); p++)
    {
        const bool window = passes[p].target == windowTarget;
        uniforms[p * uniformSlot / sizeof(float)] = window ? static_cast<float>(r.width) : r.textures[passes[p].target].width;
        uniforms[p * uniformSlot / sizeof(float) + 1] = window ? static_cast<float>(r.height) : r.textures[passes[p].target].height;
    }
    r.queue.WriteBuffer(r.uniformBuffer, uniformOffset, uniforms.data(), uniforms.size() * sizeof(float));

    const wgpu::CommandEncoder encoder = r.device.CreateCommandEncoder();
    for (size_t p = 0; p < passes.size(); p++)
    {
        const Pass& pass = passes[p];
        const bool window = pass.target == windowTarget;
        GpuTexture* target = window ? nullptr : &r.textures[pass.target];

        wgpu::RenderPassColorAttachment attachment;
        attachment.view = window ? r.frameView : target->view;
        attachment.storeOp = wgpu::StoreOp::Store;
        attachment.loadOp = wgpu::LoadOp::Load;
        if (window && r.firstPass)
        {
            attachment.loadOp = wgpu::LoadOp::Clear;
            attachment.clearValue = toGpu(r.clearColor);
            r.firstPass = false;
        }
        else if (!window && target->pendingClear)
        {
            attachment.loadOp = wgpu::LoadOp::Clear;
            attachment.clearValue = toGpu(*target->pendingClear);
            target->pendingClear.reset();
        }
        wgpu::RenderPassDescriptor passDesc;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &attachment;

        const wgpu::RenderPassEncoder encoderPass = encoder.BeginRenderPass(&passDesc);
        const uint32_t offset = static_cast<uint32_t>(uniformOffset + p * uniformSlot);
        encoderPass.SetBindGroup(0, r.uniformBindGroup, 1, &offset);
        if (!r.vertices.empty())
        {
            encoderPass.SetVertexBuffer(0, r.vertexBuffer, vertexOffset);
            encoderPass.SetIndexBuffer(r.indexBuffer, wgpu::IndexFormat::Uint32, indexOffset);
        }

        const wgpu::TextureFormat format = window ? r.format : targetFormat;
        TextureId boundTexture = UINT32_MAX;
        int boundBlend = -1;
        for (size_t i = pass.first; i < pass.first + pass.count; i++)
        {
            const DrawCommand& command = r.commands[i];
            if (command.indexCount == 0)
                continue;
            if (static_cast<int>(command.blend) != boundBlend)
            {
                encoderPass.SetPipeline(r.pipelineFor(format, command.blend));
                boundBlend = static_cast<int>(command.blend);
            }
            if (command.texture != boundTexture)
            {
                encoderPass.SetBindGroup(1, r.textures[command.texture].bindGroup);
                boundTexture = command.texture;
            }
            encoderPass.SetScissorRect(static_cast<uint32_t>(command.clip.x), static_cast<uint32_t>(command.clip.y),
                static_cast<uint32_t>(command.clip.w), static_cast<uint32_t>(command.clip.h));
            encoderPass.DrawIndexed(command.indexCount, 1, command.firstIndex);
            ++r.frameStats.drawCalls;
        }
        encoderPass.End();
    }
    const wgpu::CommandBuffer commands = encoder.Finish();
    r.queue.Submit(1, &commands);
    r.frameStats.passes += static_cast<uint32_t>(passes.size());
    r.frameStats.vertices += static_cast<uint32_t>(r.vertices.size());
    r.uploadedVertices += r.vertices.size();
    r.uploadedIndices += r.indices.size();
    r.uploadedPasses += passes.size();

    r.vertices.clear();
    r.indices.clear();
    r.commands.clear();
}

bool Renderer::saveScreenshot(const std::string& path)
{
    Impl& r = *impl_;
    if (!r.frameValid || !r.canReadBack)
    {
        SDL_Log("Screenshot: this GPU can't read back the window");
        return false;
    }

    const uint32_t width = r.frameTexture.GetWidth();
    const uint32_t height = r.frameTexture.GetHeight();
    const uint32_t rowBytes = (width * 4 + 255) / 256 * 256; // copies need 256-byte aligned rows

    wgpu::BufferDescriptor bufferDesc;
    bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
    bufferDesc.size = static_cast<uint64_t>(rowBytes) * height;
    const wgpu::Buffer buffer = r.device.CreateBuffer(&bufferDesc);

    wgpu::TexelCopyTextureInfo source;
    source.texture = r.frameTexture;
    wgpu::TexelCopyBufferInfo destination;
    destination.buffer = buffer;
    destination.layout.bytesPerRow = rowBytes;
    destination.layout.rowsPerImage = height;
    const wgpu::Extent3D extent{width, height, 1};

    const wgpu::CommandEncoder encoder = r.device.CreateCommandEncoder();
    encoder.CopyTextureToBuffer(&source, &destination, &extent);
    const wgpu::CommandBuffer commands = encoder.Finish();
    r.queue.Submit(1, &commands);

    bool mapped = false;
    r.instance.WaitAny(buffer.MapAsync(wgpu::MapMode::Read, 0, bufferDesc.size, wgpu::CallbackMode::WaitAnyOnly,
        [&mapped](wgpu::MapAsyncStatus status, wgpu::StringView) { mapped = status == wgpu::MapAsyncStatus::Success; }), UINT64_MAX);
    if (!mapped)
        return false;

    const bool bgra = r.format == wgpu::TextureFormat::BGRA8Unorm || r.format == wgpu::TextureFormat::BGRA8UnormSrgb;
    SDL_Surface* image = SDL_CreateSurface(static_cast<int>(width), static_cast<int>(height), SDL_PIXELFORMAT_RGBA32);
    const auto* data = static_cast<const unsigned char*>(buffer.GetConstMappedRange());
    for (uint32_t y = 0; y < height; y++)
    {
        const unsigned char* in = data + static_cast<size_t>(y) * rowBytes;
        unsigned char* out = static_cast<unsigned char*>(image->pixels) + static_cast<size_t>(y) * image->pitch;
        for (uint32_t x = 0; x < width; x++)
        {
            out[x * 4 + 0] = in[x * 4 + (bgra ? 2 : 0)];
            out[x * 4 + 1] = in[x * 4 + 1];
            out[x * 4 + 2] = in[x * 4 + (bgra ? 0 : 2)];
            out[x * 4 + 3] = 255;
        }
    }
    buffer.Unmap();

    const bool saved = SDL_SavePNG(image, path.c_str());
    if (!saved)
        SDL_Log("Saving %s failed: %s", path.c_str(), SDL_GetError());
    SDL_DestroySurface(image);
    return saved;
}

void Renderer::endFrame()
{
    Impl& r = *impl_;
    flush();
    if (r.frameValid)
#if !defined(SDL_PLATFORM_EMSCRIPTEN)
        (void)r.surface.Present();
#else
        (void)0; // the browser presents the canvas when control returns to its event loop
#endif
    r.frameView = nullptr;
    r.frameTexture = nullptr;
    r.frameValid = false;
}

Rect Renderer::bounds() const
{
    const TransformState& s = impl_->states.back();
    return {(s.clip.x - s.offset.x) / s.scale, (s.clip.y - s.offset.y) / s.scale, s.clip.w / s.scale, s.clip.h / s.scale};
}

void Renderer::pushViewport(const Rect& area)
{
    Impl& r = *impl_;
    TransformState next = r.states.back();
    const Vec2 topLeft = r.toScreen(area.position());
    const Rect screen{topLeft.x, topLeft.y, area.w * next.scale, area.h * next.scale};
    next.clip = next.clip.intersect(screen);
    next.offset = topLeft;
    r.states.push_back(next);
}

void Renderer::pushTransform(Vec2 translate, float scale)
{
    Impl& r = *impl_;
    TransformState next = r.states.back();
    next.offset = r.toScreen(translate);
    next.scale *= scale;
    r.states.push_back(next);
}

void Renderer::pop()
{
    if (impl_->states.size() > 1)
        impl_->states.pop_back();
}

void Renderer::pushTarget(TextureId target, std::optional<Color> clear)
{
    Impl& r = *impl_;
    if (target >= r.textures.size() || !r.textures[target].isTarget)
        return;
    GpuTexture& texture = r.textures[target];
    if (clear)
    {
        // Clears are ordered operations; another pass may already reference this target.
        flush();
        texture.pendingClear = clear;
        // An empty command makes sure the target gets a pass (and its clear) even if nothing is drawn.
        r.commands.push_back({0, target, r.blend, {}, static_cast<uint32_t>(r.indices.size()), 0});
    }
    r.states.push_back({{}, 1.0f, {0, 0, texture.width, texture.height}, target});
}

void Renderer::popTarget()
{
    pop();
}

void Renderer::setBlendMode(BlendMode mode)
{
    impl_->blend = mode;
}

BlendMode Renderer::blendMode() const
{
    return impl_->blend;
}

uint32_t Renderer::Impl::beginShape(TextureId texture)
{
    const TransformState& s = states.back();
    // Round endpoints, then clamp to the attachment; fractional nested viewports must
    // never submit a scissor outside its texture.
    const float targetWidth = s.target == windowTarget ? static_cast<float>(width) : textures[s.target].width;
    const float targetHeight = s.target == windowTarget ? static_cast<float>(height) : textures[s.target].height;
    const float left = std::clamp(std::floor(s.clip.x), 0.0f, targetWidth);
    const float top = std::clamp(std::floor(s.clip.y), 0.0f, targetHeight);
    const float right = std::clamp(std::ceil(s.clip.x + s.clip.w), left, targetWidth);
    const float bottom = std::clamp(std::ceil(s.clip.y + s.clip.h), top, targetHeight);
    const Rect clip{left, top, right - left, bottom - top};
    if (commands.empty() || commands.back().texture != texture || commands.back().target != s.target
        || commands.back().blend != blend || !sameRect(commands.back().clip, clip) || commands.back().indexCount == 0)
        commands.push_back({texture, s.target, blend, clip, static_cast<uint32_t>(indices.size()), 0});
    return static_cast<uint32_t>(vertices.size());
}

void Renderer::Impl::addQuad(TextureId texture, const Vec2 (&corners)[4], const Rect& uv, Color color)
{
    if (texture >= textures.size() || !textures[texture].texture)
        return;
    const TransformState& s = states.back();
    if (s.clip.w <= 0 || s.clip.h <= 0)
        return;
    const uint32_t first = beginShape(texture);
    const uint32_t packed = color.packed();
    const float u[4] = {uv.x, uv.x + uv.w, uv.x + uv.w, uv.x};
    const float v[4] = {uv.y, uv.y, uv.y + uv.h, uv.y + uv.h};
    for (int i = 0; i < 4; i++)
    {
        const Vec2 p = toScreen(corners[i]);
        vertices.push_back({p.x, p.y, u[i], v[i], packed});
    }
    const uint32_t pattern[6] = {first, first + 1, first + 2, first + 2, first + 3, first};
    indices.insert(indices.end(), pattern, pattern + 6);
    commands.back().indexCount += 6;
}

namespace
{

Rect whiteUv()
{
    constexpr float half = debugfont::glyphSize / 2.0f;
    return {(debugfont::whiteX + half) / debugfont::atlasWidth, (debugfont::whiteY + half) / debugfont::atlasHeight, 0, 0};
}

}

void Renderer::clear(Color color)
{
    fillRect(bounds(), color);
}

void Renderer::fillRect(const Rect& rect, Color color)
{
    if (!visible(rect))
        return;
    const Vec2 corners[4] = {{rect.x, rect.y}, {rect.x + rect.w, rect.y}, {rect.x + rect.w, rect.y + rect.h}, {rect.x, rect.y + rect.h}};
    impl_->addQuad(0, corners, whiteUv(), color);
}

void Renderer::fillRects(std::span<const Rect> rects, Color color)
{
    impl_->vertices.reserve(impl_->vertices.size() + rects.size() * 4);
    impl_->indices.reserve(impl_->indices.size() + rects.size() * 6);
    for (const Rect& rect : rects)
        fillRect(rect, color);
}

void Renderer::drawRect(const Rect& rect, Color color, float thickness)
{
    fillRect({rect.x, rect.y, rect.w, thickness}, color);
    fillRect({rect.x, rect.y + rect.h - thickness, rect.w, thickness}, color);
    fillRect({rect.x, rect.y + thickness, thickness, rect.h - thickness * 2}, color);
    fillRect({rect.x + rect.w - thickness, rect.y + thickness, thickness, rect.h - thickness * 2}, color);
}

void Renderer::drawLine(Vec2 from, Vec2 to, Color color, float thickness)
{
    const Vec2 delta = to - from;
    const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (length <= 0)
        return;
    const Vec2 side = Vec2{-delta.y, delta.x} * (thickness / 2.0f / length);
    const Vec2 corners[4] = {from + side, to + side, to - side, from - side};
    impl_->addQuad(0, corners, whiteUv(), color);
}

void Renderer::fillFan(Vec2 center, std::span<const Vec2> rim, Color centerColor, Color rimColor)
{
    Impl& r = *impl_;
    if (rim.size() < 2 || r.states.back().clip.w <= 0)
        return;
    const uint32_t first = r.beginShape(0);
    const Rect uv = whiteUv();
    const Vec2 c = r.toScreen(center);
    r.vertices.push_back({c.x, c.y, uv.x, uv.y, centerColor.packed()});
    const uint32_t rimPacked = rimColor.packed();
    for (const Vec2& point : rim)
    {
        const Vec2 p = r.toScreen(point);
        r.vertices.push_back({p.x, p.y, uv.x, uv.y, rimPacked});
    }
    const uint32_t n = static_cast<uint32_t>(rim.size());
    for (uint32_t i = 0; i < n; i++)
    {
        const uint32_t next = (i + 1) % n;
        r.indices.insert(r.indices.end(), {first, first + 1 + i, first + 1 + next});
    }
    r.commands.back().indexCount += n * 3;
}

void Renderer::fillCircle(Vec2 center, float radius, Color color, int segments)
{
    if (!visible({center.x - radius, center.y - radius, radius * 2, radius * 2}))
        return;
    std::vector<Vec2> rim(static_cast<size_t>(std::max(segments, 3)));
    for (size_t i = 0; i < rim.size(); i++)
    {
        const float a = static_cast<float>(i) / static_cast<float>(rim.size()) * 6.2831853f;
        rim[i] = {center.x + std::cos(a) * radius, center.y + std::sin(a) * radius};
    }
    fillFan(center, rim, color, color);
}

void Renderer::drawSprite(TextureId texture, const Rect& dest, Color tint)
{
    drawSpriteRegion(texture, dest, {0, 0, 1, 1}, tint);
}

void Renderer::drawSpriteRegion(TextureId texture, const Rect& dest, const Rect& uv, Color tint)
{
    if (texture >= impl_->textures.size() || !visible(dest))
        return;
    const Vec2 corners[4] = {{dest.x, dest.y}, {dest.x + dest.w, dest.y}, {dest.x + dest.w, dest.y + dest.h}, {dest.x, dest.y + dest.h}};
    impl_->addQuad(texture, corners, uv, tint);
}

void Renderer::drawSpriteRotated(TextureId texture, Vec2 center, Vec2 size, float radians, Color tint)
{
    if (texture >= impl_->textures.size())
        return;
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    const Vec2 half = size / 2.0f;
    auto rotate = [&](Vec2 p) { return Vec2{center.x + p.x * c - p.y * s, center.y + p.x * s + p.y * c}; };
    const Vec2 corners[4] = {rotate({-half.x, -half.y}), rotate({half.x, -half.y}), rotate({half.x, half.y}), rotate({-half.x, half.y})};
    impl_->addQuad(texture, corners, {0, 0, 1, 1}, tint);
}

void Renderer::drawText(Vec2 position, std::string_view text, Color color, float scale)
{
    const float size = debugfont::glyphSize * scale;
    Vec2 cursor = position;
    for (const char ch : text)
    {
        if (ch == '\n')
        {
            cursor = {position.x, cursor.y + lineHeight(scale)};
            continue;
        }
        int c = static_cast<unsigned char>(ch);
        if (c < debugfont::firstChar || c >= debugfont::lastChar)
            c = '?';
        if (c != ' ')
        {
            const int index = c - debugfont::firstChar;
            const Rect uv{
                static_cast<float>(index % debugfont::columns * debugfont::glyphSize) / debugfont::atlasWidth,
                static_cast<float>(index / debugfont::columns * debugfont::glyphSize) / debugfont::atlasHeight,
                static_cast<float>(debugfont::glyphSize) / debugfont::atlasWidth,
                static_cast<float>(debugfont::glyphSize) / debugfont::atlasHeight,
            };
            const Vec2 corners[4] = {cursor, {cursor.x + size, cursor.y}, {cursor.x + size, cursor.y + size}, {cursor.x, cursor.y + size}};
            impl_->addQuad(0, corners, uv, color);
        }
        cursor.x += size;
    }
}

float Renderer::textWidth(std::string_view text, float scale)
{
    return static_cast<float>(text.size()) * debugfont::glyphSize * scale;
}

float Renderer::lineHeight(float scale)
{
    return (debugfont::glyphSize + 2) * scale;
}

TextureId Renderer::createTexture(int width, int height, const void* rgba, bool smooth)
{
    return impl_->addTexture(width, height, rgba, smooth, false);
}

std::optional<TextureId> Renderer::loadTexture(std::span<const unsigned char> fileBytes, bool smooth)
{
    std::optional<Image> image = decodeImage(fileBytes);
    if (!image)
        return std::nullopt;
    // Huge battle maps would fail to upload on most GPUs; a slightly softer map is better than none.
    if (std::max(image->width, image->height) > maxTextureSize)
    {
        SDL_Log("Image %dx%d scaled down to fit %d pixels", image->width, image->height, maxTextureSize);
        image = fitImage(std::move(*image), maxTextureSize);
    }
    return createTexture(image->width, image->height, image->rgba.data(), smooth);
}

void Renderer::updateTexture(TextureId texture, const void* rgba)
{
    Impl& r = *impl_;
    if (texture >= r.textures.size())
        return;
    const GpuTexture& entry = r.textures[texture];
    const uint32_t width = static_cast<uint32_t>(entry.width);
    const uint32_t height = static_cast<uint32_t>(entry.height);
    wgpu::TexelCopyTextureInfo destination;
    destination.texture = entry.texture;
    wgpu::TexelCopyBufferLayout layout;
    layout.bytesPerRow = width * 4;
    layout.rowsPerImage = height;
    const wgpu::Extent3D size{width, height, 1};
    r.queue.WriteTexture(&destination, rgba, static_cast<size_t>(width) * height * 4, &layout, &size);
}

Vec2 Renderer::textureSize(TextureId texture) const
{
    if (texture >= impl_->textures.size())
        return {};
    return {impl_->textures[texture].width, impl_->textures[texture].height};
}

TextureId Renderer::createRenderTarget(int width, int height)
{
    return impl_->addTexture(width, height, nullptr, true, true);
}

std::function<void()> Renderer::textureRelease(TextureId texture)
{
    return [weak = std::weak_ptr<Impl>(impl_), texture] {
        if (const auto r = weak.lock(); r && texture != 0 && texture < r->textures.size())
            r->releasedTextures.push_back(texture);
    };
}

void Renderer::fillLightFan(Vec2 center, std::span<const Vec2> rim, float radius, Color color)
{
    if (rim.size() < 3 || radius <= 0) return;
    Impl& r = *impl_;
    if (r.states.back().clip.w <= 0 || r.states.back().clip.h <= 0) return;
    const uint32_t first = r.beginShape(0);
    const Rect uv = whiteUv();
    const Vec2 origin = r.toScreen(center);
    r.vertices.push_back({origin.x, origin.y, uv.x, uv.y, color.packed()});
    for (Vec2 point : rim)
    {
        const Vec2 delta = point - center;
        const float falloff = std::clamp(1 - std::sqrt(delta.x * delta.x + delta.y * delta.y) / radius, 0.0f, 1.0f);
        Color edge = color;
        edge.a = static_cast<uint8_t>(color.a * falloff);
        const Vec2 p = r.toScreen(point);
        r.vertices.push_back({p.x, p.y, uv.x, uv.y, edge.packed()});
    }
    for (uint32_t i = 0; i < static_cast<uint32_t>(rim.size()); ++i)
    {
        r.indices.push_back(first);
        r.indices.push_back(first + i + 1);
        r.indices.push_back(first + (i + 1) % static_cast<uint32_t>(rim.size()) + 1);
    }
    r.commands.back().indexCount += static_cast<uint32_t>(rim.size() * 3);
}

}
