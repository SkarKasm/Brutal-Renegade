/*
** d3dx8_compat.cpp - from-scratch replacements for the D3DX8 functions Renegade uses.
** See d3dx8.h for the list and the reasons.
**
** Surfaces are converted through a 32-bit ARGB image: decode the source rectangle
** (any W3D format, DXT1-5 included), resample it to the destination rectangle (point for
** D3DX_FILTER_NONE/POINT, area average otherwise), encode into the destination format.
*/
#include "d3dx8.h"
#include <stdio.h>
#include <string.h>
#include <vector>

typedef unsigned int	uint32;
typedef unsigned short	uint16;
typedef unsigned char	uint8;

/*
** Math
*/
extern "C" D3DXMATRIX * WINAPI D3DXMatrixTranspose(D3DXMATRIX * out, const D3DXMATRIX * m)
{
	D3DXMATRIX t;
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			t.m[i][j] = m->m[j][i];
		}
	}
	*out = t;
	return out;
}

extern "C" D3DXVECTOR4 * WINAPI D3DXVec3Transform(D3DXVECTOR4 * out, const D3DXVECTOR3 * v, const D3DXMATRIX * m)
{
	D3DXVECTOR4 r;
	r.x = v->x * m->m[0][0] + v->y * m->m[1][0] + v->z * m->m[2][0] + m->m[3][0];
	r.y = v->x * m->m[0][1] + v->y * m->m[1][1] + v->z * m->m[2][1] + m->m[3][1];
	r.z = v->x * m->m[0][2] + v->y * m->m[1][2] + v->z * m->m[2][2] + m->m[3][2];
	r.w = v->x * m->m[0][3] + v->y * m->m[1][3] + v->z * m->m[2][3] + m->m[3][3];
	*out = r;
	return out;
}

/*
** FVF size
*/
extern "C" UINT WINAPI D3DXGetFVFVertexSize(DWORD fvf)
{
	UINT size = 0;
	switch (fvf & 0x00E) {				// D3DFVF_POSITION_MASK
		case 0x002: size = 12; break;	// XYZ
		case 0x004: size = 16; break;	// XYZRHW
		case 0x006: size = 16; break;	// XYZB1
		case 0x008: size = 20; break;	// XYZB2
		case 0x00A: size = 24; break;	// XYZB3
		case 0x00C: size = 28; break;	// XYZB4
		case 0x00E: size = 32; break;	// XYZB5
	}
	if (fvf & 0x010) size += 12;		// NORMAL
	if (fvf & 0x020) size += 4;		// PSIZE
	if (fvf & 0x040) size += 4;		// DIFFUSE
	if (fvf & 0x080) size += 4;		// SPECULAR
	UINT tex_count = (fvf & 0xF00) >> 8;
	for (UINT t = 0; t < tex_count; t++) {
		switch ((fvf >> (16 + t * 2)) & 3) {
			case 0: size += 8; break;		// 2 floats (default)
			case 1: size += 12; break;		// 3
			case 2: size += 16; break;		// 4
			case 3: size += 4; break;		// 1
		}
	}
	return size;
}

