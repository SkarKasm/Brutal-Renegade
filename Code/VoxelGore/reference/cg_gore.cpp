/*
===========================================================================

RealRTCW-VR
Copyright (C) 2026 RealRTCW-VR contributors

This file is part of RealRTCW-VR, a VR port of RealRTCW.

RealRTCW-VR is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

===========================================================================
*/

// cg_gore.cpp -- voxel gore: bullets carve each body's own voxel copy of itself (gore/gorecore.cpp)
//
// Each skeletal model is read from its .mds file once, put in one pose (the rest pose) and
// filled with voxels, its bones inside it as rods. A body that gets shot takes a copy of
// that. A shot is taken into the rest pose through the bone it hit nearest and carves a
// tube there (out the other side if it has the energy), so wounds stay put on the body
// whatever it does afterwards. From the voxels come what the renderer draws (vr_gore.h):
// the skin with the holes left out, and the wound walls (the flesh), each vertex moving
// with its own bone. A part shot clean off (once the body is dead) becomes a body of its own.
//
// gorecore works in metres, everything here in the model's units (inches): METRES converts.
// Like cg_physics.cpp, a plain C interface (cg_gore.h) and nothing of the cgame's.

#include "cg_gore.h"
#include "../gore/gorecore.h"

#include <vector>
#include <algorithm>
#include <array>
#include <memory>
#include <string.h>
#include <math.h>

using gore::V3;

