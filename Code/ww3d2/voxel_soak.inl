// Included by voxel_skin.cpp. All patch coordinates live on the original skin,
// independent of UV atlas layout, the current animation, and voxel tessellation.
constexpr size_t MaxSoaks = 64;
constexpr size_t MaxSoakFaces = 192;
constexpr float SoakSeconds = 2.5f;

struct SoakSurface {
    std::vector<Vector3> rest;
    std::vector<std::array<int, 3>> neighbors;
    std::array<int8_t, 128> legSide{};
};

struct SoakAnchor {
    int triangle = -1;
    Vector3 weights;
};

struct BloodSoak {
    SoakAnchor anchor;
    Vector3 tangent, bitangent;
    Vector3 downhill, acrossRun;
    std::vector<int> faces;
    float started = 0;
    float initialRadius = 0;
    float finalRadius = 0;
    float runoff = 0;
};

void buildSoakSurface(SoakSurface &surface, MeshModelClass &model,
                      const HTreeClass &tree, const std::vector<float> &positions)
{
    using WeldKey = std::array<int, 4>;
    std::map<WeldKey, int> welds;
    std::vector<int> welded(model.Get_Vertex_Count());
    const uint16 *links = model.Get_Vertex_Bone_Links();
    surface.rest.reserve(welded.size());
    for (size_t i = 0; i < welded.size(); ++i) {
        const Vector3 p(positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]);
        surface.rest.push_back(p);
        // Duplicate vertices at a material/UV seam still form the same edge.
        const WeldKey key = {int(std::lround(p.X * 4096)), int(std::lround(p.Y * 4096)),
                             int(std::lround(p.Z * 4096)), links[i]};
        welded[i] = welds.emplace(key, int(i)).first->second;
    }
    for (int i = 0; i < tree.Num_Pivots(); ++i) {
        const char *name = tree.Get_Bone_Name(i);
        int side = i ? surface.legSide[tree.Get_Parent_Index(i)] : 0;
        if (name && !stricmp(name, "C L THIGH")) side = 1;
        if (name && !stricmp(name, "C R THIGH")) side = -1;
        surface.legSide[i] = int8_t(side);
    }

    const TriIndex *triangles = model.Get_Polygon_Array();
    surface.neighbors.resize(model.Get_Polygon_Count(), {-1, -1, -1});
    std::map<std::pair<int, int>, int> edges;
    for (int t = 0; t < model.Get_Polygon_Count(); ++t) {
        for (int k = 0; k < 3; ++k) {
            int a = welded[triangles[t][k]], b = welded[triangles[t][(k + 1) % 3]];
            if (a == b) continue;
            if (a > b) std::swap(a, b);
            auto result = edges.emplace(std::make_pair(a, b), 3 * t + k);
            if (result.second || result.first->second < 0) continue;
            const int other = result.first->second;
            if (other / 3 == t) continue;
            surface.neighbors[t][k] = other / 3;
            surface.neighbors[other / 3][other % 3] = t;
            result.first->second = -1;
        }
    }
}

Vector3 interpolate(const std::vector<Vector3> &positions, const TriIndex &tri, const Vector3 &w)
{
    return positions[tri[0]] * w.X + positions[tri[1]] * w.Y + positions[tri[2]] * w.Z;
}

bool soakOnSide(const SoakSurface &surface, const uint16 *links, const TriIndex &tri, int side)
{
    if (!side) return true;
    for (int k = 0; k < 3; ++k) {
        // A mixed pelvis/leg triangle can stretch across open space in
        // animation. Keep a leg stain on the leg's own skin faces.
        if (surface.legSide[links[tri[k]]] != side) return false;
    }
    return true;
}

