#pragma once
#include <d3d9.h>
#include <vector>
#include <GWCA/GameContainers/GamePos.h>

struct IDirect3DDevice9;
struct IDirect3DVertexBuffer9;
typedef unsigned long DWORD;

#define D3DFVF_CUSTOMVERTEX D3DFVF_XYZ | D3DFVF_DIFFUSE

typedef GW::Vec2f D3DVec2f;

struct D3DVertex {
    float x;
    float y;
    float z;
    DWORD color;
    D3DVertex() = default;
    D3DVertex(float x, float y, float z, DWORD color);
    D3DVertex(float x, float y, DWORD color);
};

struct D3DTriangle {
    D3DVertex v[3];
};

template <size_t N>
struct D3DShape {
    D3DTriangle t[N];
};

struct D3DQuad : D3DShape<2> {
    D3DQuad(const D3DVec2f& tl, const D3DVec2f& br, DWORD color);
};

struct D3DLine : D3DShape<2> {
    D3DLine(const D3DVec2f& a, const D3DVec2f& b, float thickness, DWORD color);
};

struct D3DDiamond : D3DShape<2> {
    D3DDiamond(const D3DVec2f& pos, float radius, DWORD color);
};

struct D3DVelocityArrow : D3DShape<1> {
    bool valid = false;
    D3DVelocityArrow(const D3DVec2f& pos, const D3DVec2f& velocity, float length, float half_width, DWORD color);
    bool Valid() const { return valid; }
};
struct D3DTeardrop : D3DShape<8> {
    D3DTeardrop(const D3DVec2f& pos, float radius, float rotation, DWORD color_dark, DWORD color_light);
};

class D3DVertexBuffer {
public:
    virtual ~D3DVertexBuffer();
    virtual void Invalidate();
    virtual void Render(IDirect3DDevice9* device);
    virtual void Terminate();
    virtual void Initialize(IDirect3DDevice9* device); // no longer pure

    virtual void reserve(size_t n) { vertices.reserve(n); }
    virtual void clear()
    {
        vertices.clear();
        dirty = true;
    }
    bool empty() const { return vertices.empty(); }

protected:
    void UploadVertices(IDirect3DDevice9* device); // the memcpy logic

    IDirect3DVertexBuffer9* buffer = nullptr;
    size_t buffer_byte_size = 0;
    D3DPRIMITIVETYPE type = D3DPT_TRIANGLELIST;
    unsigned long count = 0;
    bool initialized = false;
    bool dirty = false;
    std::vector<D3DVertex> vertices;
};

class D3DTriangleBuffer : public D3DVertexBuffer {
public:
    template <size_t N>
    void push_back(const D3DShape<N>& shape)
    {
        for (const auto& tri : shape.t) {
            vertices.insert(vertices.end(), tri.v, tri.v + 3);
        }
        dirty = true;
    }
    void push_back(const D3DTriangle& tri)
    {
        vertices.insert(vertices.end(), tri.v, tri.v + 3);
        dirty = true;
    }
    void push_back(const D3DTriangleBuffer& other)
    {
        vertices.insert(vertices.end(), other.vertices.begin(), other.vertices.end());
        dirty = true;
    }

    void reserve(size_t n) override { vertices.reserve(n * 3); }

    void Initialize(IDirect3DDevice9* device) override;
};

struct D3DCircle : D3DTriangleBuffer {
    D3DCircle() = default;
    D3DCircle(const D3DVec2f& center, float radius, float thickness, DWORD color, int segment_count = 64);
};
class D3DFillCircle : public D3DVertexBuffer {
public:
    D3DFillCircle() = default;
    D3DFillCircle(const D3DVec2f& center, float radius, DWORD color, DWORD center_color, int segments = 64);

    void SetColor(DWORD c)
    {
        if (color == c) return;
        color = c;
        dirty = true;
    }
    void SetCenterColor(DWORD c)
    {
        if (center_color == c) return;
        center_color = c;
        dirty = true;
    }
    void SetRadius(float r)
    {
        if (radius == r) return;
        radius = r;
        dirty = true;
    }
    void SetSegments(int s)
    {
        if (segments == s) return;
        segments = s;
        dirty = true;
    }

    void Initialize(IDirect3DDevice9* device) override;

private:
    D3DVec2f center{0.f, 0.f};
    DWORD color = 0xFFFFFFFF;
    DWORD center_color = 0xFFFFFFFF;
    float radius = 1.f;
    int segments = 64;
};
class D3DLineCircle : public D3DVertexBuffer {
public:
    D3DLineCircle() = default;
    D3DLineCircle(float radius, DWORD color, int segments = 48);

    void SetColor(DWORD c)
    {
        if (color == c) return;
        color = c;
        dirty = true;
    }
    void SetRadius(float r)
    {
        if (radius == r) return;
        radius = r;
        dirty = true;
    }
    void SetSegments(int s)
    {
        if (segments == s) return;
        segments = s;
        dirty = true;
    }

    void Initialize(IDirect3DDevice9* device) override;

private:
    DWORD color = 0xFFFFFFFF;
    float radius = 1.f;
    int segments = 48;
};
