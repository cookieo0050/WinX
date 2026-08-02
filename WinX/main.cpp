#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include "window.h"
#include "camera.h"
#include "shader.h"
#include "collision.h"
#include "debugdraw.h"
#include "skybox.h"
#include "texture.h"
#include "mapfile.h"
#include "shadow.h"
#include "gbuffer.h"
#include "ssao.h"
#include "ssgi.h"
#include "player.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

using namespace std;

const int MAX_LIGHTS = 16;
const unsigned int SHADOW_CUBE_RES = 1024;

Camera camera;
Player player;
float lastX = 400, lastY = 300;
bool firstMouse = true;
bool mouseLookEnabled = true;

const string TEXTURES_FOLDER = "C:/Users/QuipG/OneDrive/Desktop/WinX_Engine-main/WinX_Engine-main/GameRoot/textures/";
const string MAP_PATH = "C:/Users/QuipG/OneDrive/Desktop/WinX_Engine-main/WinX_Engine-main/Maps/Testroom.map";
const string CROSSHAIR_PATH = "C:/Users/QuipG/OneDrive/Desktop/WinX_Engine-main/WinX_Engine-main/Images/Crosshair.png";

// ==========================================
// SHADER SOURCE CODES
// ==========================================

const char* gbufferVertSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;
out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoord;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
void main() {
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
    TexCoord = aTexCoord;
    gl_Position = projection * view * vec4(FragPos, 1.0);
}
)";

const char* gbufferFragSrc = R"(
#version 330 core
layout (location = 0) out vec3 gPosition;
layout (location = 1) out vec3 gNormal;
layout (location = 2) out vec4 gAlbedoSpec;
in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;
uniform sampler2D texture1;
void main() {
    gPosition = FragPos;
    gNormal = normalize(Normal);
    gAlbedoSpec = vec4(texture(texture1, TexCoord).rgb, 1.0);
}
)";

const char* quadVertSrc = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;
out vec2 TexCoords;
void main() {
    TexCoords = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

// Point Light Shadow Depth Pass Shaders (No Geometry Shader Needed)
const char* pointShadowVertSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 model;
uniform mat4 shadowMatrix;
out vec4 FragPos;
void main() {
    FragPos = model * vec4(aPos, 1.0);
    gl_Position = shadowMatrix * FragPos;
}
)";

const char* pointShadowFragSrc = R"(
#version 330 core
in vec4 FragPos;
uniform vec3 lightPos;
uniform float farPlane;

void main() {
    float lightDistance = length(FragPos.xyz - lightPos);
    lightDistance = lightDistance / farPlane;
    gl_FragDepth = lightDistance;
}
)";

const char* lightingFragSrc = R"(
#version 330 core
out vec4 FragColor;
in vec2 TexCoords;

uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedoSpec;
uniform sampler2D ssao;
uniform sampler2D shadowMapTex;
uniform sampler2D ssgiTex;

#define MAX_LIGHTS 16
uniform samplerCube pointShadowMaps[MAX_LIGHTS];

uniform mat4 lightSpaceMatrix;
uniform vec3 sunDir;
uniform vec3 sunColor;
uniform vec3 viewPos;

uniform vec3 lightPositions[MAX_LIGHTS];
uniform vec3 lightColors[MAX_LIGHTS];
uniform float lightIntensities[MAX_LIGHTS];
uniform float lightRadii[MAX_LIGHTS];
uniform int numLights;

const vec2 poissonDisk[16] = vec2[](
    vec2(-0.94201624, -0.39906216), vec2(0.94558609, -0.76890725),
    vec2(-0.094184101, -0.92938870), vec2(0.34495938, 0.29387760),
    vec2(-0.91588581, 0.45771432), vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543, 0.27676845), vec2(0.97484398, 0.75648379),
    vec2(0.44323325, -0.97511554), vec2(0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023), vec2(0.79197514, 0.19090188),
    vec2(-0.24188840, 0.99706507), vec2(-0.81409955, 0.91437590),
    vec2(0.19984126, 0.78641367), vec2(0.14383161, -0.14100790)
);

