#include "overlay.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <iterator>
#include <memory>
#include <mutex>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <rex/logging.h>
#include <rex/ui/immediate_drawer.h>

using Microsoft::WRL::ComPtr;

namespace glue2gpu
{
namespace
{
constexpr uint32_t kSlots = 6;          // upload buffers in use round-robin (ReGPU has 3 frames in flight)
constexpr uint32_t kMaxTextures = 256;  // SRV slots
constexpr uint64_t kReleaseDelay = 8;   // presents before a dropped GPU object is released

std::atomic<uint64_t> g_presents{ 0 }; // incremented by the presenting thread

// GPU objects dropped by either thread, released on the presenting thread once the GPU is done
// with them (and their SRV slot reused).
struct Garbage
{
    uint64_t due;
    ComPtr<ID3D12Resource> resource;
    int32_t srv;
};
std::mutex g_garbageMutex;
std::vector<Garbage> g_garbage;

void Release(ComPtr<ID3D12Resource> resource, int32_t srv = -1)
{
    if (!resource && srv < 0)
        return;
    std::lock_guard lock(g_garbageMutex);
    g_garbage.push_back({ g_presents + kReleaseDelay, std::move(resource), srv });
}

// A texture's pixels, uploaded on the presenting thread the first time it is drawn.
struct GpuTexture
{
    uint32_t width = 0, height = 0;
    bool point = false;
    std::vector<uint8_t> rgba;
    ComPtr<ID3D12Resource> resource;
    int32_t srv = -1;
    ~GpuTexture() { Release(std::move(resource), srv); }
};

class OverlayTexture final : public rex::ui::ImmediateTexture
{
public:
    OverlayTexture(uint32_t width, uint32_t height, bool point, const uint8_t* data)
        : ImmediateTexture(width, height), gpu(std::make_shared<GpuTexture>())
    {
        gpu->width = width;
        gpu->height = height;
        gpu->point = point;
        if (data)
            gpu->rgba.assign(data, data + size_t(width) * height * 4);
        else
            gpu->rgba.assign(size_t(width) * height * 4, 0xFF);
    }
    std::shared_ptr<GpuTexture> gpu;
};

struct Draw
{
    uint32_t count, firstIndex;
    int32_t baseVertex;
    std::shared_ptr<GpuTexture> texture; // null: white
    bool scissor;
    float left, top, right, bottom;
};

struct Frame
{
    float width = 1, height = 1; // the coordinate space
    std::vector<rex::ui::ImmediateVertex> vertices;
    std::vector<uint16_t> indices;
    std::vector<Draw> draws;
};

std::mutex g_frameMutex;
std::shared_ptr<const Frame> g_latest;

class OverlayDrawer final : public rex::ui::ImmediateDrawer
{
public:
    std::unique_ptr<rex::ui::ImmediateTexture> CreateTexture(uint32_t width, uint32_t height,
                                                             rex::ui::ImmediateTextureFilter filter, bool,
                                                             const uint8_t* data) override
    {
        return std::make_unique<OverlayTexture>(width, height, filter == rex::ui::ImmediateTextureFilter::kNearest, data);
    }

    void Begin(rex::ui::UIDrawContext& context, float width, float height) override
    {
        ImmediateDrawer::Begin(context, width, height);
        frame_ = std::make_shared<Frame>();
        frame_->width = std::max(width, 1.0f);
        frame_->height = std::max(height, 1.0f);
    }

    void BeginDrawBatch(const rex::ui::ImmediateDrawBatch& batch) override
    {
        vertexBase_ = int32_t(frame_->vertices.size());
        indexBase_ = uint32_t(frame_->indices.size());
        frame_->vertices.insert(frame_->vertices.end(), batch.vertices, batch.vertices + batch.vertex_count);
        if (batch.indices)
            frame_->indices.insert(frame_->indices.end(), batch.indices, batch.indices + batch.index_count);
    }

    void Draw(const rex::ui::ImmediateDraw& draw) override
    {
        // Dear ImGui draws indexed triangles only.
        if (draw.primitive_type != rex::ui::ImmediatePrimitiveType::kTriangles || draw.count <= 0)
            return;
        auto* texture = static_cast<OverlayTexture*>(draw.texture);
        frame_->draws.push_back({ uint32_t(draw.count), indexBase_ + uint32_t(draw.index_offset), vertexBase_ + draw.base_vertex,
                                  texture ? texture->gpu : nullptr, draw.scissor, draw.scissor_left, draw.scissor_top,
                                  draw.scissor_right, draw.scissor_bottom });
    }