/*
** Error strings
*/
extern "C" HRESULT WINAPI D3DXGetErrorStringA(HRESULT hr, LPSTR buffer, UINT buffer_len)
{
	if (buffer == NULL || buffer_len == 0) {
		return D3DERR_INVALIDCALL;
	}
	const char * text = NULL;
	switch (hr) {
		case D3D_OK:								text = "D3D_OK"; break;
		case D3DERR_WRONGTEXTUREFORMAT:		text = "D3DERR_WRONGTEXTUREFORMAT"; break;
		case D3DERR_UNSUPPORTEDCOLOROPERATION:	text = "D3DERR_UNSUPPORTEDCOLOROPERATION"; break;
		case D3DERR_UNSUPPORTEDCOLORARG:		text = "D3DERR_UNSUPPORTEDCOLORARG"; break;
		case D3DERR_UNSUPPORTEDALPHAOPERATION:	text = "D3DERR_UNSUPPORTEDALPHAOPERATION"; break;
		case D3DERR_UNSUPPORTEDALPHAARG:		text = "D3DERR_UNSUPPORTEDALPHAARG"; break;
		case D3DERR_TOOMANYOPERATIONS:		text = "D3DERR_TOOMANYOPERATIONS"; break;
		case D3DERR_CONFLICTINGTEXTUREFILTER:	text = "D3DERR_CONFLICTINGTEXTUREFILTER"; break;
		case D3DERR_UNSUPPORTEDFACTORVALUE:	text = "D3DERR_UNSUPPORTEDFACTORVALUE"; break;
		case D3DERR_CONFLICTINGRENDERSTATE:	text = "D3DERR_CONFLICTINGRENDERSTATE"; break;
		case D3DERR_UNSUPPORTEDTEXTUREFILTER:	text = "D3DERR_UNSUPPORTEDTEXTUREFILTER"; break;
		case D3DERR_CONFLICTINGTEXTUREPALETTE:	text = "D3DERR_CONFLICTINGTEXTUREPALETTE"; break;
		case D3DERR_DRIVERINTERNALERROR:		text = "D3DERR_DRIVERINTERNALERROR"; break;
		case D3DERR_NOTFOUND:					text = "D3DERR_NOTFOUND"; break;
		case D3DERR_MOREDATA:					text = "D3DERR_MOREDATA"; break;
		case D3DERR_DEVICELOST:					text = "D3DERR_DEVICELOST"; break;
		case D3DERR_DEVICENOTRESET:			text = "D3DERR_DEVICENOTRESET"; break;
		case D3DERR_NOTAVAILABLE:				text = "D3DERR_NOTAVAILABLE"; break;
		case D3DERR_OUTOFVIDEOMEMORY:			text = "D3DERR_OUTOFVIDEOMEMORY"; break;
		case D3DERR_INVALIDDEVICE:				text = "D3DERR_INVALIDDEVICE"; break;
		case D3DERR_INVALIDCALL:				text = "D3DERR_INVALIDCALL"; break;
		case D3DERR_DRIVERINVALIDCALL:		text = "D3DERR_DRIVERINVALIDCALL"; break;
		case E_OUTOFMEMORY:						text = "E_OUTOFMEMORY"; break;
	}
	if (text != NULL) {
		_snprintf(buffer, buffer_len, "%s", text);
	} else {
		_snprintf(buffer, buffer_len, "HRESULT 0x%08lx", (unsigned long)hr);
	}
	buffer[buffer_len - 1] = 0;
	return D3D_OK;
}

/*
** Formats
*/
static bool Is_DXT(D3DFORMAT f)
{
	return f == D3DFMT_DXT1 || f == D3DFMT_DXT2 || f == D3DFMT_DXT3 || f == D3DFMT_DXT4 || f == D3DFMT_DXT5;
}

static int Bytes_Per_Pixel(D3DFORMAT f)
{
	switch (f) {
		case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8:	return 4;
		case D3DFMT_R8G8B8:										return 3;
		case D3DFMT_R5G6B5: case D3DFMT_X1R5G5B5: case D3DFMT_A1R5G5B5: case D3DFMT_A4R4G4B4:
		case D3DFMT_A8R3G3B2: case D3DFMT_X4R4G4B4: case D3DFMT_A8P8: case D3DFMT_A8L8:
																		return 2;
		case D3DFMT_R3G3B2: case D3DFMT_A8: case D3DFMT_P8: case D3DFMT_L8: case D3DFMT_A4L4:
																		return 1;
		default:														return 0;
	}
}

static bool Has_Alpha(D3DFORMAT f)
{
	switch (f) {
		case D3DFMT_A8R8G8B8: case D3DFMT_A1R5G5B5: case D3DFMT_A4R4G4B4: case D3DFMT_A8:
		case D3DFMT_A8R3G3B2: case D3DFMT_A8P8: case D3DFMT_A8L8: case D3DFMT_A4L4:
		case D3DFMT_DXT1: case D3DFMT_DXT2: case D3DFMT_DXT3: case D3DFMT_DXT4: case D3DFMT_DXT5:
			return true;
		default:
			return false;
	}
}

static inline uint32 Expand(uint32 v, int bits)
{
	// replicate the top bits so 0 -> 0 and max -> 255
	if (bits >= 8) return v;
	if (bits == 0) return 0;
	uint32 r = v << (8 - bits);
	return r | (r >> bits) | (bits < 4 ? (r >> (2 * bits)) : 0);
}

static uint32 Palette_ARGB(const PALETTEENTRY * pal, uint32 index)
{
	if (pal == NULL) {
		return 0xFF000000 | (index << 16) | (index << 8) | index;	// no palette: grey ramp
	}
	const PALETTEENTRY & e = pal[index & 255];
	return ((uint32)e.peFlags << 24) | ((uint32)e.peRed << 16) | ((uint32)e.peGreen << 8) | e.peBlue;
}

