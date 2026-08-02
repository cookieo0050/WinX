#include "collision.h"
#include <cmath>
#include <algorithm>

glm::vec3 Triangle::normal() const {
    return glm::normalize(glm::cross(v1 - v0, v2 - v0));
}

void CollisionMesh::buildFromVertices(const float* vertices, size_t floatCount, const glm::mat4& modelMatrix) {
    m_triangles.clear();
    size_t vertCount = floatCount / 3;

    for (size_t i = 0; i + 2 < vertCount; i += 3) {
        auto toVec3 = [&](size_t idx) {
            glm::vec4 p(vertices[idx * 3], vertices[idx * 3 + 1], vertices[idx * 3 + 2], 1.0f);
            p = modelMatrix * p;
            return glm::vec3(p);
            };
        Triangle tri;
        tri.v0 = toVec3(i);
        tri.v1 = toVec3(i + 1);
        tri.v2 = toVec3(i + 2);
        m_triangles.push_back(tri);
    }

    buildGrid();
}

int64_t CollisionMesh::cellKey(int cx, int cy, int cz) {
    return (int64_t)(cx & 0x1FFFF) | ((int64_t)(cy & 0x1FFFF) << 17) | ((int64_t)(cz & 0x1FFFF) << 34);
}

void CollisionMesh::buildGrid() {
    m_grid.clear();
    m_grid.reserve(m_triangles.size());

    for (size_t i = 0; i < m_triangles.size(); ++i) {
        const Triangle& tri = m_triangles[i];
        glm::vec3 mn = glm::min(glm::min(tri.v0, tri.v1), tri.v2);
        glm::vec3 mx = glm::max(glm::max(tri.v0, tri.v1), tri.v2);

        int x0 = (int)glm::floor(mn.x / m_cellSize), x1 = (int)glm::floor(mx.x / m_cellSize);
        int y0 = (int)glm::floor(mn.y / m_cellSize), y1 = (int)glm::floor(mx.y / m_cellSize);
        int z0 = (int)glm::floor(mn.z / m_cellSize), z1 = (int)glm::floor(mx.z / m_cellSize);

        for (int cx = x0; cx <= x1; ++cx)
            for (int cy = y0; cy <= y1; ++cy)
                for (int cz = z0; cz <= z1; ++cz)
                    m_grid[cellKey(cx, cy, cz)].push_back((int)i);
    }
}

void CollisionMesh::queryCells(const glm::vec3& minB, const glm::vec3& maxB, std::vector<int>& out) const {
    out.clear();

    int x0 = (int)glm::floor(minB.x / m_cellSize), x1 = (int)glm::floor(maxB.x / m_cellSize);
    int y0 = (int)glm::floor(minB.y / m_cellSize), y1 = (int)glm::floor(maxB.y / m_cellSize);
    int z0 = (int)glm::floor(minB.z / m_cellSize), z1 = (int)glm::floor(maxB.z / m_cellSize);

    for (int cx = x0; cx <= x1; ++cx) {
        int64_t xPart = (int64_t)(cx & 0x1FFFF);
        for (int cy = y0; cy <= y1; ++cy) {
            int64_t yPart = ((int64_t)(cy & 0x1FFFF) << 17);
            for (int cz = z0; cz <= z1; ++cz) {
                auto it = m_grid.find(xPart | yPart | ((int64_t)(cz & 0x1FFFF) << 34));
                if (it != m_grid.end())
                    out.insert(out.end(), it->second.begin(), it->second.end());
            }
        }
    }
}

static bool rayTriangleIntersect(const glm::vec3& orig, const glm::vec3& dir,
    const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
    float& t) {
    const float EPSILON = 1e-6f;
    glm::vec3 edge1 = v1 - v0;
    glm::vec3 edge2 = v2 - v0;
    glm::vec3 h = glm::cross(dir, edge2);
    float a = glm::dot(edge1, h);
    if (fabs(a) < EPSILON) return false;

    float f = 1.0f / a;
    glm::vec3 s = orig - v0;
    float u = f * glm::dot(s, h);
    if (u < 0.0f || u > 1.0f) return false;

    glm::vec3 q = glm::cross(s, edge1);
    float v = f * glm::dot(dir, q);
    if (v < 0.0f || u + v > 1.0f) return false;

    t = f * glm::dot(edge2, q);
    return t > EPSILON;
}