Vector3 closestWeights(const Vector3 &p, const Vector3 (&v)[3])
{
    const Vector3 u = v[1] - v[0], w = v[2] - v[0], d = p - v[0];
    const float uu = Vector3::Dot_Product(u, u), ww = Vector3::Dot_Product(w, w);
    const float uw = Vector3::Dot_Product(u, w), det = uu * ww - uw * uw;
    if (det > 1e-14f) {
        const float du = Vector3::Dot_Product(d, u), dw = Vector3::Dot_Product(d, w);
        const float b = (ww * du - uw * dw) / det, c = (uu * dw - uw * du) / det;
        if (b >= 0 && c >= 0 && b + c <= 1) return Vector3(1 - b - c, b, c);
    }
    Vector3 weights(1, 0, 0);
    float best = (p - v[0]).Length2();
    for (int k = 0; k < 3; ++k) {
        const int j = (k + 1) % 3;
        const Vector3 edge = v[j] - v[k];
        if (edge.Length2() <= 1e-12f) continue;
        const float t = std::clamp(Vector3::Dot_Product(p - v[k], edge) / edge.Length2(), 0.f, 1.f);
        const float distance = (p - (v[k] + edge * t)).Length2();
        if (distance < best) {
            best = distance;
            weights.Set(0, 0, 0);
            weights[k] = 1 - t;
            weights[j] = t;
        }
    }
    return weights;
}

SoakAnchor locateSoak(MeshModelClass &model, const SoakSurface &surface,
                      const std::vector<Vector3> &posed, const std::vector<Vector3> &normals,
                      const Vector3 &contact, const Vector3 &direction, const Vector3 *contactNormal,
                      int side, float reach, int regionBone = -1)
{
    const TriIndex *triangles = model.Get_Polygon_Array();
    const uint16 *links = model.Get_Vertex_Bone_Links();
    SoakAnchor direct, nearest;
    float bestRay = reach, bestNear = reach * reach;
    for (int i = 0; i < model.Get_Polygon_Count(); ++i) {
        const TriIndex &tri = triangles[i];
        if (!soakOnSide(surface, links, tri, side)) continue;
        if (regionBone >= 0 && links[tri[0]] != regionBone && links[tri[1]] != regionBone &&
            links[tri[2]] != regionBone) continue;
        const Vector3 v[] = {posed[tri[0]], posed[tri[1]], posed[tri[2]]};
        bool inReach = true;
        for (int k = 0; k < 3; ++k) {
            if (std::min({v[0][k], v[1][k], v[2][k]}) > contact[k] + reach ||
                std::max({v[0][k], v[1][k], v[2][k]}) < contact[k] - reach) inReach = false;
        }
        if (!inReach) continue;
        Vector3 normal = normals[tri[0]] + normals[tri[1]] + normals[tri[2]];
        if (Vector3::Dot_Product(normal, direction) > .05f) continue;
        const Vector3 plane = Vector3::Cross_Product(v[1] - v[0], v[2] - v[0]);
        const float denom = Vector3::Dot_Product(plane, direction);
        if (std::abs(denom) > 1e-9f) {
            const float t = Vector3::Dot_Product(plane, v[0] - contact) / denom;
            if (std::abs(t) < bestRay) {
                const Vector3 point = contact + direction * t;
                const Vector3 weights = closestWeights(point, v);
                const Vector3 onFace = v[0] * weights.X + v[1] * weights.Y + v[2] * weights.Z;
                if ((point - onFace).Length2() < 1e-8f) {
                    bestRay = std::abs(t);
                    direct = {i, weights};
                }
            }
        }
        const Vector3 weights = closestWeights(contact, v);
        const Vector3 point = v[0] * weights.X + v[1] * weights.Y + v[2] * weights.Z;
        float distance = (point - contact).Length2();
        if (contactNormal && Vector3::Dot_Product(normal, *contactNormal) < 0) distance += .0016f;
        if (distance < bestNear) {
            bestNear = distance;
            nearest = {i, weights};
        }
    }
    return direct.triangle >= 0 ? direct : nearest;
}