static uint32 Decode_Pixel(D3DFORMAT f, const uint8 * p, const PALETTEENTRY * pal)
{
	uint32 v;
	switch (f) {
		case D3DFMT_A8R8G8B8:	return *(const uint32 *)p;
		case D3DFMT_X8R8G8B8:	return *(const uint32 *)p | 0xFF000000;
		case D3DFMT_R8G8B8:		return 0xFF000000 | ((uint32)p[2] << 16) | ((uint32)p[1] << 8) | p[0];
		case D3DFMT_R5G6B5:
			v = *(const uint16 *)p;
			return 0xFF000000 | (Expand((v >> 11) & 31, 5) << 16) | (Expand((v >> 5) & 63, 6) << 8) | Expand(v & 31, 5);
		case D3DFMT_X1R5G5B5:
		case D3DFMT_A1R5G5B5:
			v = *(const uint16 *)p;
			return ((f == D3DFMT_X1R5G5B5 || (v & 0x8000)) ? 0xFF000000 : 0) |
				(Expand((v >> 10) & 31, 5) << 16) | (Expand((v >> 5) & 31, 5) << 8) | Expand(v & 31, 5);
		case D3DFMT_A4R4G4B4:
		case D3DFMT_X4R4G4B4:
			v = *(const uint16 *)p;
			return ((f == D3DFMT_X4R4G4B4) ? 0xFF000000 : (Expand((v >> 12) & 15, 4) << 24)) |
				(Expand((v >> 8) & 15, 4) << 16) | (Expand((v >> 4) & 15, 4) << 8) | Expand(v & 15, 4);
		case D3DFMT_R3G3B2:
			v = *p;
			return 0xFF000000 | (Expand((v >> 5) & 7, 3) << 16) | (Expand((v >> 2) & 7, 3) << 8) | Expand(v & 3, 2);
		case D3DFMT_A8R3G3B2:
			v = *(const uint16 *)p;
			return ((v >> 8) << 24) | (Expand((v >> 5) & 7, 3) << 16) | (Expand((v >> 2) & 7, 3) << 8) | Expand(v & 3, 2);
		case D3DFMT_A8:			return ((uint32)p[0] << 24) | 0x00FFFFFF;
		case D3DFMT_L8:			return 0xFF000000 | ((uint32)p[0] * 0x010101);
		case D3DFMT_A8L8:		return ((uint32)p[1] << 24) | ((uint32)p[0] * 0x010101);
		case D3DFMT_A4L4:		return (Expand(p[0] >> 4, 4) << 24) | (Expand(p[0] & 15, 4) * 0x010101);
		case D3DFMT_P8:			return Palette_ARGB(pal, p[0]);
		case D3DFMT_A8P8:		return ((uint32)p[1] << 24) | (Palette_ARGB(pal, p[0]) & 0x00FFFFFF);
		default:					return 0xFF000000;
	}
}

static void Encode_Pixel(D3DFORMAT f, uint8 * p, uint32 c)
{
	uint32 a = c >> 24, r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
	switch (f) {
		case D3DFMT_A8R8G8B8:	*(uint32 *)p = c; break;
		case D3DFMT_X8R8G8B8:	*(uint32 *)p = c | 0xFF000000; break;
		case D3DFMT_R8G8B8:		p[0] = (uint8)b; p[1] = (uint8)g; p[2] = (uint8)r; break;
		case D3DFMT_R5G6B5:		*(uint16 *)p = (uint16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); break;
		case D3DFMT_X1R5G5B5:	*(uint16 *)p = (uint16)(0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)); break;
		case D3DFMT_A1R5G5B5:	*(uint16 *)p = (uint16)(((a >= 128) ? 0x8000 : 0) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)); break;
		case D3DFMT_A4R4G4B4:	*(uint16 *)p = (uint16)(((a >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)); break;
		case D3DFMT_X4R4G4B4:	*(uint16 *)p = (uint16)(0xF000 | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)); break;
		case D3DFMT_R3G3B2:		*p = (uint8)(((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6)); break;
		case D3DFMT_A8R3G3B2:	*(uint16 *)p = (uint16)((a << 8) | ((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6)); break;
		case D3DFMT_A8:			*p = (uint8)a; break;
		case D3DFMT_L8:			*p = (uint8)((r * 77 + g * 150 + b * 29) >> 8); break;
		case D3DFMT_A8L8:		p[0] = (uint8)((r * 77 + g * 150 + b * 29) >> 8); p[1] = (uint8)a; break;
		case D3DFMT_A4L4:		*p = (uint8)(((a >> 4) << 4) | (((r * 77 + g * 150 + b * 29) >> 8) >> 4)); break;
		default:					break;		// paletted destinations aren't supported (Renegade rejects P8 textures)
	}
}

