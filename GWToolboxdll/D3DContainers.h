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
    virtual void Initialize(IDirect3DDevice9* device) = 0;

protected:
    IDirect3DVertexBuffer9* buffer = nullptr;
    D3DPRIMITIVETYPE type = D3DPT_TRIANGLELIST;
    unsigned long count = 0;
    bool initialized = false;
};

class D3DTriangleBuffer : public D3DVertexBuffer {
public:
    std::vector<D3DTriangle> triangles;

    template <size_t N>
    void push_back(const D3DShape<N>& shape)
    {
        triangles.insert(triangles.end(), shape.t, shape.t + N);
        dirty = true;
    }
    void push_back(const D3DTriangle& shape)
    {
        triangles.push_back(shape);
        dirty = true;
    }
    void push_back(const D3DTriangleBuffer& other);

    bool empty() const { return triangles.empty(); }
    void reserve(size_t n) { triangles.reserve(n); }
    void clear();

    void Render(IDirect3DDevice9* device) override;
    void Initialize(IDirect3DDevice9* device) override;

private:
    bool dirty = false;
};

struct D3DCircle : D3DTriangleBuffer {
    D3DCircle() = default;
    D3DCircle(const D3DVec2f& center, float radius, float thickness, DWORD color, int segment_count = 64);
};
struct D3DFillCircle : D3DTriangleBuffer {
    D3DFillCircle() = default;
    D3DFillCircle(const D3DVec2f& center, float radius, DWORD color, DWORD center_color, int segment_count = 64);
};