vec3 computeRadiosityNormalMapping(vec3 normal, vec3 indirectColor) {
    vec3 w = vec3(0.333, 0.333, 0.333); 
    vec3 directionalBounce = indirectColor * (0.5 + 0.5 * normal);
    return mix(indirectColor, directionalBounce, 0.5);
}

float computeSunShadow(vec3 fragPos, vec3 normal, vec3 lightDir) {
    vec4 fragPosLightSpace = lightSpaceMatrix * vec4(fragPos + normal * 0.005, 1.0);
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    if (projCoords.z > 1.0 || projCoords.z < 0.0) return 0.0;

    float bias = max(0.001 * (1.0 - dot(normal, lightDir)), 0.0001);
    float shadow = 0.0;
    float softRadius = 1.0; 
    vec2 texelSize = 1.0 / textureSize(shadowMapTex, 0);

    for (int i = 0; i < 16; i++) {
        vec2 offset = poissonDisk[i] * texelSize * softRadius;
        float pcfDepth = texture(shadowMapTex, projCoords.xy + offset).r;
        shadow += (projCoords.z - bias > pcfDepth) ? 1.0 : 0.0;
    }
    return shadow / 16.0;
}

float computePointShadow(int index, vec3 fragPos, vec3 lightPos, float farPlane, vec3 normal) {
    vec3 fragToLight = fragPos - lightPos;
    float currentDepth = length(fragToLight);

    if (currentDepth >= farPlane) return 0.0;

    vec3 lightDir = normalize(-fragToLight);
    float bias = max(0.05 * (1.0 - dot(normal, lightDir)), 0.005);

    float closestDepth = 0.0;
    if (index == 0) closestDepth = texture(pointShadowMaps[0], fragToLight).r;
    else if (index == 1) closestDepth = texture(pointShadowMaps[1], fragToLight).r;
    else if (index == 2) closestDepth = texture(pointShadowMaps[2], fragToLight).r;
    else if (index == 3) closestDepth = texture(pointShadowMaps[3], fragToLight).r;
    else if (index == 4) closestDepth = texture(pointShadowMaps[4], fragToLight).r;
    else if (index == 5) closestDepth = texture(pointShadowMaps[5], fragToLight).r;
    else if (index == 6) closestDepth = texture(pointShadowMaps[6], fragToLight).r;
    else if (index == 7) closestDepth = texture(pointShadowMaps[7], fragToLight).r;
    else if (index == 8) closestDepth = texture(pointShadowMaps[8], fragToLight).r;
    else if (index == 9) closestDepth = texture(pointShadowMaps[9], fragToLight).r;
    else if (index == 10) closestDepth = texture(pointShadowMaps[10], fragToLight).r;
    else if (index == 11) closestDepth = texture(pointShadowMaps[11], fragToLight).r;
    else if (index == 12) closestDepth = texture(pointShadowMaps[12], fragToLight).r;
    else if (index == 13) closestDepth = texture(pointShadowMaps[13], fragToLight).r;
    else if (index == 14) closestDepth = texture(pointShadowMaps[14], fragToLight).r;
    else if (index == 15) closestDepth = texture(pointShadowMaps[15], fragToLight).r;

    closestDepth *= farPlane;
    return (currentDepth - bias > closestDepth) ? 1.0 : 0.0;
}