/*
** DXT
*/
static uint32 From565(uint16 v)
{
	return 0xFF000000 | (Expand((v >> 11) & 31, 5) << 16) | (Expand((v >> 5) & 63, 6) << 8) | Expand(v & 31, 5);
}

static uint16 To565(uint32 c)
{
	return (uint16)(((((c >> 16) & 255) >> 3) << 11) | ((((c >> 8) & 255) >> 2) << 5) | ((c & 255) >> 3));
}

static uint32 Mix(uint32 a, uint32 b, int wa, int wb, int div)
{
	uint32 r = ((((a >> 16) & 255) * wa + ((b >> 16) & 255) * wb) / div);
	uint32 g = ((((a >> 8) & 255) * wa + ((b >> 8) & 255) * wb) / div);
	uint32 bl = (((a & 255) * wa + (b & 255) * wb) / div);
	return 0xFF000000 | (r << 16) | (g << 8) | bl;
}

static void Decode_Color_Block(const uint8 * blk, uint32 out[16], bool dxt1)
{
	uint16 c0 = *(const uint16 *)blk, c1 = *(const uint16 *)(blk + 2);
	uint32 col[4];
	col[0] = From565(c0);
	col[1] = From565(c1);
	if (c0 > c1 || !dxt1) {
		col[2] = Mix(col[0], col[1], 2, 1, 3);
		col[3] = Mix(col[0], col[1], 1, 2, 3);
	} else {
		col[2] = Mix(col[0], col[1], 1, 1, 2);
		col[3] = 0;						// transparent black
	}
	uint32 bits = *(const uint32 *)(blk + 4);
	for (int i = 0; i < 16; i++) {
		out[i] = col[(bits >> (2 * i)) & 3];
	}
}

static void Decode_DXT_Block(D3DFORMAT f, const uint8 * blk, uint32 out[16])
{
	if (f == D3DFMT_DXT1) {
		Decode_Color_Block(blk, out, true);
		return;
	}
	Decode_Color_Block(blk + 8, out, false);
	if (f == D3DFMT_DXT2 || f == D3DFMT_DXT3) {
		for (int i = 0; i < 16; i++) {
			uint32 a = (blk[i / 2] >> ((i & 1) * 4)) & 15;
			out[i] = (out[i] & 0x00FFFFFF) | (Expand(a, 4) << 24);
		}
	} else {
		uint32 a[8];
		a[0] = blk[0];
		a[1] = blk[1];
		if (a[0] > a[1]) {
			for (int k = 1; k < 7; k++) a[k + 1] = (a[0] * (7 - k) + a[1] * k) / 7;
		} else {
			for (int k = 1; k < 5; k++) a[k + 1] = (a[0] * (5 - k) + a[1] * k) / 5;
			a[6] = 0;
			a[7] = 255;
		}
		unsigned __int64 bits = 0;
		for (int b = 0; b < 6; b++) bits |= (unsigned __int64)blk[2 + b] << (8 * b);
		for (int i = 0; i < 16; i++) {
			out[i] = (out[i] & 0x00FFFFFF) | (a[(bits >> (3 * i)) & 7] << 24);
		}
	}
}

static int Color_Distance(uint32 a, uint32 b)
{
	int dr = (int)((a >> 16) & 255) - (int)((b >> 16) & 255);
	int dg = (int)((a >> 8) & 255) - (int)((b >> 8) & 255);
	int db = (int)(a & 255) - (int)(b & 255);
	return dr * dr * 3 + dg * dg * 4 + db * db * 2;
}

