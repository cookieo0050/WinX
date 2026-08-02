#include "mapfile.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <iostream>
using namespace std;

struct Plane {
    glm::vec3 p0, p1, p2;
    glm::vec3 normal;
    float distance = 0.0f;
    string texture;
    glm::vec3 uAxis, vAxis;
    float uOffset = 0, vOffset = 0;
    float rotation = 0, scaleX = 1, scaleY = 1;
};

struct FaceGeom {
    string texture;
    glm::vec3 normal;
    vector<glm::vec3> polyVerts;
    int planeIndex = -1;
};

struct Brush {
    vector<Plane> planes;
    vector<FaceGeom> faces;
};

struct Entity {
    map<string, string> properties;
    vector<Brush> brushes;
};

static const float MAP_SCALE = 1.0f / 32.0f;

static glm::vec3 convertPos(const glm::vec3& p) {
    return glm::vec3(p.x, p.z, -p.y) * MAP_SCALE;
}
static glm::vec3 convertDir(const glm::vec3& n) {
    return glm::normalize(glm::vec3(n.x, n.z, -n.y));
}

static string trim(const string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static Plane parsePlaneLine(const string& line) {
    istringstream iss(line);
    string tmp;
    Plane pl;

    iss >> tmp >> pl.p0.x >> pl.p0.y >> pl.p0.z >> tmp;
    iss >> tmp >> pl.p1.x >> pl.p1.y >> pl.p1.z >> tmp;
    iss >> tmp >> pl.p2.x >> pl.p2.y >> pl.p2.z >> tmp;
    iss >> pl.texture;
    iss >> tmp >> pl.uAxis.x >> pl.uAxis.y >> pl.uAxis.z >> pl.uOffset >> tmp;
    iss >> tmp >> pl.vAxis.x >> pl.vAxis.y >> pl.vAxis.z >> pl.vOffset >> tmp;
    iss >> pl.rotation >> pl.scaleX >> pl.scaleY;

    pl.normal = glm::normalize(glm::cross(pl.p2 - pl.p0, pl.p1 - pl.p0));
    pl.distance = glm::dot(pl.normal, pl.p0);

    if (pl.scaleX == 0) pl.scaleX = 1;
    if (pl.scaleY == 0) pl.scaleY = 1;

    return pl;
}

static vector<Entity> parseEntities(const string& path) {
    ifstream file(path);
    vector<Entity> entities;
    if (!file.is_open()) {
        cerr << "MapLoader: could not open " << path << "\n";
        return entities;
    }

    int depth = 0;
    Entity currentEntity;
    Brush currentBrush;
    string rawLine;

    while (getline(file, rawLine)) {
        string line = trim(rawLine);
        if (line.empty() || line.rfind("//", 0) == 0) continue;

        if (line == "{") {
            depth++;
            if (depth == 1) currentEntity = Entity();
            else if (depth == 2) currentBrush = Brush();
            continue;
        }
        if (line == "}") {
            if (depth == 2) {
                currentEntity.brushes.push_back(currentBrush);
            }
            else if (depth == 1) {
                entities.push_back(currentEntity);
            }
            depth--;
            continue;
        }

        if (depth == 1) {
            size_t firstQuote = line.find('"');
            size_t secondQuote = line.find('"', firstQuote + 1);
            size_t thirdQuote = line.find('"', secondQuote + 1);
            size_t fourthQuote = line.find('"', thirdQuote + 1);
            if (firstQuote != string::npos && fourthQuote != string::npos) {
                string key = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
                string value = line.substr(thirdQuote + 1, fourthQuote - thirdQuote - 1);
                currentEntity.properties[key] = value;
            }
        }
        else if (depth == 2) {
            currentBrush.planes.push_back(parsePlaneLine(line));
        }
    }

    return entities;
}

static void generateBrushGeometry(Brush& brush) {
    size_t n = brush.planes.size();
    vector<vector<glm::vec3>> faceVerts(n);
    const float EPS = 0.01f;

    for (size_t i = 0; i < n; i++)
        for (size_t j = i + 1; j < n; j++)
            for (size_t k = j + 1; k < n; k++) {
                glm::mat3 Mt(brush.planes[i].normal, brush.planes[j].normal, brush.planes[k].normal);
                glm::mat3 M = glm::transpose(Mt);
                float det = glm::determinant(M);
                if (fabs(det) < 1e-6f) continue;

                glm::vec3 d(brush.planes[i].distance, brush.planes[j].distance, brush.planes[k].distance);
                glm::vec3 p = glm::inverse(M) * d;

                bool valid = true;
                for (size_t m = 0; m < n; m++) {
                    if (glm::dot(brush.planes[m].normal, p) > brush.planes[m].distance + EPS) {
                        valid = false;
                        break;
                    }
                }
                if (!valid) continue;

                for (size_t face : { i, j, k }) {
                    auto& verts = faceVerts[face];
                    bool dup = false;
                    for (auto& v : verts) {
                        if (glm::distance(v, p) < 0.02f) { dup = true; break; }
                    }
                    if (!dup) verts.push_back(p);
                }
            }

    for (size_t i = 0; i < n; i++) {
        auto& verts = faceVerts[i];
        if (verts.size() < 3) continue;

        glm::vec3 centroid(0.0f);
        for (auto& v : verts) centroid += v;
        centroid /= (float)verts.size();

        glm::vec3 normal = brush.planes[i].normal;
        glm::vec3 u = glm::normalize(verts[0] - centroid);
        glm::vec3 v = glm::cross(normal, u);

        sort(verts.begin(), verts.end(), [&](const glm::vec3& a, const glm::vec3& b) {
            float angA = atan2(glm::dot(a - centroid, v), glm::dot(a - centroid, u));
            float angB = atan2(glm::dot(b - centroid, v), glm::dot(b - centroid, u));
            return angA < angB;
            });

        FaceGeom fg;
        fg.texture = brush.planes[i].texture;
        fg.normal = normal;
        fg.polyVerts = verts;
        fg.planeIndex = (int)i;
        brush.faces.push_back(fg);
    }
}

static void emitFace(const FaceGeom& face, const Plane& plane, glm::ivec2 texSize, LevelData& level) {
    if (face.polyVerts.size() < 3) return;
    float texW = (float)(texSize.x > 0 ? texSize.x : 128);
    float texH = (float)(texSize.y > 0 ? texSize.y : 128);

    auto& renderBuf = level.renderChunks[face.texture];

    auto emitVertex = [&](const glm::vec3& mapPos) {
        float u = (glm::dot(mapPos, plane.uAxis) / plane.scaleX + plane.uOffset) / texW;
        float v = (glm::dot(mapPos, plane.vAxis) / plane.scaleY + plane.vOffset) / texH;

        glm::vec3 enginePos = convertPos(mapPos);
        glm::vec3 engineNormal = convertDir(face.normal);

        renderBuf.push_back(enginePos.x); renderBuf.push_back(enginePos.y); renderBuf.push_back(enginePos.z);
        renderBuf.push_back(engineNormal.x); renderBuf.push_back(engineNormal.y); renderBuf.push_back(engineNormal.z);
        renderBuf.push_back(u); renderBuf.push_back(v);

        level.collisionVertices.push_back(enginePos.x);
        level.collisionVertices.push_back(enginePos.y);
        level.collisionVertices.push_back(enginePos.z);
        };

    for (size_t k = 1; k + 1 < face.polyVerts.size(); k++) {
        emitVertex(face.polyVerts[0]);
        emitVertex(face.polyVerts[k]);
        emitVertex(face.polyVerts[k + 1]);
    }
}

LevelData MapLoader::load(const string& path, const function<glm::ivec2(const string&)>& textureSizeLookup) {
    LevelData level;
    vector<Entity> entities = parseEntities(path);

    for (auto& entity : entities) {
        auto classIt = entity.properties.find("classname");
        if (classIt == entity.properties.end()) continue;

        if (classIt->second == "info_player_start") {
            auto originIt = entity.properties.find("origin");
            if (originIt != entity.properties.end()) {
                istringstream iss(originIt->second);
                glm::vec3 mapOrigin;
                iss >> mapOrigin.x >> mapOrigin.y >> mapOrigin.z;
                level.playerStart = convertPos(mapOrigin);
                level.hasPlayerStart = true;
            }
        }
        else if (classIt->second == "light") {
            PointLight light;
            light.color = glm::vec3(1.0f);
            light.intensity = 1.0f;
            light.radius = 6.0f;

            auto originIt = entity.properties.find("origin");
            if (originIt != entity.properties.end()) {
                istringstream iss(originIt->second);
                glm::vec3 mapOrigin;
                iss >> mapOrigin.x >> mapOrigin.y >> mapOrigin.z;
                light.position = convertPos(mapOrigin);
            }
            auto colorIt = entity.properties.find("color");
            if (colorIt != entity.properties.end()) {
                istringstream iss(colorIt->second);
                iss >> light.color.x >> light.color.y >> light.color.z;
            }
            auto intensityIt = entity.properties.find("intensity");
            if (intensityIt != entity.properties.end()) {
                light.intensity = stof(intensityIt->second);
            }
            auto radiusIt = entity.properties.find("radius");
            if (radiusIt != entity.properties.end()) {
                light.radius = stof(radiusIt->second) * MAP_SCALE;
            }

            level.pointLights.push_back(light);
        }

        for (auto& brush : entity.brushes) {
            generateBrushGeometry(brush);
            for (auto& face : brush.faces) {
                glm::ivec2 texSize = textureSizeLookup(face.texture);
                emitFace(face, brush.planes[face.planeIndex], texSize, level);
            }
        }
    }

    return level;
}