    void EndDrawBatch() override {}

    void End() override
    {
        std::lock_guard lock(g_frameMutex);
        g_latest = std::move(frame_);
        ImmediateDrawer::End();
    }

private:
    std::shared_ptr<Frame> frame_;
    int32_t vertexBase_ = 0;
    uint32_t indexBase_ = 0;
};

constexpr char kShader[] = R"(
cbuffer C : register(b0) { float2 scale; float2 offset; uint pointSample; };
Texture2D t : register(t0);
SamplerState linearSampler : register(s0);
SamplerState pointSampler : register(s1);
struct V { float2 pos : POSITION; float2 uv : TEXCOORD; float4 color : COLOR; };
struct P { float4 pos : SV_Position; float2 uv : TEXCOORD; float4 color : COLOR; };
P vs(V v) { P p; p.pos = float4(v.pos * scale + offset, 0, 1); p.uv = v.uv; p.color = v.color; return p; }
float4 ps(P p) : SV_Target
{
    return p.color * (pointSample ? t.Sample(pointSampler, p.uv) : t.Sample(linearSampler, p.uv));
}
)";

// GPU state, presenting thread only.
struct Gpu
{
    bool failed = false;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pso;
    DXGI_FORMAT psoFormat = DXGI_FORMAT_UNKNOWN;
    ComPtr<ID3DBlob> vs, ps;
    ComPtr<ID3D12DescriptorHeap> heap;
    uint32_t srvStride = 0;
    std::vector<uint32_t> freeSrvs;
    std::shared_ptr<GpuTexture> white;
    struct Slot
    {
        ComPtr<ID3D12Resource> buffer;
        uint8_t* data = nullptr;
        size_t size = 0;
    } slots[kSlots];
} g_gpu;

bool Compile(const char* entry, const char* target, ComPtr<ID3DBlob>& out)
{
    using Fn = decltype(&D3DCompile);
    static Fn compile = [] {
        HMODULE m = LoadLibraryW(L"d3dcompiler_47.dll");
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "D3DCompile")) : nullptr;
    }();
    ComPtr<ID3DBlob> errors;
    if (!compile || FAILED(compile(kShader, sizeof(kShader) - 1, "overlay", nullptr, nullptr, entry, target, 0, 0, &out, &errors)))
    {
        REXLOG_ERROR("regpu overlay: shader {} failed: {}", entry,
                     errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no d3dcompiler_47.dll");
        return false;
    }
    return true;
}

bool Init(ID3D12GraphicsCommandList* list, DXGI_FORMAT format)
{
    if (g_gpu.failed)
        return false;
    if (!g_gpu.device)
    {
        g_gpu.failed = true; // until everything below worked
        list->GetDevice(IID_PPV_ARGS(&g_gpu.device));
        ID3D12Device* d = g_gpu.device.Get();
        D3D12_DESCRIPTOR_RANGE range{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0 };
        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants = { 0, 0, 5 };
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable = { 1, &range };
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC samplers[2]{};
        for (UINT i = 0; i < 2; i++)
        {
            samplers[i].Filter = i ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            samplers[i].AddressU = samplers[i].AddressV = samplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
            samplers[i].ShaderRegister = i;
            samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        }
        D3D12_ROOT_SIGNATURE_DESC rd{ 2, params, 2, samplers, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT };
        ComPtr<ID3DBlob> blob, error;
        if (FAILED(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)) ||
            FAILED(d->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&g_gpu.root))))
            return false;
        if (!Compile("vs", "vs_5_0", g_gpu.vs) || !Compile("ps", "ps_5_0", g_gpu.ps))
            return false;
        D3D12_DESCRIPTOR_HEAP_DESC hd{ D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kMaxTextures, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0 };
        if (FAILED(d->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&g_gpu.heap))))
            return false;
        g_gpu.srvStride = d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        for (uint32_t i = kMaxTextures; i-- > 0;)
            g_gpu.freeSrvs.push_back(i);
        g_gpu.white = std::make_shared<GpuTexture>();
        g_gpu.white->width = g_gpu.white->height = 1;
        g_gpu.white->rgba.assign(4, 0xFF);
        g_gpu.failed = false;
    }
    if (g_gpu.psoFormat != format)
    {
        D3D12_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = g_gpu.root.Get();
        pd.VS = { g_gpu.vs->GetBufferPointer(), g_gpu.vs->GetBufferSize() };
        pd.PS = { g_gpu.ps->GetBufferPointer(), g_gpu.ps->GetBufferSize() };
        auto& rt = pd.BlendState.RenderTarget[0];
        rt.BlendEnable = TRUE;
        rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        rt.BlendOp = D3D12_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D12_BLEND_ONE;
        rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pd.SampleMask = UINT_MAX;
        pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pd.RasterizerState.DepthClipEnable = TRUE;
        pd.InputLayout = { layout, UINT(std::size(layout)) };
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets = 1;
        pd.RTVFormats[0] = format;
        pd.SampleDesc.Count = 1;
        g_gpu.pso.Reset();
        if (FAILED(g_gpu.device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&g_gpu.pso))))
        {
            REXLOG_ERROR("regpu overlay: pipeline for format {} failed", int(format));
            g_gpu.failed = true;
            return false;
        }
        g_gpu.psoFormat = format;
    }
    return true;
}

