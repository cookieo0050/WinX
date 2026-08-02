#include "texture.h"
#include <iostream>
#include <string>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

using namespace std;

// Helper function to safely join paths without needing C++17 <filesystem>
std::string combinePath(const std::string& dir, const std::string& file) {
    if (dir.empty()) return file;
    char lastChar = dir.back();
    if (lastChar == '/' || lastChar == '\\') {
        return dir + file;
    }
    return dir + "/" + file;
}

void Texture::load(const std::string& path) {
    glGenTextures(1, &m_id);
    glBindTexture(GL_TEXTURE_2D, m_id);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    int channels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path.c_str(), &m_width, &m_height, &channels, 0);

    if (!data) {
        cerr << "Texture failed to load: " << path << " (" << stbi_failure_reason() << ")\n";
        return;
    }

    GLenum format = (channels == 4) ? GL_RGBA : (channels == 1 ? GL_RED : GL_RGB);
    glTexImage2D(GL_TEXTURE_2D, 0, format, m_width, m_height, 0, format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    stbi_image_free(data);
}

void Texture::bind(unsigned int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_id);
}

glm::ivec2 Texture::getImageSize(const std::string& path) {
    int w = 128, h = 128, channels;
    if (stbi_info(path.c_str(), &w, &h, &channels)) {
        return glm::ivec2(w, h);
    }
    return glm::ivec2(128, 128);
}