static void Encode_Color_Block(const uint32 in[16], uint8 * blk, bool dxt1_alpha)
{
	// endpoints: the darkest and brightest pixels (by luminance) of the opaque ones
	int lo = -1, hi = -1, lo_l = 1 << 30, hi_l = -1;
	for (int i = 0; i < 16; i++) {
		if (dxt1_alpha && (in[i] >> 24) < 128) continue;
		int l = (int)((in[i] >> 16) & 255) * 77 + (int)((in[i] >> 8) & 255) * 150 + (int)(in[i] & 255) * 29;
		if (l < lo_l) { lo_l = l; lo = i; }
		if (l > hi_l) { hi_l = l; hi = i; }
	}
	bool any_transparent = false;
	if (dxt1_alpha) {
		for (int i = 0; i < 16; i++) if ((in[i] >> 24) < 128) any_transparent = true;
	}
	uint16 c_hi = (hi >= 0) ? To565(in[hi]) : 0;
	uint16 c_lo = (lo >= 0) ? To565(in[lo]) : 0;

	uint16 c0, c1;
	uint32 col[4];
	if (any_transparent) {
		// three-colour mode: c0 <= c1, index 3 is transparent
		c0 = (c_lo < c_hi) ? c_lo : c_hi;
		c1 = (c_lo < c_hi) ? c_hi : c_lo;
		col[0] = From565(c0); col[1] = From565(c1); col[2] = Mix(col[0], col[1], 1, 1, 2); col[3] = 0;
	} else {
		c0 = (c_hi > c_lo) ? c_hi : c_lo;
		c1 = (c_hi > c_lo) ? c_lo : c_hi;
		col[0] = From565(c0); col[1] = From565(c1);
		col[2] = Mix(col[0], col[1], 2, 1, 3); col[3] = Mix(col[0], col[1], 1, 2, 3);
	}
	uint32 bits = 0;
	for (int i = 0; i < 16; i++) {
		int best = 0;
		if (any_transparent && (in[i] >> 24) < 128) {
			best = 3;
		} else if (c0 != c1) {
			int best_d = 1 << 30;
			int count = any_transparent ? 3 : 4;
			for (int k = 0; k < count; k++) {
				int d = Color_Distance(in[i], col[k]);
				if (d < best_d) { best_d = d; best = k; }
			}
		}
		bits |= (uint32)best << (2 * i);
	}
	*(uint16 *)blk = c0;
	*(uint16 *)(blk + 2) = c1;
	*(uint32 *)(blk + 4) = bits;
}

static void Encode_DXT_Block(D3DFORMAT f, const uint32 in[16], uint8 * blk)
{
	if (f == D3DFMT_DXT1) {
		Encode_Color_Block(in, blk, true);
		return;
	}
	if (f == D3DFMT_DXT2 || f == D3DFMT_DXT3) {
		memset(blk, 0, 8);
		for (int i = 0; i < 16; i++) {
			blk[i / 2] |= (uint8)(((in[i] >> 24) >> 4) << ((i & 1) * 4));
		}
	} else {
		uint32 amin = 255, amax = 0;
		for (int i = 0; i < 16; i++) {
			uint32 a = in[i] >> 24;
			if (a < amin) amin = a;
			if (a > amax) amax = a;
		}
		uint32 a[8];
		a[0] = amax;
		a[1] = amin;
		for (int k = 1; k < 7; k++) a[k + 1] = (a[0] * (7 - k) + a[1] * k) / 7;
		unsigned __int64 bits = 0;
		for (int i = 0; i < 16; i++) {
			int best = 0;
			if (amax != amin) {
				int best_d = 1 << 30;
				for (int k = 0; k < 8; k++) {
					int d = (int)(in[i] >> 24) - (int)a[k];
					if (d < 0) d = -d;
					if (d < best_d) { best_d = d; best = k; }
				}
			}
			bits |= (unsigned __int64)best << (3 * i);
		}
		blk[0] = (uint8)amax;
		blk[1] = (uint8)amin;
		for (int b = 0; b < 6; b++) blk[2 + b] = (uint8)(bits >> (8 * b));
	}
	Encode_Color_Block(in, blk + 8, false);
}

/*
** Whole surfaces <-> ARGB images
*/
struct Image
{
	int						Width;
	int						Height;
	std::vector<uint32>	Pixels;
	uint32 & At(int x, int y) { return Pixels[(size_t)y * Width + x]; }
};

static HRESULT Read_Surface(IDirect3DSurface8 * surface, const RECT & r, const PALETTEENTRY * pal, Image & img)
{
	D3DSURFACE_DESC desc;
	surface->GetDesc(&desc);
	img.Width = r.right - r.left;
	img.Height = r.bottom - r.top;
	img.Pixels.assign((size_t)img.Width * img.Height, 0);

	D3DLOCKED_RECT lr;
	HRESULT hr = surface->LockRect(&lr, NULL, D3DLOCK_READONLY);
	if (FAILED(hr)) {
		return hr;
	}
	const uint8 * bits = (const uint8 *)lr.pBits;
	if (Is_DXT(desc.Format)) {
		int block_bytes = (desc.Format == D3DFMT_DXT1) ? 8 : 16;
		uint32 block[16];
		for (int by = r.top / 4; by * 4 < r.bottom; by++) {
			for (int bx = r.left / 4; bx * 4 < r.right; bx++) {
				Decode_DXT_Block(desc.Format, bits + by * lr.Pitch + bx * block_bytes, block);
				for (int i = 0; i < 16; i++) {
					int x = bx * 4 + (i & 3) - r.left, y = by * 4 + (i >> 2) - r.top;
					if (x >= 0 && y >= 0 && x < img.Width && y < img.Height) img.At(x, y) = block[i];
				}
			}
		}
	} else {
		int bpp = Bytes_Per_Pixel(desc.Format);
		for (int y = 0; y < img.Height; y++) {
			const uint8 * row = bits + (y + r.top) * lr.Pitch + r.left * bpp;
			for (int x = 0; x < img.Width; x++) {
				img.At(x, y) = Decode_Pixel(desc.Format, row + x * bpp, pal);
			}
		}
	}
	surface->UnlockRect();
	return D3D_OK;
}