ComPtr<ID3D12Resource> CreateBuffer(D3D12_HEAP_TYPE type, size_t size, D3D12_RESOURCE_STATES state)
{
    D3D12_HEAP_PROPERTIES hp{ type };
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = size;
    d.Height = d.DepthOrArraySize = d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> r;
    g_gpu.device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, state, nullptr, IID_PPV_ARGS(&r));
    return r;
}

// Uploads a texture the first time it is drawn (copied in this command list, before the draws).
bool Upload(ID3D12GraphicsCommandList* list, GpuTexture& t)
{
    if (t.resource)
        return true;
    if (g_gpu.freeSrvs.empty())
        return false;
    D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = t.width;
    d.Height = t.height;
    d.DepthOrArraySize = d.MipLevels = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    if (FAILED(g_gpu.device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                     IID_PPV_ARGS(&t.resource))))
        return false;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT64 total = 0;
    g_gpu.device->GetCopyableFootprints(&d, 0, 1, 0, &fp, nullptr, nullptr, &total);
    ComPtr<ID3D12Resource> staging = CreateBuffer(D3D12_HEAP_TYPE_UPLOAD, size_t(total), D3D12_RESOURCE_STATE_GENERIC_READ);
    uint8_t* p = nullptr;
    if (!staging || FAILED(staging->Map(0, nullptr, reinterpret_cast<void**>(&p))))
        return false;
    for (uint32_t y = 0; y < t.height; y++)
        std::memcpy(p + fp.Offset + size_t(y) * fp.Footprint.RowPitch, t.rgba.data() + size_t(y) * t.width * 4, size_t(t.width) * 4);
    staging->Unmap(0, nullptr);
    D3D12_TEXTURE_COPY_LOCATION dst{ t.resource.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX }, src{ staging.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT };
    dst.SubresourceIndex = 0;
    src.PlacedFootprint = fp;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = t.resource.Get();
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    list->ResourceBarrier(1, &b);
    Release(std::move(staging));
    t.srv = int32_t(g_gpu.freeSrvs.back());
    g_gpu.freeSrvs.pop_back();
    D3D12_CPU_DESCRIPTOR_HANDLE h = g_gpu.heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += SIZE_T(t.srv) * g_gpu.srvStride;
    g_gpu.device->CreateShaderResourceView(t.resource.Get(), nullptr, h);
    return true;
}

void CollectGarbage()
{
    std::lock_guard lock(g_garbageMutex);
    std::erase_if(g_garbage, [](const Garbage& g) {
        if (g.due > g_presents)
            return false;
        if (g.srv >= 0)
            g_gpu.freeSrvs.push_back(uint32_t(g.srv));
        return true;
    });
}

void (*g_frameCallback)(void*) = nullptr;
void* g_frameContext = nullptr;
} // namespace

rex::ui::ImmediateDrawer* OverlayImmediateDrawer()
{
    static OverlayDrawer drawer;
    return &drawer;
}

void SetOverlayFrameCallback(void (*callback)(void*), void* context)
{
    g_frameContext = context;
    g_frameCallback = callback;
}

void OverlayFramePresented()
{
    if (auto callback = g_frameCallback)
        callback(g_frameContext);
}