namespace {

const float METRES = 1.0f;       // a model unit (an inch) in metres
const int MAX_MODELS = 32;
const int MAX_BODIES = 128;         // wounded skins and detached pieces
const int MAX_BONES = 128;

// --------------------------------------------------------------------------- the .mds file

#pragma pack(push, 4)
struct MdsHeader { int ident, version; char name[64]; float lodScale, lodBias; int numFrames, numBones, ofsFrames, ofsBones, torsoParent, numSurfaces, ofsSurfaces, numTags, ofsTags, ofsEnd; };
struct MdsSurface { int ident; char name[64], shader[64]; int shaderIndex, minLod, ofsHeader, numVerts, ofsVerts, numTriangles, ofsTriangles, ofsCollapseMap, numBoneReferences, ofsBoneReferences, ofsEnd; };
struct MdsWeight { int boneIndex; float boneWeight; float offset[3]; };
struct MdsVertex { float normal[3]; float texCoords[2]; int numWeights, fixedParent; float fixedDist; MdsWeight weights[1]; };
// heads: .md3, or .mdc (md3 with frames stored as small offsets from a few full ones)
struct Md3Header { int ident, version; char name[64]; int flags, numFrames, numTags, numSurfaces, numSkins, ofsFrames, ofsTags, ofsSurfaces, ofsEnd; };
struct Md3Surface { int ident; char name[64]; int flags, numFrames, numShaders, numVerts, numTriangles, ofsTriangles, ofsShaders, ofsSt, ofsXyzNormals, ofsEnd; };
struct MdcHeader { int ident, version; char name[64]; int flags, numFrames, numTags, numSurfaces, numSkins, ofsFrames, ofsTagNames, ofsTags, ofsSurfaces, ofsEnd; };
struct MdcSurface { int ident; char name[64]; int flags, numCompFrames, numBaseFrames, numShaders, numVerts, numTriangles, ofsTriangles, ofsShaders, ofsSt, ofsXyzNormals, ofsXyzCompressed, ofsFrameBaseFrames, ofsFrameCompFrames, ofsEnd; };
struct Md3XyzNormal { short xyz[3]; short normal; };
#pragma pack(pop)

// --------------------------------------------------------------------------- poses

struct Xf { float m[9]; float t[3]; };          // p = m p' + t (m by rows)

Xf PoseBone( const float *pose, int b ) {
	Xf x;
	memcpy( x.m, pose + b * 12, sizeof( x.m ) );
	memcpy( x.t, pose + b * 12 + 9, sizeof( x.t ) );
	return x;
}

Xf Inverse( const Xf &a ) {
	const float *m = a.m;
	float det = m[0] * ( m[4] * m[8] - m[5] * m[7] ) - m[1] * ( m[3] * m[8] - m[5] * m[6] ) + m[2] * ( m[3] * m[7] - m[4] * m[6] );
	float id = fabsf( det ) > 1e-12f ? 1.0f / det : 0.0f;
	Xf r;
	r.m[0] = ( m[4] * m[8] - m[5] * m[7] ) * id;
	r.m[1] = ( m[2] * m[7] - m[1] * m[8] ) * id;
	r.m[2] = ( m[1] * m[5] - m[2] * m[4] ) * id;
	r.m[3] = ( m[5] * m[6] - m[3] * m[8] ) * id;
	r.m[4] = ( m[0] * m[8] - m[2] * m[6] ) * id;
	r.m[5] = ( m[2] * m[3] - m[0] * m[5] ) * id;
	r.m[6] = ( m[3] * m[7] - m[4] * m[6] ) * id;
	r.m[7] = ( m[1] * m[6] - m[0] * m[7] ) * id;
	r.m[8] = ( m[0] * m[4] - m[1] * m[3] ) * id;
	for ( int i = 0; i < 3; i++ ) {
		r.t[i] = -( r.m[i * 3] * a.t[0] + r.m[i * 3 + 1] * a.t[1] + r.m[i * 3 + 2] * a.t[2] );
	}
	return r;
}

Xf Mul( const Xf &a, const Xf &b ) {        // a after b
	Xf r;
	for ( int i = 0; i < 3; i++ ) {
		for ( int j = 0; j < 3; j++ ) {
			r.m[i * 3 + j] = a.m[i * 3] * b.m[j] + a.m[i * 3 + 1] * b.m[3 + j] + a.m[i * 3 + 2] * b.m[6 + j];
		}
		r.t[i] = a.m[i * 3] * b.t[0] + a.m[i * 3 + 1] * b.t[1] + a.m[i * 3 + 2] * b.t[2] + a.t[i];
	}
	return r;
}

V3 Point( const Xf &x, V3 p ) {
	return { x.m[0] * p.x + x.m[1] * p.y + x.m[2] * p.z + x.t[0],
			 x.m[3] * p.x + x.m[4] * p.y + x.m[5] * p.z + x.t[1],
			 x.m[6] * p.x + x.m[7] * p.y + x.m[8] * p.z + x.t[2] };
}

V3 Dir( const Xf &x, V3 p ) {
	return { x.m[0] * p.x + x.m[1] * p.y + x.m[2] * p.z,
			 x.m[3] * p.x + x.m[4] * p.y + x.m[5] * p.z,
			 x.m[6] * p.x + x.m[7] * p.y + x.m[8] * p.z };
}

V3 ToV3( const float *f ) { return { f[0], f[1], f[2] }; }

float SegDist( V3 p, V3 a, V3 b, float *tOut = nullptr ) {
	V3 ab = b - a;
	float l2 = gore::dot( ab, ab );
	float t = l2 > 1e-12f ? gore::dot( p - a, ab ) / l2 : 0.0f;
	t = t < 0 ? 0 : ( t > 1 ? 1 : t );
	if ( tOut ) {
		*tOut = t;
	}
	return gore::len( p - ( a + ab * t ) );
}

// how thick the bone in the limb from this bone to its child is (inches)
float BoneRadius( const char *name ) {
	static const struct { const char *part; float r; } bones[] = {
		{ "Spine", 0.7f }, { "Pelvis", 1.2f }, { "Neck", 0.45f }, { "Clavicle", 0.3f }, { "UpperArm", 0.42f },
		{ "Forearm", 0.35f }, { "Thigh", 0.55f }, { "Calf", 0.45f }, { "Foot", 0.3f },
	};
	for ( auto &b : bones ) {
		if ( strstr( name, b.part ) ) {
			return b.r;
		}
	}
	return 0;
}

// --------------------------------------------------------------------------- models and bodies

struct Surf {
	int numVerts = 0, numTris = 0;
	std::vector<V3> xyz, nrm;       // rest pose, metres
	std::vector<int> tri;
};

struct Model {
	bool head = false;              // a rigid head: one bone, a skull shell, brains
	int numBones = 0;
	std::vector<Xf> rest, restInv;
	std::vector<int> parents, flags;
	std::vector<Surf> surfs;
	gore::VoxelBody vb;
	std::vector<uint8_t> island;    // filled voxels not joined to the body from the start (a flap, a pouch)
};

int rebuildsLeft = 1;               // wounds worked out again this frame (Gore_NewFrame)
// A single synchronous rebuild uses this at a time. Keep one reusable prefix
// table instead of a dense integer grid in every wounded body.
std::vector<int> carvedSum;

// the model's own loose parts, so they're never taken for pieces shot off (or crumbs)
void MarkIslands( Model &m, const signed char *legSide = nullptr ) {
	gore::VoxelBody vb;
	vb.copyStateFrom(m.vb);
	std::vector<gore::Piece> pcs = vb.pieces( 1, legSide );
	m.island.assign( vb.state.size(), 0 );
	for ( size_t c = 0; c < vb.state.size(); c++ ) {
		int l = vb.labels()[c];
		if ( l >= 0 && !pcs[l].isMain ) {
			m.island[c] = 1;
		}
	}
}

struct Body {
	int model = 0;
	bool piece = false;
	gore::VoxelBody vb;
	std::vector<uint8_t> compactMask; // CARVED bits when a body is offscreen
	bool dirty = true;
	bool fullSkinDirty = true, fullWalls = false;
	int pendingBounds[6] = {}, wallBounds[6] = {};
	std::vector<std::array<int, 6>> changedBoxes;
	bool mergedChanges = false;
	float skinScale = 1.f;
	std::vector<std::vector<int>> keep;
	std::vector<std::vector<float>> sub;
	gore::Mesh flesh;               // the wound walls, rest pose (metres)
	std::vector<float> fxyz, fnrm;
	std::vector<unsigned char> frgba;
	std::vector<int> fidx;
	vrGoreDraw_t draw;
	// what draw was worked out for: the same again (the other eye, a body lying still) is reused
	bool drawValid = false;
	std::vector<float> drawPose;
	float drawLight[9] = {};
};

void MergeBounds( int (&box)[6], const int *add ) {
	if ( box[3] <= box[0] ) {
		memcpy( box, add, sizeof( box ) );
		return;
	}
	for ( int k = 0; k < 3; ++k ) {
		box[k] = std::min( box[k], add[k] );
		box[k + 3] = std::max( box[k + 3], add[k + 3] );
	}
}

std::unique_ptr<Model> models[MAX_MODELS + 1];
std::unique_ptr<Body> bodies[MAX_BODIES + 1];

Model *GetModel( int h ) { return h > 0 && h <= MAX_MODELS ? models[h].get() : nullptr; }
Body *GetBody( int h ) { return h > 0 && h <= MAX_BODIES ? bodies[h].get() : nullptr; }

int NewBodySlot() {
	for ( int i = 1; i <= MAX_BODIES; i++ ) {
		if ( !bodies[i] ) {
			return i;
		}
	}
	return 0;
}

// --------------------------------------------------------------------------- the skin's holes

// is the skin at p (rest, metres, facing n) still there: the voxel just under it is body
bool SkinStays( const Body &b, V3 p, V3 n ) {
	const gore::VoxelBody &vb = b.vb;
	V3 q = p - n * ( vb.cell * 1.1f );     // a cell in: the cloth opens no wider than the wound under it
	int x = (int)floorf( ( q.x - vb.origin.x ) / vb.cell );
	int y = (int)floorf( ( q.y - vb.origin.y ) / vb.cell );
	int z = (int)floorf( ( q.z - vb.origin.z ) / vb.cell );
	if ( vb.in( x, y, z ) ) {
		const int i = vb.id( x, y, z );
		uint8_t s = vb.state[i];
		if ( s == gore::SOLID || s == gore::BONE ) {
			return true;
		}
		if ( s == gore::CARVED ) {
			return false;
		}
	}
	// Thin or open skin can lie outside the grid. Sample nearby occupied cells.
	for ( int r = 1; r <= 2; r++ ) {
		float best = 1e9f;
		int found = -1;
		for ( int dz = -r; dz <= r; dz++ ) {
			for ( int dy = -r; dy <= r; dy++ ) {
				for ( int dx = -r; dx <= r; dx++ ) {
					int X = x + dx, Y = y + dy, Z = z + dz;
					if ( !vb.in( X, Y, Z ) ) {
						continue;
					}
					uint8_t s = vb.state[vb.id( X, Y, Z )];
					if ( s == gore::EMPTY ) {
						continue;
					}
					float d = (float)( dx * dx + dy * dy + dz * dz );
					if ( d < best ) {
						best = d;
						found = s;
					}
				}
			}
		}
		if ( found >= 0 ) {
			return found != gore::CARVED;
		}
	}
	return !b.piece;    // nothing near: the body keeps it, a piece doesn't
}

// carved voxels counted over boxes: carvedSum[x,y,z] = how many in [0,x) x [0,y) x [0,z)
void CountCarved( Body &b ) {
	const gore::VoxelBody &vb = b.vb;
	int sx = vb.nx + 1, sy = vb.ny + 1, sz = vb.nz + 1;
	carvedSum.resize( (size_t)sx * sy * sz, 0 );
	// A different model can reuse the same storage with different strides.
	std::fill_n(carvedSum.data(), size_t(sx)*sy, 0);
	for ( int z = 1; z < sz; z++ ) {
		std::fill_n(carvedSum.data() + size_t(z)*sx*sy, sx, 0);
		for ( int y = 1; y < sy; y++ ) {
			const size_t slice = size_t(sx) * sy;
			int *dst = carvedSum.data() + z * slice + y * sx;
			dst[0] = 0;
			const int *above = dst - sx, *prior = dst - slice;
			const int *priorAbove = prior - sx;
			const uint8_t *cells = vb.state.data() + vb.id(0,y-1,z-1);
			int row = 0;
			for ( int x = 1; x < sx; x++ ) {
				row += cells[x-1] == gore::CARVED;
				dst[x] = row + above[x] + prior[x] - priorAbove[x];
			}
		}
	}
}

// is there a wound (or a cut) within r of c
bool Near( const Body &b, V3 c, float r ) {
	const gore::VoxelBody &vb = b.vb;
	int lo[3], hi[3];
	float cc[3] = { c.x, c.y, c.z }, o[3] = { vb.origin.x, vb.origin.y, vb.origin.z };
	int n[3] = { vb.nx, vb.ny, vb.nz };
	for ( int k = 0; k < 3; k++ ) {
		lo[k] = std::max( 0, (int)floorf( ( cc[k] - r - o[k] ) / vb.cell ) );
		hi[k] = std::min( n[k], (int)floorf( ( cc[k] + r - o[k] ) / vb.cell ) + 1 );
		if ( lo[k] >= hi[k] ) {
			return false;
		}
	}
	int sx = vb.nx + 1, sy = vb.ny + 1;
	auto S = [&]( int x, int y, int z ) { return carvedSum[( (size_t)z * sy + y ) * sx + x]; };
	int count = S( hi[0], hi[1], hi[2] ) - S( lo[0], hi[1], hi[2] ) - S( hi[0], lo[1], hi[2] ) - S( hi[0], hi[1], lo[2] )
				+ S( lo[0], lo[1], hi[2] ) + S( lo[0], hi[1], lo[2] ) + S( hi[0], lo[1], lo[2] ) - S( lo[0], lo[1], lo[2] );
	return count > 0;
}

struct Refiner {
	const Body *b;
	const V3 *X, *N;        // the triangle's corners
	float cell;
	std::vector<float> *out;
	int tri;

