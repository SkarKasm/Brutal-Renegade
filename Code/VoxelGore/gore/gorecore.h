// gorecore - the geometry core of GF Gore. No game, no D3D: pure CPU code so it
// can be built and tested anywhere (Linux test harness, MSVC x86 in the ASI).
//
//   Body       a character's bind-pose skin mesh + per-vertex bone index
//   VoxelBody  that mesh as a solid voxel grid in bind space, with per-cell
//              depth under the skin and owning bone (the "carve field")
//   carve      a bullet: tube along the shot, entry + optional exit, holes bone
//   remesh     wound surface (naive surface nets) for a range of blocks
//   pieces     connected components after carving -> severed limbs / chunks
//
// Design notes: see docs/GF_GORE_DESIGN.md §4. Units are metres.
#pragma once
#include <stdint.h>
#include <vector>
#include <math.h>

namespace gore {

struct V3 { float x, y, z; };
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float len(V3 a) { return sqrtf(dot(a, a)); }
inline V3 norm(V3 a) { float l = len(a); return l > 1e-12f ? a * (1.0f / l) : V3{0, 0, 0}; }

struct Body {                       // input: bind-pose skin
    std::vector<V3>      pos;
    std::vector<uint8_t> bone;      // dominant bone per vertex (0..254)
    std::vector<uint32_t> idx;      // triangles
};

// Cell states. EMPTY outside the body, SOLID flesh, BONE inside a bone,
// CARVED removed by a wound (kept distinct from EMPTY so the wound surface
// and the skin mask know where wounds are).
enum : uint8_t { EMPTY = 0, SOLID = 1, BONE = 2, CARVED = 3 };

struct Wound {
    V3 entry, dir; float rIn, rOut, length; bool exited;
    int cellsRemoved, boneCellsHit;
};

struct Piece {                      // a connected component of the remaining body
    int cells; V3 centroid; V3 bmin, bmax;
    uint8_t mainBone;               // most common bone in the piece
    bool isMain;                    // the biggest component (stays on the body)
};

struct Mesh {
    std::vector<V3> pos, nrm;
    std::vector<uint8_t> bone;      // owning bone (for skinning the layer)
    std::vector<uint8_t> depthCm;   // depth under the skin; 255 = bone
    std::vector<uint8_t> wound;     // 1 = vertex lies on a wound wall (flesh), 0 = skin
    std::vector<uint32_t> idx;
};

class VoxelBody {
public:
    // cell = edge length (m). pad = empty border cells.
    bool build(const Body& b, float cell, int pad = 2);
    // The template outlives this instance. Copy only mutable occupancy.
    void copyStateFrom(const VoxelBody& source);

    // bone rods: segments the skeleton occupies (radius in m). Cells within a
    // rod become BONE. Call after build(); used for "bones holed" and exits.
    void addBoneRod(V3 a, V3 b, float radius, uint8_t boneId);

    // A shot. 'energy' ~ how far it can travel through flesh (m); each bone cell
    // costs extra. Returns what happened. Cells are CARVED along a tube whose
    // radius grows from rIn at entry to rOut at exit.
    // A 256-entry bone mask limits the carve to the first passage through
    // the selected limb. Unmasked callers keep the original penetration.
    Wound carve(V3 entry, V3 dir, float rIn, float rOut, float energy,
                const uint8_t* boneMask = nullptr);

    // Remove a labelled component (a severed piece) from this body. Its cells
    // become CARVED, so the skin mask hides the body's skin where it was.
    void removeComponent(int label);

    // Connected components of SOLID|BONE cells (6-connected). Components
    // smaller than crumbCells are "crumbs": turned to CARVED and not returned
    // (MP2VR does the same - 'crumb (gone)').
    std::vector<Piece> pieces(int crumbCells = 40, const int8_t* legSide = nullptr);
    const std::vector<int>& labels() const { return label_; }
    void releaseLabels() { std::vector<int>().swap(label_); }

    // Surface of the remaining body (skin + wound walls) via naive surface nets.
    // onlyWounds: emit only faces that border a CARVED cell (the flesh layer).
    // Optional 256-entry grouping keeps leg wound vertices on their own limb.
    Mesh remesh(bool onlyWounds, const int8_t* legSide = nullptr) const;
    // Same, restricted to the cell box [lo, hi) - for rebuilding only what a
    // shot changed. Faces are emitted for cell pairs whose lower cell is inside.
    Mesh remeshRegion(bool onlyWounds, int x0, int y0, int z0, int x1, int y1, int z1,
                      const int8_t* legSide = nullptr) const;
    // Cell box touched by the last carve() (expanded by 2 cells); empty if none.
    int dirty[6] = {0, 0, 0, 0, 0, 0};

    // Skin hole mask in bind space: W*H*D bytes, 255 = skin shows, 0 = hole.
    // A texel is a hole if a CARVED cell lies within `reach` metres of it.
    // The texture spans the grid's box; sample it with (p - origin) / extent.
    void bakeMask(uint8_t* out, int W, int H, int D, float reach) const;

    // grid access
    int nx = 0, ny = 0, nz = 0; float cell = 0; V3 origin{0, 0, 0};
    std::vector<uint8_t> state, depth, boneOf;   // depth in cells under the skin
    inline int id(int x, int y, int z) const { return (z * ny + y) * nx + x; }
    inline bool in(int x, int y, int z) const { return x >= 0 && y >= 0 && z >= 0 && x < nx && y < ny && z < nz; }
    inline V3 centre(int x, int y, int z) const { return origin + V3{(x + 0.5f) * cell, (y + 0.5f) * cell, (z + 0.5f) * cell}; }
    inline bool filled(int i) const { return state[i] == SOLID || state[i] == BONE; }
    uint8_t boneAt(int i) const { return (layout_ ? layout_->boneOf : boneOf)[i]; }
    uint8_t depthAt(int i) const { return (layout_ ? layout_->depth : depth)[i]; }
private:
    const VoxelBody* layout_ = nullptr;
    std::vector<int> label_;
    void computeDepth();
};

// A severed piece in flight: rigid body with box inertia, gravity, and a flat
// floor. Enough for a piece that tumbles, bounces and settles - the game has
// no ragdolls, so this is all the physics a cut-off limb gets.
struct RigidPiece {
    V3 pos{0, 0, 0}, vel{0, 0, 0};          // centre of mass (world)
    float q[4] = {1, 0, 0, 0};              // orientation (w, x, y, z)
    V3 angVel{0, 0, 0};                     // world rad/s
    V3 half{0.1f, 0.1f, 0.1f};              // box half extents (local)
    float mass = 1;
    bool asleep = false;
    void init(const Piece& p, float density = 1000.0f, float cell = 0.01f);
    void impulse(V3 at, V3 J);              // world point, world impulse
    void step(float dt, float floorY);
    void corner(int i, V3& out) const;      // world position of box corner i (0..7)
};

} // namespace gore