static HRESULT Write_Surface(IDirect3DSurface8 * surface, const RECT & r, Image & img)
{
	D3DSURFACE_DESC desc;
	surface->GetDesc(&desc);
	D3DLOCKED_RECT lr;
	HRESULT hr = surface->LockRect(&lr, NULL, 0);
	if (FAILED(hr)) {
		return hr;
	}
	uint8 * bits = (uint8 *)lr.pBits;
	if (Is_DXT(desc.Format)) {
		int block_bytes = (desc.Format == D3DFMT_DXT1) ? 8 : 16;
		uint32 block[16];
		for (int by = r.top / 4; by * 4 < r.bottom; by++) {
			for (int bx = r.left / 4; bx * 4 < r.right; bx++) {
				for (int i = 0; i < 16; i++) {
					// edge blocks repeat the nearest pixel inside the rectangle
					int x = bx * 4 + (i & 3) - r.left, y = by * 4 + (i >> 2) - r.top;
					if (x < 0) x = 0;
					if (y < 0) y = 0;
					if (x >= img.Width) x = img.Width - 1;
					if (y >= img.Height) y = img.Height - 1;
					block[i] = img.At(x, y);
				}
				Encode_DXT_Block(desc.Format, block, bits + by * lr.Pitch + bx * block_bytes);
			}
		}
	} else {
		int bpp = Bytes_Per_Pixel(desc.Format);
		for (int y = 0; y < img.Height; y++) {
			uint8 * row = bits + (y + r.top) * lr.Pitch + r.left * bpp;
			for (int x = 0; x < img.Width; x++) {
				Encode_Pixel(desc.Format, row + x * bpp, img.At(x, y));
			}
		}
	}
	surface->UnlockRect();
	return D3D_OK;
}

static void Resample(Image & src, Image & dst, DWORD filter)
{
	DWORD kind = filter & 0xFFFF;
	bool point = (kind == D3DX_FILTER_NONE || kind == D3DX_FILTER_POINT);
	for (int y = 0; y < dst.Height; y++) {
		float sy0 = (float)y * src.Height / dst.Height, sy1 = (float)(y + 1) * src.Height / dst.Height;
		for (int x = 0; x < dst.Width; x++) {
			float sx0 = (float)x * src.Width / dst.Width, sx1 = (float)(x + 1) * src.Width / dst.Width;
			if (point || (sx1 - sx0 <= 1.0f && sy1 - sy0 <= 1.0f)) {
				int px = (int)((sx0 + sx1) * 0.5f), py = (int)((sy0 + sy1) * 0.5f);
				if (px >= src.Width) px = src.Width - 1;
				if (py >= src.Height) py = src.Height - 1;
				dst.At(x, y) = src.At(px, py);
				continue;
			}
			// area average over the covered source pixels
			int x0 = (int)sx0, x1 = (int)(sx1 + 0.999f), y0 = (int)sy0, y1 = (int)(sy1 + 0.999f);
			if (x1 > src.Width) x1 = src.Width;
			if (y1 > src.Height) y1 = src.Height;
			uint32 sa = 0, sr = 0, sg = 0, sb = 0, n = 0;
			for (int yy = y0; yy < y1; yy++) {
				for (int xx = x0; xx < x1; xx++) {
					uint32 c = src.At(xx, yy);
					sa += c >> 24; sr += (c >> 16) & 255; sg += (c >> 8) & 255; sb += c & 255; n++;
				}
			}
			if (n == 0) n = 1;
			dst.At(x, y) = ((sa / n) << 24) | ((sr / n) << 16) | ((sg / n) << 8) | (sb / n);
		}
	}
}