void addSoak(std::vector<BloodSoak> &soaks, MeshModelClass &model, const SoakSurface &surface,
             const std::vector<Vector3> &posed,
             const SoakAnchor &anchor, int side, float rim, float time, float heatRadius = 0.f)
{
    if (anchor.triangle < 0 || soaks.size() >= MaxSoaks) return;
    const TriIndex *triangles = model.Get_Polygon_Array();
    const TriIndex &root = triangles[anchor.triangle];
    const auto &rest = surface.rest;
    const Vector3 center = interpolate(rest, root, anchor.weights);
    Vector3 tangent = rest[root[1]] - rest[root[0]];
    const Vector3 restEdge = tangent;
    const Vector3 edge = rest[root[2]] - rest[root[0]];
    Vector3 normal = Vector3::Cross_Product(tangent, edge);
    if (normal.Length2() < 1e-14f) return;
    normal.Normalize();

    // Use the direction of the original fabric UVs when meaningful. The
    // stain has its own UV origin and never shares atlas pixels with other faces.
    const Vector2 *uv = model.Get_UV_Array(0, 0);
    if (uv) {
        const Vector2 a = uv[root[1]] - uv[root[0]], b = uv[root[2]] - uv[root[0]];
        const float det = a.X * b.Y - a.Y * b.X;
        if (std::abs(det) > 1e-7f) tangent = (tangent * b.Y - edge * a.Y) * (det > 0 ? 1.f : -1.f);
    }
    if (tangent.Length2() < 1e-12f) return;
    tangent.Normalize();
    const Vector3 bitangent = Vector3::Cross_Product(normal, tangent);
    const float angle = float((unsigned(anchor.triangle) * 37u + soaks.size() * 73u) % 360) * .017453293f;
    BloodSoak soak;
    soak.anchor = anchor;
    soak.tangent = tangent * std::cos(angle) + bitangent * std::sin(angle);
    soak.bitangent = Vector3::Cross_Product(normal, soak.tangent);
    soak.started = time;
    soak.initialRadius = rim + .006f;
    soak.finalRadius = std::clamp(rim * 2.5f + .018f, .065f, .13f);

    // Express world gravity along the impact triangle in rest coordinates.
    // Once deposited, the run follows the skin as the body changes pose.
    const Vector3 a = posed[root[1]] - posed[root[0]];
    const Vector3 b = posed[root[2]] - posed[root[0]];
    const float aa = a.Length2(), bb = b.Length2(), ab = Vector3::Dot_Product(a, b);
    const float det = aa * bb - ab * ab;
    if (det > 1e-14f) {
        const float u = (-a.Z * bb + b.Z * ab) / det;
        const float v = (-b.Z * aa + a.Z * ab) / det;
        const float slope = (a * u + b * v).Length();
        if (slope > .18f) {
            soak.downhill = restEdge * u + edge * v;
            soak.downhill.Normalize();
            soak.acrossRun = Vector3::Cross_Product(soak.downhill, normal);
            soak.runoff = (.075f + rim * 1.5f) * std::min(1.f, slope);
        }
    }

    if (heatRadius > 0.f) {
        soak.initialRadius = soak.finalRadius = heatRadius;
        soak.runoff = 0;
    }

    const float patchReach = std::max(soak.finalRadius, soak.runoff + .025f);
    const uint16 *links = model.Get_Vertex_Bone_Links();
    std::vector<uint8_t> visited(surface.neighbors.size());
    soak.faces.push_back(anchor.triangle);
    visited[anchor.triangle] = 1;
    for (size_t i = 0; i < soak.faces.size() && soak.faces.size() < MaxSoakFaces; ++i) {
        for (int t : surface.neighbors[soak.faces[i]]) {
            if (t < 0 || visited[t]) continue;
            visited[t] = 1;
            const TriIndex &tri = triangles[t];
            if (!soakOnSide(surface, links, tri, side)) continue;
            const Vector3 v[] = {rest[tri[0]], rest[tri[1]], rest[tri[2]]};
            const Vector3 n = Vector3::Cross_Product(v[1] - v[0], v[2] - v[0]);
            if (Vector3::Dot_Product(normal, n) < .15f * n.Length()) continue;
            const Vector3 w = closestWeights(center, v);
            const Vector3 p = v[0] * w.X + v[1] * w.Y + v[2] * w.Z;
            if ((p - center).Length2() > patchReach * patchReach) continue;
            soak.faces.push_back(t);
            if (soak.faces.size() == MaxSoakFaces) break;
        }
    }
    soaks.push_back(std::move(soak));
}

float soakEase(float t)
{
    t = std::clamp(t, 0.f, 1.f);
    return t * t * (3 - 2 * t);
}
