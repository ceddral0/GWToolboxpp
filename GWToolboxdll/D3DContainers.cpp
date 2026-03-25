#include "D3DContainers.h"
#include "stdafx.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// D3DVertex
D3DVertex::D3DVertex(const float x, const float y, const float z, const DWORD color) : x(x), y(y), z(z), color(color) {}

D3DVertex::D3DVertex(const float x, const float y, const DWORD color) : x(x), y(y), z(0.f), color(color) {}

// D3DQuad
D3DQuad::D3DQuad(const D3DVec2f& tl, const D3DVec2f& br, DWORD color)
{
    const D3DVertex TL{tl.x, tl.y, color};
    const D3DVertex TR{br.x, tl.y, color};
    const D3DVertex BR{br.x, br.y, color};
    const D3DVertex BL{tl.x, br.y, color};
    t[0] = {TL, TR, BR};
    t[1] = {TL, BR, BL};
}

// D3DLine
D3DLine::D3DLine(const D3DVec2f& a, const D3DVec2f& b, float thickness, DWORD color)
{
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float len = sqrtf(dx * dx + dy * dy);
    const float nx = (dy / len) * thickness;
    const float ny = (dx / len) * thickness;
    const D3DVertex TL{a.x + nx, a.y - ny, color};
    const D3DVertex TR{b.x + nx, b.y - ny, color};
    const D3DVertex BR{b.x - nx, b.y + ny, color};
    const D3DVertex BL{a.x - nx, a.y + ny, color};
    t[0] = {TL, TR, BR};
    t[1] = {TL, BR, BL};
}

// D3DDiamond
D3DDiamond::D3DDiamond(const D3DVec2f& pos, float radius, DWORD color)
{
    const D3DVertex top{pos.x, pos.y + radius, color};
    const D3DVertex right{pos.x + radius, pos.y, color};
    const D3DVertex bot{pos.x, pos.y - radius, color};
    const D3DVertex left{pos.x - radius, pos.y, color};
    t[0] = {top, right, bot};
    t[1] = {top, bot, left};
}

// D3DVelocityArrow
D3DVelocityArrow::D3DVelocityArrow(const D3DVec2f& pos, const D3DVec2f& velocity, float length, float half_width, DWORD color)
{
    const float vlen_sq = velocity.x * velocity.x + velocity.y * velocity.y;
    if (vlen_sq < 1.0f) return;
    const float vlen = sqrtf(vlen_sq);
    const float dx = velocity.x / vlen;
    const float dy = velocity.y / vlen;
    const float nx = -dy, ny = dx;
    const D3DVec2f tip{pos.x + dx * length, pos.y + dy * length};
    t[0] = {D3DVertex(tip.x, tip.y, color), D3DVertex(pos.x + nx * half_width, pos.y + ny * half_width, color), D3DVertex(pos.x - nx * half_width, pos.y - ny * half_width, color)};
    valid = true;
}

// D3DVertexBuffer
void D3DVertexBuffer::Invalidate()
{
    if (buffer) buffer->Release();
    buffer = nullptr;
    initialized = false;
}

void D3DVertexBuffer::Render(IDirect3DDevice9* device)
{
    if (!initialized) {
        initialized = true;
        Initialize(device);
    }
    if (!count) return;
    device->SetFVF(D3DFVF_CUSTOMVERTEX);
    device->SetStreamSource(0, buffer, 0, sizeof(D3DVertex));
    device->DrawPrimitive(type, 0, count);
}

void D3DVertexBuffer::Terminate()
{
    Invalidate();
}

// D3DTriangleBuffer
void D3DTriangleBuffer::push_back(const D3DTriangleBuffer& other)
{
    triangles.insert(triangles.end(), other.triangles.begin(), other.triangles.end());
    dirty = true;
}

void D3DTriangleBuffer::clear()
{
    triangles.clear();
    dirty = true;
}

void D3DTriangleBuffer::Render(IDirect3DDevice9* device)
{
    if (dirty) Invalidate();
    D3DVertexBuffer::Render(device);
}

void D3DTriangleBuffer::Initialize(IDirect3DDevice9* device)
{
    dirty = false;
    const size_t byte_size = triangles.size() * sizeof(D3DTriangle);
    if (!byte_size) return;
    if (FAILED(device->CreateVertexBuffer(byte_size, D3DUSAGE_WRITEONLY, D3DFVF_CUSTOMVERTEX, D3DPOOL_MANAGED, &buffer, nullptr))) return;
    void* ptr = nullptr;
    if (SUCCEEDED(buffer->Lock(0, byte_size, &ptr, 0))) {
        memcpy(ptr, triangles.data(), byte_size);
        buffer->Unlock();
    }
    type = D3DPT_TRIANGLELIST;
    count = triangles.size();
}

// D3DCircle
D3DCircle::D3DCircle(const D3DVec2f& center, float radius, float thickness, DWORD color, int segment_count)
{
    triangles.reserve(segment_count * 2);
    D3DVec2f prev = {center.x + radius, center.y};
    for (int i = 1; i <= segment_count; i++) {
        const float a = static_cast<float>(i) / segment_count * M_PI * 2;
        const D3DVec2f cur = {center.x + radius * cosf(a), center.y + radius * sinf(a)};
        push_back(D3DLine(prev, cur, thickness, color));
        prev = cur;
    }
}

D3DTeardrop::D3DTeardrop(const D3DVec2f& pos, float radius, float rotation, DWORD color_dark, DWORD color_light)
{
    // Unit positions from AgentRenderer, scaled by radius and rotated
    const auto vert = [&](float x, float y, DWORD color) -> D3DVertex {
        const float rx = x * radius;
        const float ry = y * radius;
        const float cos_r = cosf(rotation);
        const float sin_r = sinf(rotation);
        return {pos.x + rx * cos_r - ry * sin_r, pos.y + rx * sin_r + ry * cos_r, color};
    };

    const D3DVertex A = vert(1.8f, 0.0f, color_dark);
    const D3DVertex B = vert(0.7f, 0.7f, color_dark);
    const D3DVertex C = vert(0.0f, 1.0f, color_dark);
    const D3DVertex D = vert(-0.7f, 0.7f, color_dark);
    const D3DVertex E = vert(-1.0f, 0.0f, color_dark);
    const D3DVertex F = vert(-0.7f, -0.7f, color_dark);
    const D3DVertex G = vert(0.0f, -1.0f, color_dark);
    const D3DVertex H = vert(0.7f, -0.7f, color_dark);
    const D3DVertex O = vert(0.0f, 0.0f, color_light);

    t[0] = {A, B, O};
    t[1] = {B, C, O};
    t[2] = {C, D, O};
    t[3] = {D, E, O};
    t[4] = {E, F, O};
    t[5] = {F, G, O};
    t[6] = {G, H, O};
    t[7] = {H, A, O};
}