void main() {
    vec4 albedoSample = texture(gAlbedoSpec, TexCoords);
    if (albedoSample.a < 0.5) discard;

    vec3 FragPos = texture(gPosition, TexCoords).rgb;
    vec3 Normal = normalize(texture(gNormal, TexCoords).rgb);
    vec3 texColor = albedoSample.rgb;
    float ao = texture(ssao, TexCoords).r;

    float ambientStrength = 0.15;
    vec3 ambient = ambientStrength * sunColor * ao;

    vec3 sunLightDir = normalize(-sunDir);
    float sunDiff = max(dot(Normal, sunLightDir), 0.0);
    vec3 sunDiffuse = sunDiff * sunColor;

    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 sunReflect = reflect(-sunLightDir, Normal);
    float sunSpec = pow(max(dot(viewDir, sunReflect), 0.0), 32.0);
    vec3 sunSpecular = 0.5 * sunSpec * sunColor;

    float sunShadow = computeSunShadow(FragPos, Normal, sunLightDir);
    vec3 sunContribution = (1.0 - sunShadow) * (sunDiffuse + sunSpecular);

    vec3 pointContribution = vec3(0.0);
    for (int i = 0; i < numLights; i++) {
        vec3 toLight = lightPositions[i] - FragPos;
        float dist = length(toLight);
        vec3 lightDir = toLight / max(dist, 0.0001);

        float diff = max(dot(Normal, lightDir), 0.0);
        float atten = clamp(1.0 - (dist / lightRadii[i]), 0.0, 1.0);
        atten = atten * atten;

        float pShadow = computePointShadow(i, FragPos, lightPositions[i], lightRadii[i], Normal);
        pointContribution += (1.0 - pShadow) * diff * lightColors[i] * lightIntensities[i] * atten;
    }
    pointContribution *= ao;

    vec3 rawIndirect = texture(ssgiTex, TexCoords).rgb * texColor;
    vec3 indirectLight = computeRadiosityNormalMapping(Normal, rawIndirect);

    vec3 result = (ambient + sunContribution + pointContribution) * texColor + indirectLight;
    FragColor = vec4(result, 1.0);
}
)";

const char* debugViewFragSrc = R"(
#version 330 core
out vec4 FragColor;
in vec2 TexCoords;
uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedoSpec;
uniform sampler2D ssao;
uniform sampler2D ssgiTex;
uniform int mode;

void main() {
    if (mode == 1) FragColor = vec4(texture(gPosition, TexCoords).rgb * 0.05 + 0.5, 1.0);
    else if (mode == 2) FragColor = vec4(texture(gNormal, TexCoords).rgb * 0.5 + 0.5, 1.0);
    else if (mode == 3) FragColor = vec4(texture(gAlbedoSpec, TexCoords).rgb, 1.0);
    else if (mode == 4) { float a = texture(ssao, TexCoords).r; FragColor = vec4(vec3(a), 1.0); }
    else if (mode == 5) FragColor = vec4(texture(ssgiTex, TexCoords).rgb, 1.0);
    else FragColor = vec4(0.0);
}
)";

const char* crosshairVertSrc = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;
out vec2 TexCoords;
void main() {
    TexCoords = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* crosshairFragSrc = R"(
#version 330 core
out vec4 FragColor;
in vec2 TexCoords;
uniform sampler2D crosshairTex;
void main() {
    FragColor = texture(crosshairTex, TexCoords);
}
)";

void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (!mouseLookEnabled) { firstMouse = true; return; }
    if (firstMouse) {
        lastX = (float)xpos; lastY = (float)ypos;
        firstMouse = false;
    }
    float xoffset = (float)xpos - lastX;
    float yoffset = lastY - (float)ypos;
    lastX = (float)xpos; lastY = (float)ypos;
    camera.processMouseMovement(xoffset, yoffset);
}

struct LevelChunk {
    Texture texture;
    GLuint VAO = 0, VBO = 0;
    int vertexCount = 0;
};