void DrawOverlay(void* commandList, void* backBuffer, size_t rtv, uint32_t width, uint32_t height)
{
    g_presents++;
    CollectGarbage();
    std::shared_ptr<const Frame> frame;
    {
        std::lock_guard lock(g_frameMutex);
        frame = g_latest;
    }
    if (!frame || frame->draws.empty())
        return;
    auto* list = static_cast<ID3D12GraphicsCommandList*>(commandList);
    if (!Init(list, static_cast<ID3D12Resource*>(backBuffer)->GetDesc().Format))
        return;

    // Vertices and indices into this present's upload buffer.
    const size_t vertexBytes = frame->vertices.size() * sizeof(rex::ui::ImmediateVertex);
    const size_t indexBytes = frame->indices.size() * sizeof(uint16_t);
    const size_t need = (vertexBytes + 255) / 256 * 256 + indexBytes;
    auto& slot = g_gpu.slots[g_presents % kSlots];
    if (slot.size < need)
    {
        Release(std::move(slot.buffer));
        slot.size = std::max<size_t>(need * 2, 256 * 1024);
        slot.buffer = CreateBuffer(D3D12_HEAP_TYPE_UPLOAD, slot.size, D3D12_RESOURCE_STATE_GENERIC_READ);
        if (!slot.buffer || FAILED(slot.buffer->Map(0, nullptr, reinterpret_cast<void**>(&slot.data))))
        {
            slot.size = 0;
            return;
        }
    }
    const size_t indexOffset = (vertexBytes + 255) / 256 * 256;
    std::memcpy(slot.data, frame->vertices.data(), vertexBytes);
    std::memcpy(slot.data + indexOffset, frame->indices.data(), indexBytes);

    Upload(list, *g_gpu.white);
    for (const auto& d : frame->draws)
        if (d.texture)
            Upload(list, *d.texture);

    D3D12_CPU_DESCRIPTOR_HANDLE target{ rtv };
    list->OMSetRenderTargets(1, &target, FALSE, nullptr);
    const D3D12_VIEWPORT viewport{ 0, 0, float(width), float(height), 0, 1 };
    list->RSSetViewports(1, &viewport);
    list->SetGraphicsRootSignature(g_gpu.root.Get());
    list->SetPipelineState(g_gpu.pso.Get());
    ID3D12DescriptorHeap* heaps[] = { g_gpu.heap.Get() };
    list->SetDescriptorHeaps(1, heaps);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const D3D12_GPU_VIRTUAL_ADDRESS base = slot.buffer->GetGPUVirtualAddress();
    const D3D12_VERTEX_BUFFER_VIEW vb{ base, UINT(vertexBytes), sizeof(rex::ui::ImmediateVertex) };
    const D3D12_INDEX_BUFFER_VIEW ib{ base + indexOffset, UINT(indexBytes), DXGI_FORMAT_R16_UINT };
    list->IASetVertexBuffers(0, 1, &vb);
    list->IASetIndexBuffer(&ib);
    // Coordinates are in the overlay's space (the window's logical size); the back buffer covers it.
    const float constants[4] = { 2.0f / frame->width, -2.0f / frame->height, -1.0f, 1.0f };
    list->SetGraphicsRoot32BitConstants(0, 4, constants, 0);
    const float sx = float(width) / frame->width, sy = float(height) / frame->height;
    int32_t lastSrv = -2;
    uint32_t lastPoint = 2;
    for (const auto& d : frame->draws)
    {
        GpuTexture& t = d.texture && d.texture->resource ? *d.texture : *g_gpu.white;
        if (t.srv < 0)
            continue;
        D3D12_RECT r{ 0, 0, LONG(width), LONG(height) };
        if (d.scissor)
            r = { LONG(std::max(0.0f, d.left * sx)), LONG(std::max(0.0f, d.top * sy)), LONG(std::min(float(width), d.right * sx + 0.5f)),
                  LONG(std::min(float(height), d.bottom * sy + 0.5f)) };
        if (r.right <= r.left || r.bottom <= r.top)
            continue;
        list->RSSetScissorRects(1, &r);
        if (t.srv != lastSrv)
        {
            D3D12_GPU_DESCRIPTOR_HANDLE h = g_gpu.heap->GetGPUDescriptorHandleForHeapStart();
            h.ptr += UINT64(t.srv) * g_gpu.srvStride;
            list->SetGraphicsRootDescriptorTable(1, h);
            lastSrv = t.srv;
        }
        if (uint32_t point = t.point; point != lastPoint)
        {
            list->SetGraphicsRoot32BitConstant(0, point, 4);
            lastPoint = point;
        }
        list->DrawIndexedInstanced(d.count, 1, d.firstIndex, d.baseVertex, 0);
    }
}
} // namespace glue2gpu
