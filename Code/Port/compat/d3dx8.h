/*
** d3dx8.h (compat)
**
** The DirectX 8 SDK's D3DX8 library isn't redistributable and isn't on modern systems.
** This is a from-scratch replacement for exactly the D3DX functions Renegade calls
** (implemented in Port/compat/d3dx8_compat.cpp):
**
**   D3DXCreateTexture            - create a texture, adjusting size/format to the device
**   D3DXLoadSurfaceFromSurface   - copy with format conversion and filtering (incl. DXT1-5)
**   D3DXFilterTexture            - build mip levels
**   D3DXGetFVFVertexSize, D3DXGetErrorStringA
**   D3DXMATRIX / D3DXVECTOR3 / D3DXVECTOR4, D3DXMatrixTranspose, D3DXVec3Transform
**   D3DXCreateTextureFromFileExA, D3DXLoadSurfaceFromFile - always fail: Renegade loads
**                                  its textures itself (TextureLoader), these are unused fallbacks
**
** D3dx8core.h, D3dx8tex.h and D3dx8math.h all include this file.
*/
#ifndef PORT_D3DX8_H
#define PORT_D3DX8_H

#include <d3d8.h>

#define D3DX_DEFAULT				((UINT)-1)

#define D3DX_FILTER_NONE			(1 << 0)
#define D3DX_FILTER_POINT			(2 << 0)
#define D3DX_FILTER_LINEAR		(3 << 0)
#define D3DX_FILTER_TRIANGLE		(4 << 0)
#define D3DX_FILTER_BOX			(5 << 0)
#define D3DX_FILTER_MIRROR_U		(1 << 16)
#define D3DX_FILTER_MIRROR_V		(2 << 16)
#define D3DX_FILTER_MIRROR		(7 << 16)
#define D3DX_FILTER_DITHER		(8 << 16)

typedef struct D3DXIMAGE_INFO
{
	UINT			Width;
	UINT			Height;
	UINT			Depth;
	UINT			MipLevels;
	D3DFORMAT	Format;
} D3DXIMAGE_INFO;

/*
** Math: only what sortingrenderer.cpp uses. Row-vector convention like D3DX:
** v' = v * M, and (A * B) applies A first.
*/
#ifdef __cplusplus
struct D3DXVECTOR3 : public D3DVECTOR
{
	D3DXVECTOR3() {}
	D3DXVECTOR3(FLOAT fx, FLOAT fy, FLOAT fz) { x = fx; y = fy; z = fz; }
	operator FLOAT * ()					{ return &x; }
	operator const FLOAT * () const	{ return &x; }
};

struct D3DXVECTOR4
{
	FLOAT x, y, z, w;
	D3DXVECTOR4() {}
	D3DXVECTOR4(FLOAT fx, FLOAT fy, FLOAT fz, FLOAT fw) : x(fx), y(fy), z(fz), w(fw) {}
	operator FLOAT * ()					{ return &x; }
	operator const FLOAT * () const	{ return &x; }
};

struct D3DXMATRIX : public D3DMATRIX
{
	D3DXMATRIX() {}
	FLOAT & operator () (UINT row, UINT col)				{ return m[row][col]; }
	FLOAT operator () (UINT row, UINT col) const			{ return m[row][col]; }
	D3DXMATRIX operator * (const D3DXMATRIX & b) const
	{
		D3DXMATRIX r;
		for (int i = 0; i < 4; i++) {
			for (int j = 0; j < 4; j++) {
				r.m[i][j] = m[i][0] * b.m[0][j] + m[i][1] * b.m[1][j] + m[i][2] * b.m[2][j] + m[i][3] * b.m[3][j];
			}
		}
		return r;
	}
};
#endif

#ifdef __cplusplus
extern "C" {
#endif

D3DXMATRIX *	WINAPI D3DXMatrixTranspose(D3DXMATRIX * out, const D3DXMATRIX * m);
D3DXVECTOR4 *	WINAPI D3DXVec3Transform(D3DXVECTOR4 * out, const D3DXVECTOR3 * v, const D3DXMATRIX * m);

UINT		WINAPI D3DXGetFVFVertexSize(DWORD fvf);
HRESULT	WINAPI D3DXGetErrorStringA(HRESULT hr, LPSTR buffer, UINT buffer_len);

HRESULT	WINAPI D3DXCreateTexture(IDirect3DDevice8 * device, UINT width, UINT height, UINT mip_levels,
										DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DTexture8 ** texture);

HRESULT	WINAPI D3DXLoadSurfaceFromSurface(IDirect3DSurface8 * dest, const PALETTEENTRY * dest_palette, const RECT * dest_rect,
										IDirect3DSurface8 * src, const PALETTEENTRY * src_palette, const RECT * src_rect,
										DWORD filter, D3DCOLOR color_key);

HRESULT	WINAPI D3DXFilterTexture(IDirect3DTexture8 * texture, const PALETTEENTRY * palette, UINT src_level, DWORD filter);

HRESULT	WINAPI D3DXCreateTextureFromFileExA(IDirect3DDevice8 * device, LPCSTR file, UINT width, UINT height, UINT mip_levels,
										DWORD usage, D3DFORMAT format, D3DPOOL pool, DWORD filter, DWORD mip_filter,
										D3DCOLOR color_key, D3DXIMAGE_INFO * src_info, PALETTEENTRY * palette,
										IDirect3DTexture8 ** texture);

HRESULT	WINAPI D3DXLoadSurfaceFromFileA(IDirect3DSurface8 * dest, const PALETTEENTRY * dest_palette, const RECT * dest_rect,
										LPCSTR file, const RECT * src_rect, DWORD filter, D3DCOLOR color_key,
										D3DXIMAGE_INFO * src_info);
#define D3DXLoadSurfaceFromFile		D3DXLoadSurfaceFromFileA
#define D3DXCreateTextureFromFileEx	D3DXCreateTextureFromFileExA

#ifdef __cplusplus
}
#endif

#endif
