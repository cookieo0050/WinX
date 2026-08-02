#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>

struct Triangle {
    glm::vec3 v0, v1, v2;
    glm::vec3 normal() const;
};

struct RaycastHit {
    bool hit = false;
    float distance = 0.0f;
    glm::vec3 point{};
    glm::vec3 normal{};
};

struct CollisionContact {
    bool collided = false;
    glm::vec3 normal{ 0.0f, 1.0f, 0.0f };
    float penetration = 0.0f;
};

class CollisionMesh {
public:
    void buildFromVertices(const float* vertices, size_t floatCount, const glm::mat4& modelMatrix = glm::mat4(1.0f));
    RaycastHit raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDistance = 1000.0f) const;

    void resolveSphereCollision(glm::vec3& position, float radius) const;
    CollisionContact resolveSphereCollisionDetailed(glm::vec3& position, float radius) const;

    const std::vector<Triangle>& triangles() const { return m_triangles; }

    void setCellSize(float size) { if (size > 0.1f) m_cellSize = size; }
    float cellSize() const { return m_cellSize; }

private:
    std::vector<Triangle> m_triangles;
    std::unordered_map<int64_t, std::vector<int>> m_grid;
    float m_cellSize = 2.0f;

    void buildGrid();
    void queryCells(const glm::vec3& minB, const glm::vec3& maxB, std::vector<int>& out) const;
    static int64_t cellKey(int cx, int cy, int cz);
};