extern "C" HRESULT WINAPI D3DXLoadSurfaceFromSurface(IDirect3DSurface8 * dest, const PALETTEENTRY * dest_palette, const RECT * dest_rect,
	IDirect3DSurface8 * src, const PALETTEENTRY * src_palette, const RECT * src_rect, DWORD filter, D3DCOLOR color_key)
{
	if (dest == NULL || src == NULL) {
		return D3DERR_INVALIDCALL;
	}
	D3DSURFACE_DESC sd, dd;
	src->GetDesc(&sd);
	dest->GetDesc(&dd);
	RECT sr = { 0, 0, (LONG)sd.Width, (LONG)sd.Height };
	RECT dr = { 0, 0, (LONG)dd.Width, (LONG)dd.Height };
	if (src_rect != NULL) sr = *src_rect;
	if (dest_rect != NULL) dr = *dest_rect;
	if (sr.right <= sr.left || sr.bottom <= sr.top || dr.right <= dr.left || dr.bottom <= dr.top) {
		return D3DERR_INVALIDCALL;
	}

	// Same format and size: straight copy (keeps DXT data lossless)
	if (sd.Format == dd.Format && sr.right - sr.left == dr.right - dr.left && sr.bottom - sr.top == dr.bottom - dr.top &&
		(Is_DXT(sd.Format) || Bytes_Per_Pixel(sd.Format) > 0) && color_key == 0) {
		D3DLOCKED_RECT sl, dl;
		if (FAILED(src->LockRect(&sl, NULL, D3DLOCK_READONLY))) return D3DERR_INVALIDCALL;
		if (FAILED(dest->LockRect(&dl, NULL, 0))) { src->UnlockRect(); return D3DERR_INVALIDCALL; }
		if (Is_DXT(sd.Format)) {
			int block_bytes = (sd.Format == D3DFMT_DXT1) ? 8 : 16;
			int bw = (sr.right - sr.left + 3) / 4, bh = (sr.bottom - sr.top + 3) / 4;
			for (int y = 0; y < bh; y++) {
				memcpy((uint8 *)dl.pBits + (dr.top / 4 + y) * dl.Pitch + (dr.left / 4) * block_bytes,
					(const uint8 *)sl.pBits + (sr.top / 4 + y) * sl.Pitch + (sr.left / 4) * block_bytes, bw * block_bytes);
			}
		} else {
			int bpp = Bytes_Per_Pixel(sd.Format);
			for (int y = 0; y < sr.bottom - sr.top; y++) {
				memcpy((uint8 *)dl.pBits + (dr.top + y) * dl.Pitch + dr.left * bpp,
					(const uint8 *)sl.pBits + (sr.top + y) * sl.Pitch + sr.left * bpp, (sr.right - sr.left) * bpp);
			}
		}
		dest->UnlockRect();
		src->UnlockRect();
		return D3D_OK;
	}

	Image si, di;
	HRESULT hr = Read_Surface(src, sr, src_palette, si);
	if (FAILED(hr)) {
		return hr;
	}
	if (color_key != 0) {
		for (size_t i = 0; i < si.Pixels.size(); i++) {
			if (si.Pixels[i] == color_key) si.Pixels[i] = 0;
		}
	}
	di.Width = dr.right - dr.left;
	di.Height = dr.bottom - dr.top;
	di.Pixels.assign((size_t)di.Width * di.Height, 0);
	Resample(si, di, (filter == D3DX_DEFAULT) ? D3DX_FILTER_BOX : filter);
	return Write_Surface(dest, dr, di);
}

extern "C" HRESULT WINAPI D3DXFilterTexture(IDirect3DTexture8 * texture, const PALETTEENTRY * palette, UINT src_level, DWORD filter)
{
	if (texture == NULL) {
		return D3DERR_INVALIDCALL;
	}
	if (src_level == D3DX_DEFAULT) {
		src_level = 0;
	}
	DWORD levels = texture->GetLevelCount();
	for (DWORD level = src_level + 1; level < levels; level++) {
		IDirect3DSurface8 * s = NULL;
		IDirect3DSurface8 * d = NULL;
		texture->GetSurfaceLevel(level - 1, &s);
		texture->GetSurfaceLevel(level, &d);
		HRESULT hr = (s && d) ? D3DXLoadSurfaceFromSurface(d, palette, NULL, s, palette, NULL,
			(filter == D3DX_DEFAULT) ? D3DX_FILTER_BOX : filter, 0) : D3DERR_INVALIDCALL;
		if (s) s->Release();
		if (d) d->Release();
		if (FAILED(hr)) {
			return hr;
		}
	}
	return D3D_OK;
}

