#pragma once
#include <glad/glad.h>
#include <GLFW/glfw3.h>

class Window {
public:
    Window(int width, int height, const char* title);
    ~Window();

    bool shouldClose() const;
    void swapBuffersAndPollEvents();
    void setCursorDisabled(bool disabled);

    GLFWwindow* handle() const { return m_window; }
    int width() const { return m_width; }
    int height() const { return m_height; }

    void onResize(int width, int height);

private:
    GLFWwindow* m_window = nullptr;
    int m_width, m_height;
};