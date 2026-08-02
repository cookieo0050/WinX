#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <map>
#include <functional>

struct PointLight {
    glm::vec3 position;
    glm::vec3 color;
    float intensity;
    float radius;
};

struct LevelData {
    std::map<std::string, std::vector<float>> renderChunks;
    std::vector<float> collisionVertices;
    bool hasPlayerStart = false;
    glm::vec3 playerStart{ 0.0f, 1.0f, 0.0f };
    std::vector<PointLight> pointLights;
};

class MapLoader {
public:
    static LevelData load(const std::string& path,
        const std::function<glm::ivec2(const std::string&)>& textureSizeLookup);
};