/*
** Texture creation with D3DX's adjustments: power-of-two/square sizes when the card needs
** them, size limits, full mip chains, and the closest supported format.
*/
static bool Format_Supported(IDirect3DDevice8 * device, DWORD usage, D3DFORMAT format)
{
	IDirect3D8 * d3d = NULL;
	if (FAILED(device->GetDirect3D(&d3d)) || d3d == NULL) {
		return true;
	}
	D3DDEVICE_CREATION_PARAMETERS cp;
	D3DDISPLAYMODE mode;
	device->GetCreationParameters(&cp);
	device->GetDisplayMode(&mode);
	HRESULT hr = d3d->CheckDeviceFormat(cp.AdapterOrdinal, cp.DeviceType, mode.Format, usage, D3DRTYPE_TEXTURE, format);
	d3d->Release();
	return SUCCEEDED(hr);
}

static D3DFORMAT Closest_Format(IDirect3DDevice8 * device, DWORD usage, D3DFORMAT format)
{
	if (format == D3DFMT_UNKNOWN) {
		format = D3DFMT_A8R8G8B8;
	}
	if (Format_Supported(device, usage, format)) {
		return format;
	}
	static const D3DFORMAT with_alpha[] = { D3DFMT_A8R8G8B8, D3DFMT_A4R4G4B4, D3DFMT_A1R5G5B5, D3DFMT_X8R8G8B8, D3DFMT_R5G6B5 };
	static const D3DFORMAT without_alpha[] = { D3DFMT_X8R8G8B8, D3DFMT_R5G6B5, D3DFMT_X1R5G5B5, D3DFMT_A8R8G8B8, D3DFMT_A1R5G5B5 };
	const D3DFORMAT * list = Has_Alpha(format) ? with_alpha : without_alpha;
	for (int i = 0; i < 5; i++) {
		if (Format_Supported(device, usage, list[i])) {
			return list[i];
		}
	}
	return format;
}

static UINT Next_Pow2(UINT v)
{
	UINT p = 1;
	while (p < v) p <<= 1;
	return p;
}

extern "C" HRESULT WINAPI D3DXCreateTexture(IDirect3DDevice8 * device, UINT width, UINT height, UINT mip_levels,
	DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DTexture8 ** texture)
{
	if (device == NULL || texture == NULL) {
		return D3DERR_INVALIDCALL;
	}
	D3DCAPS8 caps;
	device->GetDeviceCaps(&caps);

	if (width == 0 || width == D3DX_DEFAULT) width = 256;
	if (height == 0 || height == D3DX_DEFAULT) height = 256;
	if (caps.TextureCaps & D3DPTEXTURECAPS_POW2) {
		width = Next_Pow2(width);
		height = Next_Pow2(height);
	}
	if (caps.TextureCaps & D3DPTEXTURECAPS_SQUAREONLY) {
		width = height = (width > height) ? width : height;
	}
	if (caps.MaxTextureWidth != 0 && width > caps.MaxTextureWidth) width = caps.MaxTextureWidth;
	if (caps.MaxTextureHeight != 0 && height > caps.MaxTextureHeight) height = caps.MaxTextureHeight;

	format = Closest_Format(device, usage, format);
	if (Is_DXT(format)) {
		// DXT needs whole 4x4 blocks
		if (width < 4) width = 4;
		if (height < 4) height = 4;
	}

	UINT max_levels = 1;
	for (UINT s = (width > height) ? width : height; s > 1; s >>= 1) max_levels++;
	if (mip_levels == 0 || mip_levels == D3DX_DEFAULT || mip_levels > max_levels) {
		mip_levels = max_levels;
	}
	return device->CreateTexture(width, height, mip_levels, usage, format, pool, texture);
}

/*
** File loaders: Renegade decodes its own files (TextureLoader); these are never reached in
** normal play and simply report failure (callers fall back to the missing texture).
*/
extern "C" HRESULT WINAPI D3DXCreateTextureFromFileExA(IDirect3DDevice8 *, LPCSTR, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL,
	DWORD, DWORD, D3DCOLOR, D3DXIMAGE_INFO *, PALETTEENTRY *, IDirect3DTexture8 ** texture)
{
	if (texture != NULL) *texture = NULL;
	return D3DERR_NOTAVAILABLE;
}

extern "C" HRESULT WINAPI D3DXLoadSurfaceFromFileA(IDirect3DSurface8 *, const PALETTEENTRY *, const RECT *, LPCSTR,
	const RECT *, DWORD, D3DCOLOR, D3DXIMAGE_INFO *)
{
	return D3DERR_NOTAVAILABLE;
}