	V3 At( const float *w, const V3 *v ) const { return v[0] * w[0] + v[1] * w[1] + v[2] * w[2]; }

	void Emit( float B[3][3] ) {
		out->push_back( (float)tri );
		for ( int k = 0; k < 3; k++ ) {
			for ( int j = 0; j < 3; j++ ) {
				out->push_back( B[k][j] );
			}
		}
	}

	// 1 keeps a complete node, -1 removes it, 0 emits a partial boundary.
	int Run( float B[3][3], int depth ) {
		V3 P[3];
		for ( int k = 0; k < 3; k++ ) {
			P[k] = At( B[k], X );
		}
		V3 c = ( P[0] + P[1] + P[2] ) * ( 1.0f / 3 );
		float rad = std::max( gore::len( P[0] - c ), std::max( gore::len( P[1] - c ), gore::len( P[2] - c ) ) );
		// The farthest sample in SkinStays is 1.1 cells inward plus a
		// two-cell neighborhood. Outside that reach the original skin stays.
		if ( !b->piece && !Near( *b, c, rad + b->vb.cell * 3.2f ) ) {
			return 1;
		}
		const V3 edges[] = {P[1] - P[2], P[2] - P[0], P[0] - P[1]};
		const float e[] = {gore::dot(edges[0],edges[0]), gore::dot(edges[1],edges[1]), gore::dot(edges[2],edges[2])};
		int longest = e[0] > e[1] ? ( e[0] > e[2] ? 0 : 2 ) : ( e[1] > e[2] ? 1 : 2 );
		if ( e[longest] < cell * cell || depth >= 10 ) {
			const V3 nc = gore::norm(gore::norm(At(B[0],N)) + gore::norm(At(B[1],N)) + gore::norm(At(B[2],N)));
			return SkinStays( *b, c, nc ) ? 1 : -1;
		}
		// split the longest edge (between the other two corners)
		int i = ( longest + 1 ) % 3, j = ( longest + 2 ) % 3;
		float M[3], C1[3][3], C2[3][3];
		for ( int k = 0; k < 3; k++ ) {
			M[k] = ( B[i][k] + B[j][k] ) * 0.5f;
		}
		memcpy( C1, B, sizeof( C1 ) );
		memcpy( C2, B, sizeof( C2 ) );
		memcpy( C1[j], M, sizeof( M ) );
		memcpy( C2[i], M, sizeof( M ) );
		const int a = Run( C1, depth + 1 ), b = Run( C2, depth + 1 );
		if ( a == b && a != 0 ) return a;
		if ( a == 1 ) Emit( C1 );
		if ( b == 1 ) Emit( C2 );
		return 0;
	}
};

void BuildSkin( Body &b, const Model &m ) {
	b.keep.resize( m.surfs.size() );
	b.sub.resize( m.surfs.size() );
	const float origin[] = {b.vb.origin.x, b.vb.origin.y, b.vb.origin.z};
	std::vector<std::array<float,6>> changedRegions;
	auto addRegion = [&](const int *bounds) {
		std::array<float,6> region;
		for ( int k = 0; k < 3; ++k ) {
			region[k] = origin[k] + ( bounds[k] - 3.2f ) * b.vb.cell;
			region[k+3] = origin[k] + ( bounds[k+3] + 3.2f ) * b.vb.cell;
		}
		changedRegions.push_back(region);
	};
	if ( b.mergedChanges ) addRegion(b.pendingBounds);
	else for (const auto &box : b.changedBoxes) addRegion(box.data());
	for ( size_t s = 0; s < m.surfs.size(); s++ ) {
		const Surf &sf = m.surfs[s];
		std::vector<int> oldKeep;
		std::vector<float> oldSub;
		oldKeep.swap( b.keep[s] );
		oldSub.swap( b.sub[s] );
		b.keep[s].reserve( oldKeep.size() );
		b.sub[s].reserve( oldSub.size() );
		size_t keepAt = 0, subAt = 0;
		Refiner r;
		r.b = &b;
		r.cell = b.vb.cell * b.skinScale;
		r.out = &b.sub[s];
		for ( int t = 0; t < sf.numTris; t++ ) {
			V3 X[3], N[3];
			for ( int k = 0; k < 3; k++ ) {
				X[k] = sf.xyz[sf.tri[t * 3 + k]];
				N[k] = sf.nrm[sf.tri[t * 3 + k]];
			}
			if ( !b.fullSkinDirty ) {
				const bool wasKept = keepAt < oldKeep.size() && oldKeep[keepAt] == t;
				if ( wasKept ) ++keepAt;
				const size_t first = subAt;
				while ( subAt < oldSub.size() && int(oldSub[subAt]) == t ) subAt += 10;
				const float axes[3][3] = {{X[0].x,X[1].x,X[2].x}, {X[0].y,X[1].y,X[2].y},
				                          {X[0].z,X[1].z,X[2].z}};
				float lo[3], hi[3];
				for ( int k = 0; k < 3; ++k ) {
					lo[k] = std::min({axes[k][0],axes[k][1],axes[k][2]});
					hi[k] = std::max({axes[k][0],axes[k][1],axes[k][2]});
				}
				const bool changed = std::any_of(changedRegions.begin(), changedRegions.end(), [&](const auto &box) {
					return hi[0] >= box[0] && lo[0] <= box[3] && hi[1] >= box[1] &&
					       lo[1] <= box[4] && hi[2] >= box[2] && lo[2] <= box[5];
				});
				if ( !changed ) {
					if ( wasKept ) b.keep[s].push_back( t );
					b.sub[s].insert( b.sub[s].end(), oldSub.begin() + first, oldSub.begin() + subAt );
					continue;
				}
			}
			float B[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
			r.X = X;
			r.N = N;
			r.tri = t;
			if ( r.Run( B, 0 ) == 1 ) {
				b.keep[s].push_back( t );
			}
		}
	}
}

void Rebuild( Body &b, const signed char *legSide ) {
	const Model *m = GetModel( b.model );
	if ( !m ) {
		return;
	}
	CountCarved( b );
	for (;;) {
		BuildSkin( b, *m );
		size_t total = 0;
		bool fits = true;
		for ( size_t s = 0; s < m->surfs.size(); ++s ) {
			const size_t sub = b.sub[s].size() / 10;
			total += sub;
			fits &= m->surfs[s].numVerts + 3 * sub < 65535;
		}
		if ( fits && total <= 20000 ) break;
		// Increase only the rim sampling interval. Keep all carved voxels.
		b.skinScale *= 1.5f;
		b.fullSkinDirty = true;
	}
	const int *box = b.wallBounds;
	b.flesh = b.piece || b.fullWalls || box[3] <= box[0] ? b.vb.remesh( true, legSide ) :
		b.vb.remeshRegion( true, box[0], box[1], box[2], box[3], box[4], box[5], legSide );
	memset( b.pendingBounds, 0, sizeof(b.pendingBounds) );
	b.changedBoxes.clear();
	b.mergedChanges = false;
	b.fullSkinDirty = false;
	b.dirty = false;
}

}   // namespace

// =========================================================================== the C interface

extern "C" {

void Gore_NewFrame( void ) {
	rebuildsLeft = 1;
}

void Gore_Shutdown( void ) {
	for ( auto &b : bodies ) {
		b.reset();
	}
	for ( auto &m : models ) {
		m.reset();
	}
	std::vector<int>().swap(carvedSum);
}

int Gore_LoadModel( const void *data, int size, const float *restPose, int numBones,
					const int *parents, const int *flags, const char *names, float cell ) {
	const MdsHeader *h = (const MdsHeader *)data;
	int slot = 0;

	if ( !data || size < (int)sizeof( MdsHeader ) || h->ident != ( ( 'W' << 24 ) + ( 'S' << 16 ) + ( 'D' << 8 ) + 'M' ) ||
		 h->version != 4 || h->numBones != numBones || numBones > MAX_BONES || h->numSurfaces > VRG_MAX_SURFS ||
		 h->ofsEnd > size ) {
		return 0;
	}
	for ( int i = 1; i <= MAX_MODELS; i++ ) {
		if ( !models[i] ) {
			slot = i;
			break;
		}
	}
	if ( !slot ) {
		return 0;
	}
	std::unique_ptr<Model> m( new Model );
	m->numBones = numBones;
	for ( int i = 0; i < numBones; i++ ) {
		m->rest.push_back( PoseBone( restPose, i ) );
		m->restInv.push_back( Inverse( m->rest.back() ) );
		m->parents.push_back( parents[i] );
		m->flags.push_back( flags[i] );
	}

	// the skin in the rest pose, as the renderer would draw it
	gore::Body body;
	const char *base = (const char *)data;
	const MdsSurface *s = (const MdsSurface *)( base + h->ofsSurfaces );
	for ( int si = 0; si < h->numSurfaces; si++ ) {
		if ( (const char *)s < base || (const char *)s + sizeof( MdsSurface ) > base + size ) {
			return 0;
		}
		Surf sf;
		sf.numVerts = s->numVerts;
		sf.numTris = s->numTriangles;
		uint32_t first = (uint32_t)body.pos.size();
		const MdsVertex *v = (const MdsVertex *)( (const char *)s + s->ofsVerts );
		for ( int j = 0; j < s->numVerts; j++ ) {
			V3 p { 0, 0, 0 };
			float best = -1;
			int bone = 0;
			for ( int k = 0; k < v->numWeights; k++ ) {
				const MdsWeight &w = v->weights[k];
				if ( w.boneIndex < 0 || w.boneIndex >= numBones ) {
					return 0;
				}
				p = p + Point( m->rest[w.boneIndex], ToV3( w.offset ) ) * w.boneWeight;
				if ( w.boneWeight > best ) {
					best = w.boneWeight;
					bone = w.boneIndex;
				}
			}
			V3 n = v->numWeights > 0 ? gore::norm( Dir( m->rest[v->weights[0].boneIndex], ToV3( v->normal ) ) ) : V3 { 0, 0, 1 };
			sf.xyz.push_back( p * METRES );
			sf.nrm.push_back( n );
			body.pos.push_back( p * METRES );
			body.bone.push_back( (uint8_t)bone );
			v = (const MdsVertex *)&v->weights[v->numWeights];
		}
		const int *tri = (const int *)( (const char *)s + s->ofsTriangles );
		for ( int t = 0; t < s->numTriangles * 3; t++ ) {
			if ( tri[t] < 0 || tri[t] >= s->numVerts ) {
				return 0;
			}
			sf.tri.push_back( tri[t] );
			body.idx.push_back( first + tri[t] );
		}
		m->surfs.push_back( std::move( sf ) );
		s = (const MdsSurface *)( (const char *)s + s->ofsEnd );
	}
	if ( !m->vb.build( body, cell * METRES ) ) {
		return 0;
	}
	// the bones inside it
	for ( int i = 0; i < numBones; i++ ) {
		int p = parents[i];
		if ( p < 0 || ( flags[i] & 1 ) || ( flags[p] & 1 ) ) {
			continue;
		}
		float r = BoneRadius( names + p * 32 );
		if ( r > 0 ) {
			m->vb.addBoneRod( ToV3( m->rest[p].t ) * METRES, ToV3( m->rest[i].t ) * METRES, r * METRES, (uint8_t)p );
		}
	}
	MarkIslands( *m );
	models[slot] = std::move( m );
	return slot;
}

static V3 LatLong( short n ) {
	float lat = ( ( n >> 8 ) & 255 ) * ( 2 * 3.14159265f / 255 ), lng = ( n & 255 ) * ( 2 * 3.14159265f / 255 );
	return { cosf( lat ) * sinf( lng ), sinf( lat ) * sinf( lng ), cosf( lng ) };
}

int Gore_LoadHead( const void *data, int size, float cell ) {
	const char *base = (const char *)data;
	int ident = size >= 4 ? *(const int *)data : 0, slot = 0, numSurfaces;
	const int MD3 = ( '3' << 24 ) + ( 'P' << 16 ) + ( 'D' << 8 ) + 'I', MDC = ( 'C' << 24 ) + ( 'P' << 16 ) + ( 'D' << 8 ) + 'I';
	const char *surf;

	if ( ident == MD3 && size >= (int)sizeof( Md3Header ) ) {
		numSurfaces = ( (const Md3Header *)data )->numSurfaces;
		surf = base + ( (const Md3Header *)data )->ofsSurfaces;
	} else if ( ident == MDC && size >= (int)sizeof( MdcHeader ) ) {
		numSurfaces = ( (const MdcHeader *)data )->numSurfaces;
		surf = base + ( (const MdcHeader *)data )->ofsSurfaces;
	} else {
		return 0;
	}
	if ( numSurfaces <= 0 || numSurfaces > VRG_MAX_SURFS ) {
		return 0;
	}
	for ( int i = 1; i <= MAX_MODELS; i++ ) {
		if ( !models[i] ) {
			slot = i;
			break;
		}
	}
	if ( !slot ) {
		return 0;
	}
	std::unique_ptr<Model> m( new Model );
	m->head = true;
	m->numBones = 1;
	Xf id = { { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, { 0, 0, 0 } };
	m->rest.push_back( id );
	m->restInv.push_back( id );
	m->parents.push_back( -1 );
	m->flags.push_back( 0 );

	gore::Body body;
	for ( int si = 0; si < numSurfaces; si++ ) {
		if ( surf < base || surf + sizeof( MdcSurface ) > base + size ) {
			return 0;
		}
		Surf sf;
		int numVerts, numTris, ofsTris, ofsEnd;
		const Md3XyzNormal *xyz;
		const unsigned *comp = nullptr;
		if ( ident == MD3 ) {
			const Md3Surface *s = (const Md3Surface *)surf;
			numVerts = s->numVerts; numTris = s->numTriangles; ofsTris = s->ofsTriangles; ofsEnd = s->ofsEnd;
			xyz = (const Md3XyzNormal *)( surf + s->ofsXyzNormals );       // frame 0
		} else {
			const MdcSurface *s = (const MdcSurface *)surf;
			numVerts = s->numVerts; numTris = s->numTriangles; ofsTris = s->ofsTriangles; ofsEnd = s->ofsEnd;
			short baseFrame = ( (const short *)( surf + s->ofsFrameBaseFrames ) )[0];
			short compFrame = ( (const short *)( surf + s->ofsFrameCompFrames ) )[0];
			xyz = (const Md3XyzNormal *)( surf + s->ofsXyzNormals ) + baseFrame * numVerts;
			if ( compFrame >= 0 ) {
				comp = (const unsigned *)( surf + s->ofsXyzCompressed ) + compFrame * numVerts;
			}
		}
		if ( (const char *)( xyz + numVerts ) > base + size || surf + ofsTris + numTris * 12 > base + size ) {
			return 0;
		}
		sf.numVerts = numVerts;
		sf.numTris = numTris;
		uint32_t first = (uint32_t)body.pos.size();
		for ( int j = 0; j < numVerts; j++ ) {
			V3 p { xyz[j].xyz[0] / 64.0f, xyz[j].xyz[1] / 64.0f, xyz[j].xyz[2] / 64.0f };
			if ( comp ) {       // tr_local.h R_MDC_DecodeXyzCompressed
				unsigned o = comp[j];
				p = p + V3 { ( (float)( o & 255 ) - 127.0f ) * 0.05f, ( (float)( ( o >> 8 ) & 255 ) - 127.0f ) * 0.05f,
							 ( (float)( ( o >> 16 ) & 255 ) - 127.0f ) * 0.05f };
			}
			sf.xyz.push_back( p * METRES );
			sf.nrm.push_back( LatLong( xyz[j].normal ) );
			body.pos.push_back( p * METRES );
			body.bone.push_back( 0 );
		}
		const int *tri = (const int *)( surf + ofsTris );
		for ( int t = 0; t < numTris * 3; t++ ) {
			if ( tri[t] < 0 || tri[t] >= numVerts ) {
				return 0;
			}
			sf.tri.push_back( tri[t] );
			body.idx.push_back( first + tri[t] );
		}
		m->surfs.push_back( std::move( sf ) );
		surf += ofsEnd;
	}
	if ( !m->vb.build( body, cell * METRES ) ) {
		return 0;
	}
	// the skull: a shell from just under the scalp to 2.5 cm in
	for ( size_t i = 0; i < m->vb.state.size(); i++ ) {
		float d = m->vb.depth[i] * m->vb.cell;
		if ( m->vb.state[i] == gore::SOLID && d >= 0.006f && d <= 0.025f ) {
			m->vb.state[i] = gore::BONE;
		}
	}
	MarkIslands( *m );
	models[slot] = std::move( m );
	return slot;
}

void Gore_ModelCentre( int model, float out[3] ) {
	Model *m = GetModel( model );
	out[0] = out[1] = out[2] = 0;
	if ( m ) {
		const gore::VoxelBody &vb = m->vb;
		V3 c = ( vb.origin + V3 { vb.nx * 0.5f, vb.ny * 0.5f, vb.nz * 0.5f } * vb.cell ) * ( 1.0f / METRES );
		memcpy( out, &c, sizeof( float ) * 3 );
	}
}

int Gore_NewBody( int model ) {
	Model *m = GetModel( model );
	int slot = NewBodySlot();
	if ( !m || !slot ) {
		return 0;
	}
	auto body = std::make_unique<Body>();
	body->model = model;
	body->vb.copyStateFrom(m->vb);
	bodies[slot] = std::move(body);
	return slot;
}

void Gore_FreeBody( int body ) {
	if ( GetBody( body ) ) {
		bodies[body].reset();
	}
}

int Gore_BodyModel( int body ) {
	Body *b = GetBody( body );
	return b ? b->model : 0;
}

int Gore_BodiesInUse( void ) {
	int n = 0;
	for ( auto &b : bodies ) {
		n += b ? 1 : 0;
	}
	return n;
}

int Gore_Shot( int body, const float *pose, const float point[3], const float dir[3],
			   float radiusIn, float radiusOut, float energy, goreShot_t *out,
			   const unsigned char *boneMask ) {
	Body *b = GetBody( body );
	Model *m = b ? GetModel( b->model ) : nullptr;
	goreShot_t res;

	memset( &res, 0, sizeof( res ) );
	if ( out ) {
		*out = res;
	}
	if ( !m || !pose || b->piece ) {
		return 0;
	}
	// the limb it hit: the bone segment nearest the point, moving with the parent bone
	V3 p = ToV3( point ), d = gore::norm( ToV3( dir ) );
	int bone = -1, child = -1;
	float best = 1e9f;
	for ( int i = 0; i < m->numBones; i++ ) {
		int pa = m->parents[i];
		if ( pa < 0 || ( m->flags[i] & 1 ) || ( m->flags[pa] & 1 ) ) {
			continue;
		}
		float dist = SegDist( p, ToV3( pose + pa * 12 + 9 ), ToV3( pose + i * 12 + 9 ) );
		if ( dist < best ) {
			best = dist;
			bone = pa;
			child = i;
		}
	}
	if ( m->numBones == 1 ) {
		bone = child = 0;   // rigid
	}
	if ( bone < 0 ) {
		return 0;
	}
	Xf toRest = Mul( m->rest[bone], Inverse( PoseBone( pose, bone ) ) );
	Xf toPose = Inverse( toRest );
	V3 pr = Point( toRest, p ) * METRES, dr = gore::norm( Dir( toRest, d ) );
	const float back = 0.35f;       // start outside the body
	V3 entry = pr - dr * back;
	gore::Wound w = b->vb.carve( entry, dr, radiusIn, radiusOut, energy, boneMask );
	if ( !w.cellsRemoved ) {
		// it missed the drawn body (hit boxes are bigger): aimed at the bone it passed, if close
		V3 a = ToV3( m->rest[bone].t ) * METRES, c = ToV3( m->rest[child].t ) * METRES;
		if ( m->numBones == 1 ) {       // a head: aimed at its middle
			a = c = b->vb.origin + V3 { b->vb.nx * 0.5f, b->vb.ny * 0.5f, b->vb.nz * 0.5f } * b->vb.cell;
		}
		float t;
		V3 onRay = pr + dr * gore::dot( a - pr, dr );
		SegDist( onRay, a, c, &t );
		V3 onBone = a + ( c - a ) * t;
		V3 off = onBone - pr;
		off = off - dr * gore::dot( off, dr );
		if ( gore::len( off ) > 0.08f ) {
			return 0;
		}
		entry = entry + off;
		w = b->vb.carve( entry, dr, radiusIn, radiusOut, energy, boneMask );
		if ( !w.cellsRemoved ) {
			return 0;
		}
	}
	b->dirty = true;
	MergeBounds( b->pendingBounds, b->vb.dirty );
	MergeBounds( b->wallBounds, b->vb.dirty );
	if ( b->changedBoxes.size() < 8 && !b->mergedChanges ) {
		std::array<int,6> box;
		std::copy_n(b->vb.dirty,6,box.begin());
		b->changedBoxes.push_back(box);
	} else {
		b->mergedChanges = true;
	}

	res.cells = w.cellsRemoved;
	res.boneCells = w.boneCellsHit;
	res.exited = w.exited;
	res.length = w.length / METRES;
	// where the skin was met: the first voxel along the path that isn't outside the body
	float t0 = back;
	for ( float t = 0; t < back + 0.05f; t += b->vb.cell * 0.5f ) {
		V3 q = entry + dr * t;
		int x = (int)floorf( ( q.x - b->vb.origin.x ) / b->vb.cell ), y = (int)floorf( ( q.y - b->vb.origin.y ) / b->vb.cell ),
			z = (int)floorf( ( q.z - b->vb.origin.z ) / b->vb.cell );
		if ( b->vb.in( x, y, z ) && b->vb.state[b->vb.id( x, y, z )] != gore::EMPTY ) {
			if ( boneMask && !boneMask[b->vb.boneAt(b->vb.id( x, y, z ))] ) {
				continue;
			}
			t0 = t;
			break;
		}
	}
	V3 r0 = ( entry + dr * t0 ) * ( 1.0f / METRES ), r1 = ( entry + dr * ( t0 + w.length ) ) * ( 1.0f / METRES );
	V3 e0 = Point( toPose, r0 ), e1 = Point( toPose, r1 );
	memcpy( res.entry, &e0, sizeof( res.entry ) );
	memcpy( res.exit, &e1, sizeof( res.exit ) );
	res.bone = bone;
	memcpy( res.restEntry, &r0, sizeof( res.restEntry ) );
	memcpy( res.restExit, &r1, sizeof( res.restExit ) );
	memcpy( res.restDir, &dr, sizeof( res.restDir ) );
	if ( out ) {
		*out = res;
	}
	return res.cells;
}

int Gore_Sever( int body, int *pieces, float centres[][3], int *bones, int max,
                const signed char *legSide ) {
	Body *b = GetBody( body );
	int n = 0;

	if ( !b || b->piece ) {
		return 0;
	}
	Model *m = GetModel( b->model );
	struct ReleaseLabels {
		gore::VoxelBody &grid;
		~ReleaseLabels() { grid.releaseLabels(); }
	} scratch{b->vb};
	std::vector<gore::Piece> pcs = b->vb.pieces( 1, legSide );
	if ( pcs.size() < 2 || !m ) {
		return 0;
	}
	const std::vector<int> &labels = b->vb.labels();
	std::vector<int> own( pcs.size(), 0 );      // voxels of each that were joined to the body at first
	for ( size_t c = 0; c < labels.size(); c++ ) {
		if ( labels[c] >= 0 && !m->island[c] ) {
			own[labels[c]]++;
		}
	}
	for ( size_t i = 0; i < pcs.size(); i++ ) {
		if ( pcs[i].isMain || !own[i] ) {
			continue;       // the body, or a loose part of the model it always had
		}
		b->dirty = true;
		b->fullSkinDirty = b->fullWalls = true;
		if ( own[i] < 40 ) {        // a crumb: gone
			for ( size_t c = 0; c < labels.size(); c++ ) {
				if ( labels[c] == (int)i ) {
					b->vb.state[c] = gore::CARVED;
				}
			}
			continue;
		}
		int slot = n < max ? NewBodySlot() : 0;
		if ( slot ) {
			std::unique_ptr<Body> p( new Body );
			p->model = b->model;
			p->piece = true;
			p->vb.copyStateFrom(b->vb);
			for ( size_t c = 0; c < labels.size(); c++ ) {
				if ( p->vb.filled( (int)c ) && labels[c] != (int)i ) {
					p->vb.state[c] = gore::CARVED;
				}
			}
			bodies[slot] = std::move( p );
			pieces[n] = slot;
			V3 c = pcs[i].centroid * ( 1.0f / METRES );
			memcpy( centres[n], &c, sizeof( float ) * 3 );
			bones[n] = pcs[i].mainBone;
			n++;
		}
		// off the body, whether it could be drawn flying or not
		for ( size_t c = 0; c < labels.size(); c++ ) {
			if ( labels[c] == (int)i ) {
				b->vb.state[c] = gore::CARVED;
			}
		}
	}
	return n;
}

int Gore_Chop( int body, int from, int to, float t, float radius ) {
	Body *b = GetBody( body );
	Model *m = b ? GetModel( b->model ) : nullptr;
	int cells = 0;

	if ( !m || b->piece || from < 0 || to < 0 || from >= m->numBones || to >= m->numBones ) {
		return 0;
	}
	gore::VoxelBody &vb = b->vb;
	V3 a = ToV3( m->rest[from].t ) * METRES, c = ToV3( m->rest[to].t ) * METRES;
	V3 axis = gore::norm( c - a ), at = a + ( c - a ) * t;
	float r = radius * METRES, half = vb.cell * 1.5f;
	for ( int z = 0; z < vb.nz; z++ ) {
		for ( int y = 0; y < vb.ny; y++ ) {
			for ( int x = 0; x < vb.nx; x++ ) {
				int i = vb.id( x, y, z );
				if ( !vb.filled( i ) ) {
					continue;
				}
				V3 d = vb.centre( x, y, z ) - at;
				float along = gore::dot( d, axis );
				if ( fabsf( along ) > half || gore::len( d - axis * along ) > r ) {
					continue;
				}
				vb.state[i] = gore::CARVED;
				cells++;
			}
		}
	}
	// the limb beyond the cut is often pressed against the body (an arm at the side, a thigh
	// under a coat): part it from whatever it touches that isn't the limb, so it comes away
	std::vector<char> limb( m->numBones, 0 );
	for ( int i = 0; i < m->numBones; i++ ) {
		for ( int j = i; j >= 0; j = m->parents[j] ) {
			if ( j == from ) {
				limb[i] = 1;
				break;
			}
		}
	}
	auto inLimb = [&]( int x, int y, int z ) {
		int i = vb.id( x, y, z );
		return vb.filled( i ) && vb.boneAt(i) < m->numBones && limb[vb.boneAt(i)] &&
			   gore::dot( vb.centre( x, y, z ) - at, axis ) > 0;
	};
	std::vector<int> part;
	for ( int z = 1; z < vb.nz - 1; z++ ) {
		for ( int y = 1; y < vb.ny - 1; y++ ) {
			for ( int x = 1; x < vb.nx - 1; x++ ) {
				if ( !inLimb( x, y, z ) ) {
					continue;
				}
				static const int D[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
				for ( auto &dd : D ) {
					int j = vb.id( x + dd[0], y + dd[1], z + dd[2] );
					if ( vb.filled( j ) && !inLimb( x + dd[0], y + dd[1], z + dd[2] ) ) {
						part.push_back( vb.id( x, y, z ) );
						break;
					}
				}
			}
		}
	}
	for ( int i : part ) {
		vb.state[i] = gore::CARVED;
		cells++;
	}
	b->dirty = true;
	b->fullSkinDirty = b->fullWalls = true;
	return cells;
}

void Gore_RestToPose( int body, const float *pose, int bone, const float in[3], float out[3] ) {
	Body *b = GetBody( body );
	Model *m = b ? GetModel( b->model ) : nullptr;
	if ( !m || bone < 0 || bone >= m->numBones ) {
		memcpy( out, in, sizeof( float ) * 3 );
		return;
	}
	V3 p = Point( Mul( PoseBone( pose, bone ), m->restInv[bone] ), ToV3( in ) );
	memcpy( out, &p, sizeof( float ) * 3 );
}

const vrGoreDraw_t *Gore_Draw( int body, const float *pose, const float ambient[3],
							   const float directed[3], const float lightDir[3],
							   const signed char *legSide ) {
	Body *b = GetBody( body );
	Model *m = b ? GetModel( b->model ) : nullptr;
	if ( !m || !pose ) {
		return nullptr;
	}
	if ( b->dirty && ( rebuildsLeft > 0 || b->keep.empty() ) ) {
		Rebuild( *b, legSide );
		rebuildsLeft--;
		b->drawValid = false;
		b->fidx.assign( b->flesh.idx.begin(), b->flesh.idx.end() );
	}
	if ( b->keep.size() != m->surfs.size() ) {
		return nullptr;
	}
	// RealRTCW-VR: unchanged since last time (VR draws each eye, and a body lying dead still
	// keeps its pose): the same flesh, not posed and lit again
	{
		const size_t np = (size_t)m->numBones * 12;
		float light[9];
		memcpy( light, ambient, sizeof( float ) * 3 );
		memcpy( light + 3, directed, sizeof( float ) * 3 );
		memcpy( light + 6, lightDir, sizeof( float ) * 3 );
		if ( b->drawValid && b->drawPose.size() == np && !memcmp( b->drawPose.data(), pose, sizeof( float ) * np ) &&
			 !memcmp( b->drawLight, light, sizeof( light ) ) ) {
			return &b->draw;
		}
		b->drawPose.assign( pose, pose + np );
		memcpy( b->drawLight, light, sizeof( light ) );
		b->drawValid = true;
	}
	vrGoreDraw_t &dr = b->draw;
	memset( &dr, 0, sizeof( dr ) );
	dr.numSurfaces = (int)m->surfs.size();
	for ( int s = 0; s < dr.numSurfaces; s++ ) {
		vrGoreSurf_t &o = dr.surf[s];
		o.numVerts = m->surfs[s].numVerts;
		o.numTriangles = m->surfs[s].numTris;
		if ( !b->piece && (int)b->keep[s].size() == m->surfs[s].numTris ) {
			o.numKeep = -1;     // untouched
			continue;
		}
		o.numKeep = (int)b->keep[s].size();
		o.keep = b->keep[s].data();
		o.numSub = (int)( b->sub[s].size() / VRG_SUB_FLOATS );
		o.sub = b->sub[s].data();
	}

	// the flesh, each vertex with its bone
	const gore::Mesh &f = b->flesh;
	size_t nv = f.pos.size();
	if ( nv && !f.idx.empty() ) {
		std::vector<Xf> delta( m->numBones );
		std::vector<char> have( m->numBones, 0 );
		b->fxyz.resize( nv * 3 );
		b->fnrm.resize( nv * 3 );
		b->frgba.resize( nv * 4 );
		V3 L = gore::norm( ToV3( lightDir ) );
		for ( size_t i = 0; i < nv; i++ ) {
			int bone = f.bone[i] < m->numBones ? f.bone[i] : 0;
			if ( !have[bone] ) {
				delta[bone] = Mul( PoseBone( pose, bone ), m->restInv[bone] );
				have[bone] = 1;
			}
			V3 p = Point( delta[bone], f.pos[i] * ( 1.0f / METRES ) );
			V3 n = gore::norm( Dir( delta[bone], f.nrm[i] ) );
			memcpy( &b->fxyz[i * 3], &p, sizeof( float ) * 3 );
			memcpy( &b->fnrm[i * 3], &n, sizeof( float ) * 3 );
			// raw and bloody: a thin pink-red layer under the skin, dark red muscle deeper; bone
			float tint[3];
			if ( m->head && f.depthCm[i] != 255 && f.depthCm[i] >= 3 ) {
				tint[0] = 0.43f; tint[1] = 0.24f; tint[2] = 0.25f;     // blood-stained brains
			} else if ( f.depthCm[i] == 255 ) {
				tint[0] = 0.56f; tint[1] = 0.39f; tint[2] = 0.36f;
			} else {
				float t = f.depthCm[i] / 2.0f, deep = f.depthCm[i] / 8.0f;
				t = t > 1 ? 1 : t;
				deep = deep > 1 ? 1 : deep;
				tint[0] = 0.46f - 0.17f * t - 0.10f * deep;
				tint[1] = 0.095f - 0.052f * t - 0.016f * deep;
				tint[2] = 0.12f - 0.06f * t - 0.018f * deep;
			}
			float lit = gore::dot( n, L );
			lit = lit < 0 ? 0 : lit;
			for ( int k = 0; k < 3; k++ ) {
				float c = tint[k] * ( ambient[k] + directed[k] * lit );
				b->frgba[i * 4 + k] = (unsigned char)( c > 255 ? 255 : ( c < 0 ? 0 : c ) );
			}
			b->frgba[i * 4 + 3] = 255;
		}
		if ( b->fidx.size() != f.idx.size() ) {
			b->fidx.assign( f.idx.begin(), f.idx.end() );
		}
		dr.numFleshVerts = (int)nv;
		dr.numFleshIndexes = (int)b->fidx.size();
		dr.fleshXyz = b->fxyz.data();
		dr.fleshNormal = b->fnrm.data();
		dr.fleshRGBA = b->frgba.data();
		dr.fleshIndexes = b->fidx.data();
	}
	return &dr;
}

}   // extern "C"