int main() {
    Window window(800, 600, "WinX Engine");
    glfwSetCursorPosCallback(window.handle(), mouse_callback);
    window.setCursorDisabled(true);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window.handle(), true);
    ImGui_ImplOpenGL3_Init("#version 330");

    Shader gbufferShader(gbufferVertSrc, gbufferFragSrc);
    Shader lightingShader(quadVertSrc, lightingFragSrc);
    Shader debugViewShader(quadVertSrc, debugViewFragSrc);
    Shader crosshairShader(crosshairVertSrc, crosshairFragSrc);

    // Shader now only takes 2 arguments as expected
    Shader pointShadowShader(pointShadowVertSrc, pointShadowFragSrc);

    auto textureSizeLookup = [](const string& texName) -> glm::ivec2 {
        string path = TEXTURES_FOLDER + texName + ".png";
        return Texture::getImageSize(path);
        };

    LevelData level = MapLoader::load(MAP_PATH, textureSizeLookup);
    cout << "Loaded map: " << level.renderChunks.size() << " texture chunks, "
        << (level.collisionVertices.size() / 9) << " collision triangles, "
        << level.pointLights.size() << " point lights\n";

    glm::vec3 sceneMin(1e9f), sceneMax(-1e9f);
    for (size_t i = 0; i + 2 < level.collisionVertices.size(); i += 3) {
        glm::vec3 p(level.collisionVertices[i], level.collisionVertices[i + 1], level.collisionVertices[i + 2]);
        sceneMin = glm::min(sceneMin, p);
        sceneMax = glm::max(sceneMax, p);
    }
    glm::vec3 sceneCenter = (sceneMin + sceneMax) * 0.5f;
    float sceneRadius = glm::length(sceneMax - sceneMin) * 0.5f;
    if (sceneRadius < 1.0f) sceneRadius = 20.0f;

    vector<LevelChunk> chunks;
    for (auto& pair : level.renderChunks) {
        const string& texName = pair.first;
        vector<float>& data = pair.second;
        if (data.empty()) continue;

        LevelChunk chunk;
        chunk.texture.load(TEXTURES_FOLDER + texName + ".png");
        chunk.vertexCount = (int)(data.size() / 8);

        glGenVertexArrays(1, &chunk.VAO);
        glGenBuffers(1, &chunk.VBO);
        glBindVertexArray(chunk.VAO);
        glBindBuffer(GL_ARRAY_BUFFER, chunk.VBO);
        glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(2);

        chunks.push_back(chunk);
    }

    CollisionMesh collisionMesh;
    collisionMesh.buildFromVertices(level.collisionVertices.data(), level.collisionVertices.size());

    if (level.hasPlayerStart) {
        player.m_Position = level.playerStart;
    }

    DebugDraw debugDraw; debugDraw.init();
    Skybox skybox; skybox.init("C:/Users/QuipG/OneDrive/Desktop/WinX_Engine-main/WinX_Engine-main/Images/cubemap_3.png");
    ShadowMap shadowMap; shadowMap.init(2048);
    GBuffer gBuffer; gBuffer.init(window.width(), window.height());
    SSAO ssao; ssao.init(window.width(), window.height());
    SSGI ssgi; ssgi.init(window.width(), window.height());

    GLuint pointShadowFBO;
    glGenFramebuffers(1, &pointShadowFBO);

    int activePointLights = min((int)level.pointLights.size(), MAX_LIGHTS);
    GLuint pointShadowCubemaps[MAX_LIGHTS];
    glGenTextures(activePointLights, pointShadowCubemaps);

    for (int i = 0; i < activePointLights; ++i) {
        glBindTexture(GL_TEXTURE_CUBE_MAP, pointShadowCubemaps[i]);
        for (unsigned int f = 0; f < 6; ++f) {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_DEPTH_COMPONENT,
                SHADOW_CUBE_RES, SHADOW_CUBE_RES, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        }
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    }

    float quadVertices[] = {
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f
    };
    GLuint quadVAO, quadVBO;
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    Texture crosshairTexture;
    crosshairTexture.load(CROSSHAIR_PATH);
    float crosshairSizePixels = 3.0f;

    GLuint crosshairVAO, crosshairVBO;
    glGenVertexArrays(1, &crosshairVAO);
    glGenBuffers(1, &crosshairVBO);
    glBindVertexArray(crosshairVAO);
    glBindBuffer(GL_ARRAY_BUFFER, crosshairVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 24, nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glm::vec3 sunDir = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
    glm::vec3 sunColor(1.0f, 0.95f, 0.85f);
    int debugViewMode = 0;

    bool wireframe = false;
    bool tKeyWasDown = false;
    bool tildeWasDown = false;
    bool showDebugPanel = true;

    float deltaTime = 0.0f, lastFrame = 0.0f;
    float fpsTimer = 0.0f;
    int frameCount = 0;
    float displayedFps = 0.0f;

    const int FRAME_HISTORY = 120;
    vector<float> frameTimeHistory(FRAME_HISTORY, 0.0f);
    int frameHistoryIdx = 0;

    while (!window.shouldClose()) {
        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        frameTimeHistory[frameHistoryIdx] = deltaTime * 1000.0f;
        frameHistoryIdx = (frameHistoryIdx + 1) % FRAME_HISTORY;

        frameCount++;
        fpsTimer += deltaTime;
        if (fpsTimer >= 1.0f) {
            displayedFps = frameCount / fpsTimer;
            frameCount = 0;
            fpsTimer = 0.0f;
        }

        if (glfwGetKey(window.handle(), GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window.handle(), true);

        bool tildeDown = glfwGetKey(window.handle(), GLFW_KEY_GRAVE_ACCENT) == GLFW_PRESS;
        if (tildeDown && !tildeWasDown) {
            showDebugPanel = !showDebugPanel;
            mouseLookEnabled = !showDebugPanel;
            window.setCursorDisabled(mouseLookEnabled);
        }
        tildeWasDown = tildeDown;

        bool tDown = glfwGetKey(window.handle(), GLFW_KEY_T) == GLFW_PRESS;
        if (tDown && !tKeyWasDown) wireframe = !wireframe;
        tKeyWasDown = tDown;

        if (mouseLookEnabled) {
            player.update(window.handle(), collisionMesh, deltaTime, camera);
        }
        else {
            camera.position = player.m_Position + glm::vec3(0.0f, player.eyeHeight(), 0.0f);
        }

        glm::mat4 view = camera.getViewMatrix();
        glm::mat4 projection = glm::perspective(glm::radians(75.0f),
            (float)window.width() / (float)window.height(), 0.1f, 100.0f);
        glm::mat4 lightSpaceMatrix = shadowMap.getLightSpaceMatrix(sunDir, sceneCenter, sceneRadius);
        glm::mat4 identityModel = glm::mat4(1.0f);

        // 1. Sun Shadow Pass
        shadowMap.beginRender();
        shadowMap.depthShader()->use();
        shadowMap.depthShader()->setMat4("lightSpaceMatrix", lightSpaceMatrix);
        shadowMap.depthShader()->setMat4("model", identityModel);
        glCullFace(GL_FRONT);
        for (auto& chunk : chunks) {
            glBindVertexArray(chunk.VAO);
            glDrawArrays(GL_TRIANGLES, 0, chunk.vertexCount);
        }
        glCullFace(GL_BACK);
        shadowMap.endRender(window.width(), window.height());

        // 2. OPTIMIZED: Point Light Depth Pass (Cached so it runs ONLY ONCE)
        static bool pointShadowsInitialized = false;
        if (!pointShadowsInitialized) {
            glViewport(0, 0, SHADOW_CUBE_RES, SHADOW_CUBE_RES);
            glBindFramebuffer(GL_FRAMEBUFFER, pointShadowFBO);
            pointShadowShader.use();

            glEnable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);

            float aspect = 1.0f;
            float nearPlane = 0.1f;

            for (int i = 0; i < activePointLights; ++i) {
                float farPlane = level.pointLights[i].radius;
                glm::mat4 shadowProj = glm::perspective(glm::radians(90.0f), aspect, nearPlane, farPlane);
                glm::vec3 pPos = level.pointLights[i].position;

                vector<glm::mat4> shadowTransforms;
                shadowTransforms.push_back(shadowProj * glm::lookAt(pPos, pPos + glm::vec3(1, 0, 0), glm::vec3(0, -1, 0)));
                shadowTransforms.push_back(shadowProj * glm::lookAt(pPos, pPos + glm::vec3(-1, 0, 0), glm::vec3(0, -1, 0)));
                shadowTransforms.push_back(shadowProj * glm::lookAt(pPos, pPos + glm::vec3(0, 1, 0), glm::vec3(0, 0, 1)));
                shadowTransforms.push_back(shadowProj * glm::lookAt(pPos, pPos + glm::vec3(0, -1, 0), glm::vec3(0, 0, -1)));
                shadowTransforms.push_back(shadowProj * glm::lookAt(pPos, pPos + glm::vec3(0, 0, 1), glm::vec3(0, -1, 0)));
                shadowTransforms.push_back(shadowProj * glm::lookAt(pPos, pPos + glm::vec3(0, 0, -1), glm::vec3(0, -1, 0)));

                pointShadowShader.setFloat("farPlane", farPlane);
                pointShadowShader.setVec3("lightPos", pPos);
                pointShadowShader.setMat4("model", identityModel);

                for (int f = 0; f < 6; ++f) {
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                        GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, pointShadowCubemaps[i], 0);
                    glClear(GL_DEPTH_BUFFER_BIT);

                    pointShadowShader.setMat4("shadowMatrix", shadowTransforms[f]);

                    for (auto& chunk : chunks) {
                        glBindVertexArray(chunk.VAO);
                        glDrawArrays(GL_TRIANGLES, 0, chunk.vertexCount);
                    }
                }
            }
            pointShadowsInitialized = true;
        }

        // Restore standard culling
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        // 3. G-Buffer Geometry Pass
        glViewport(0, 0, window.width(), window.height());
        glEnable(GL_DEPTH_TEST);
        glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
        gBuffer.bindForWriting();
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        gbufferShader.use();
        gbufferShader.setMat4("model", identityModel);
        gbufferShader.setMat4("view", view);
        gbufferShader.setMat4("projection", projection);
        gbufferShader.setInt("texture1", 0);
        for (auto& chunk : chunks) {
            chunk.texture.bind(0);
            glBindVertexArray(chunk.VAO);
            glDrawArrays(GL_TRIANGLES, 0, chunk.vertexCount);
        }
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        // 4. SSAO & SSGI Pass
        glDisable(GL_DEPTH_TEST);
        ssao.renderSSAO(gBuffer.positionTex(), gBuffer.normalTex(), view, projection, quadVAO);
        ssao.blur(quadVAO);

        ssgi.render(gBuffer.positionTex(), gBuffer.normalTex(), gBuffer.albedoSpecTex(), view, projection, quadVAO);
        ssgi.blur(quadVAO);

        // 5. Blit Depth Buffer
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, window.width(), window.height());
        glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, gBuffer.getFBO());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, gBuffer.width(), gBuffer.height(),
            0, 0, window.width(), window.height(),
            GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // 6. Deferred Lighting Pass
        if (debugViewMode == 0) {
            lightingShader.use();
            lightingShader.setInt("gPosition", 0);
            lightingShader.setInt("gNormal", 1);
            lightingShader.setInt("gAlbedoSpec", 2);
            lightingShader.setInt("ssao", 3);
            lightingShader.setInt("shadowMapTex", 4);
            lightingShader.setInt("ssgiTex", 5);

            lightingShader.setMat4("lightSpaceMatrix", lightSpaceMatrix);
            lightingShader.setVec3("sunDir", sunDir);
            lightingShader.setVec3("sunColor", sunColor);
            lightingShader.setVec3("viewPos", camera.position);

            lightingShader.setInt("numLights", activePointLights);
            for (int i = 0; i < activePointLights; i++) {
                string idx = to_string(i);
                lightingShader.setVec3("lightPositions[" + idx + "]", level.pointLights[i].position);
                lightingShader.setVec3("lightColors[" + idx + "]", level.pointLights[i].color);
                lightingShader.setFloat("lightIntensities[" + idx + "]", level.pointLights[i].intensity);
                lightingShader.setFloat("lightRadii[" + idx + "]", level.pointLights[i].radius);

                glActiveTexture(GL_TEXTURE6 + i);
                glBindTexture(GL_TEXTURE_CUBE_MAP, pointShadowCubemaps[i]);
                lightingShader.setInt("pointShadowMaps[" + idx + "]", 6 + i);
            }

            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, gBuffer.positionTex());
            glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, gBuffer.normalTex());
            glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, gBuffer.albedoSpecTex());
            glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, ssao.resultTexture());
            glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, shadowMap.depthTexture());
            glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_2D, ssgi.resultTexture());

            glBindVertexArray(quadVAO);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
        else {
            debugViewShader.use();
            debugViewShader.setInt("gPosition", 0);
            debugViewShader.setInt("gNormal", 1);
            debugViewShader.setInt("gAlbedoSpec", 2);
            debugViewShader.setInt("ssao", 3);
            debugViewShader.setInt("ssgiTex", 4);
            debugViewShader.setInt("mode", debugViewMode);

            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, gBuffer.positionTex());
            glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, gBuffer.normalTex());
            glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, gBuffer.albedoSpecTex());
            glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, ssao.resultTexture());
            glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, ssgi.resultTexture());

            glBindVertexArray(quadVAO);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }

        // 7. Skybox Pass
        glEnable(GL_DEPTH_TEST);
        skybox.draw(view, projection);

        // 8. Crosshair Overlay Pass
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        float chHalfX = crosshairSizePixels / (float)window.width();
        float chHalfY = crosshairSizePixels / (float)window.height();
        float crosshairVertices[] = {
            -chHalfX,  chHalfY,  0.0f, 1.0f,
            -chHalfX, -chHalfY,  0.0f, 0.0f,
             chHalfX, -chHalfY,  1.0f, 0.0f,
            -chHalfX,  chHalfY,  0.0f, 1.0f,
             chHalfX, -chHalfY,  1.0f, 0.0f,
             chHalfX,  chHalfY,  1.0f, 1.0f
        };
        glBindBuffer(GL_ARRAY_BUFFER, crosshairVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(crosshairVertices), crosshairVertices);

        crosshairShader.use();
        crosshairShader.setInt("crosshairTex", 0);
        crosshairTexture.bind(0);
        glBindVertexArray(crosshairVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);

        // 9. ImGui Debug Overlay Pass
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (showDebugPanel) {
            ImGui::Begin("Debug Control Center");

            ImGui::Text("FPS: %.1f (%.2f ms)", displayedFps, deltaTime * 1000.0f);
            ImGui::PlotLines("Frame time (ms)", frameTimeHistory.data(), FRAME_HISTORY, frameHistoryIdx,
                nullptr, 0.0f, 33.0f, ImVec2(0, 60));

            ImGui::Separator();
            ImGui::Text("Player Controls & Physics");
            ImGui::Text("Position: (%.2f, %.2f, %.2f)", player.m_Position.x, player.m_Position.y, player.m_Position.z);
            ImGui::Text("Velocity: (%.2f, %.2f, %.2f)", player.m_Velocity.x, player.m_Velocity.y, player.m_Velocity.z);
            ImGui::Text("Grounded: %s", player.m_IsGrounded ? "Yes" : "No");
            ImGui::Text("Crouching: %s", player.isCrouching() ? "Yes" : "No");

            ImGui::Separator();
            ImGui::Text("Level Info");
            ImGui::Text("Texture chunks: %d", (int)chunks.size());
            ImGui::Text("Collision triangles: %d", (int)collisionMesh.triangles().size());
            ImGui::Text("Active Point Lights: %d", activePointLights);

            ImGui::Separator();
            ImGui::Text("Debug Display Mode");
            const char* modes[] = { "Final", "Position", "Normal", "Albedo", "SSAO only", "SSGI only" };
            ImGui::Combo("View mode", &debugViewMode, modes, 6);

            ImGui::Separator();
            ImGui::Text("Sun Light Settings");
            ImGui::SliderFloat3("Sun Direction", &sunDir.x, -1.0f, 1.0f);
            ImGui::ColorEdit3("Sun Color", &sunColor.x);

            ImGui::Separator();
            ImGui::Checkbox("Wireframe (T)", &wireframe);
            ImGui::Text("Crouch: Left Ctrl / C");
            ImGui::Text("Toggle Panel / Mouse: ` (tilde)");

            ImGui::End();
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        window.swapBuffersAndPollEvents();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glDeleteFramebuffers(1, &pointShadowFBO);
    glDeleteTextures(activePointLights, pointShadowCubemaps);

    for (auto& chunk : chunks) {
        glDeleteVertexArrays(1, &chunk.VAO);
        glDeleteBuffers(1, &chunk.VBO);
    }
    return 0;
}