RaycastHit CollisionMesh::raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDistance) const {
    RaycastHit result;

    float dirLen = glm::length(dir);
    if (dirLen < 1e-8f) return result;
    glm::vec3 d = dir / dirLen;

    const float BIG = 1e30f;
    glm::vec3 invD(
        fabsf(d.x) < 1e-9f ? BIG : 1.0f / d.x,
        fabsf(d.y) < 1e-9f ? BIG : 1.0f / d.y,
        fabsf(d.z) < 1e-9f ? BIG : 1.0f / d.z);

    int cx = (int)glm::floor(origin.x / m_cellSize);
    int cy = (int)glm::floor(origin.y / m_cellSize);
    int cz = (int)glm::floor(origin.z / m_cellSize);

    int stepX = d.x > 0 ? 1 : (d.x < 0 ? -1 : 0);
    int stepY = d.y > 0 ? 1 : (d.y < 0 ? -1 : 0);
    int stepZ = d.z > 0 ? 1 : (d.z < 0 ? -1 : 0);

    float tMaxX = stepX == 0 ? BIG : ((stepX > 0 ? (cx + 1) * m_cellSize : cx * m_cellSize) - origin.x) * invD.x;
    float tMaxY = stepY == 0 ? BIG : ((stepY > 0 ? (cy + 1) * m_cellSize : cy * m_cellSize) - origin.y) * invD.y;
    float tMaxZ = stepZ == 0 ? BIG : ((stepZ > 0 ? (cz + 1) * m_cellSize : cz * m_cellSize) - origin.z) * invD.z;

    float tDeltaX = stepX != 0 ? m_cellSize * fabsf(invD.x) : BIG;
    float tDeltaY = stepY != 0 ? m_cellSize * fabsf(invD.y) : BIG;
    float tDeltaZ = stepZ != 0 ? m_cellSize * fabsf(invD.z) : BIG;

    float closest = maxDistance;
    float t = 0.0f;
    int guard = 0;

    while (t <= maxDistance && guard < 100000) {
        if (result.hit && t >= closest) break;

        auto it = m_grid.find(cellKey(cx, cy, cz));
        if (it != m_grid.end()) {
            for (int idx : it->second) {
                const Triangle& tri = m_triangles[idx];
                float hitT;
                if (rayTriangleIntersect(origin, d, tri.v0, tri.v1, tri.v2, hitT) && hitT < closest) {
                    closest = hitT;
                    result.hit = true;
                    result.distance = hitT;
                    result.point = origin + d * hitT;
                    result.normal = tri.normal();
                }
            }
        }

        float tMin;
        if (tMaxX <= tMaxY && tMaxX <= tMaxZ) { tMin = tMaxX; tMaxX += tDeltaX; cx += stepX; }
        else if (tMaxY <= tMaxZ) { tMin = tMaxY; tMaxY += tDeltaY; cy += stepY; }
        else { tMin = tMaxZ; tMaxZ += tDeltaZ; cz += stepZ; }
        t = tMin;
        ++guard;
    }

    return result;
}

static glm::vec3 closestPointOnTriangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    glm::vec3 ab = b - a;
    glm::vec3 ac = c - a;
    glm::vec3 ap = p - a;

    float d1 = glm::dot(ab, ap);
    float d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return a;

    glm::vec3 bp = p - b;
    float d3 = glm::dot(ab, bp);
    float d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return b;

    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        float v = d1 / (d1 - d3);
        return a + v * ab;
    }

    glm::vec3 cp = p - c;
    float d5 = glm::dot(ab, cp);
    float d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return c;

    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        float w = d2 / (d2 - d6);
        return a + w * ac;
    }

    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return b + w * (c - b);
    }

    float denom = 1.0f / (va + vb + vc);
    float v = vb * denom;
    float w = vc * denom;
    return a + ab * v + ac * w;
}

CollisionContact CollisionMesh::resolveSphereCollisionDetailed(glm::vec3& position, float radius) const {
    CollisionContact result;
    const int MAX_PASSES = 3;
    std::vector<int> nearby;

    for (int pass = 0; pass < MAX_PASSES; ++pass) {
        queryCells(position - glm::vec3(radius), position + glm::vec3(radius), nearby);
        if (nearby.empty()) break;

        std::sort(nearby.begin(), nearby.end());
        nearby.erase(std::unique(nearby.begin(), nearby.end()), nearby.end());

        bool pushed = false;
        float maxPen = 0.0f;
        glm::vec3 contactNormal(0.0f);

        for (int idx : nearby) {
            const Triangle& tri = m_triangles[idx];
            glm::vec3 closest = closestPointOnTriangle(position, tri.v0, tri.v1, tri.v2);
            glm::vec3 delta = position - closest;
            float dist = glm::length(delta);

            if (dist < radius) {
                pushed = true;
                glm::vec3 pushDir;
                if (dist > 1e-6f) {
                    pushDir = delta / dist;
                    position += pushDir * (radius - dist);
                    maxPen = glm::max(maxPen, radius - dist);
                }
                else {
                    pushDir = tri.normal();
                    position += pushDir * radius;
                    maxPen = glm::max(maxPen, radius);
                }
                contactNormal += pushDir;
            }
        }

        if (!pushed) break;

        float normalLen = glm::length(contactNormal);
        if (normalLen > 1e-8f) {
            result.collided = true;
            result.normal = contactNormal / normalLen;
            result.penetration = maxPen;
        }
    }

    return result;
}

void CollisionMesh::resolveSphereCollision(glm::vec3& position, float radius) const {
    resolveSphereCollisionDetailed(position, radius);
}
