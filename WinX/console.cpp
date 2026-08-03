#include "console.h"
#include "imgui.h"
#include <algorithm>
#include <sstream>

Console g_Console;

namespace {
    // Ignore backticks/tildes typed into the prompt so pressing the toggle key never
    // pollutes the command line.
    int consoleCharFilter(ImGuiInputTextCallbackData* data) {
        if (data->EventChar == '`' || data->EventChar == '~')
            return 1;
        return 0;
    }
}

Console::Console() {
    registerCommand("help", [this](const std::vector<std::string>&) {
        log("Available commands:");
        for (const auto& c : m_commands)
            log("  " + c.first);
    });
    registerCommand("clear", [this](const std::vector<std::string>&) {
        clear();
    });
    registerCommand("echo", [](const std::vector<std::string>& args) {
        std::string line;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i) line += " ";
            line += args[i];
        }
        g_Console.log(line);
    });
    registerCommand("quit", [this](const std::vector<std::string>&) {
        if (m_quit) m_quit();
    });
}

void Console::toggle() {
    if (m_open) close(); else open();
}

void Console::open() {
    if (m_open) return;
    m_open = true;
    m_justOpened = true;
    m_autoScroll = true;
    m_input[0] = '\0';
}

void Console::close() {
    m_open = false;
}

void Console::log(const std::string& line) {
    m_lines.push_back(line);
    while (m_lines.size() > m_maxLines)
        m_lines.pop_front();
    m_autoScroll = true;
}

void Console::logError(const std::string& line) {
    log("ERROR: " + line);
}

void Console::clear() {
    m_lines.clear();
    m_autoScroll = true;
}

void Console::registerCommand(const std::string& name, CommandFn fn) {
    m_commands.push_back({ name, std::move(fn) });
}

void Console::setQuitCallback(std::function<void()> quit) {
    m_quit = std::move(quit);
}

void Console::execute(const std::string& line) {
    if (line.empty()) return;

    log("> " + line);

    std::istringstream ss(line);
    std::string cmd;
    ss >> cmd;
    if (cmd.empty()) return;

    std::vector<std::string> args;
    std::string arg;
    while (ss >> arg) args.push_back(arg);

    for (const auto& c : m_commands)
        if (c.first == cmd) {
            c.second(args);
            return;
        }
    log("Unknown command: \"" + cmd + "\" (type 'help' for a list)");
}

void Console::draw(float screenWidth, float screenHeight) {
    if (!m_open) return;

    const float height = std::min(240.0f, screenHeight * 0.5f);

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(screenWidth, height));
    ImGui::SetNextWindowBgAlpha(0.92f);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    // Semi-transparent white bar, dark text.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.10f, 0.10f, 0.10f, 1.0f));

    if (ImGui::Begin("##EngineConsole", nullptr, flags)) {
        const float inputHeight = ImGui::GetFrameHeight();

        // Scrollable log area printed line by line.
        ImGui::BeginChild("##ConsoleLog", ImVec2(0.0f, -inputHeight - 8.0f), false,
            ImGuiWindowFlags_AlwaysVerticalScrollbar);
        {
            const bool atBottom = ImGui::GetScrollMaxY() < 1.0f
                || ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
            for (const auto& line : m_lines)
                ImGui::TextUnformatted(line.c_str());
            if (m_autoScroll && atBottom)
                ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();

        // Prompt line: "> " caret plus the active text entry.
        ImGui::TextUnformatted("> ");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(screenWidth - 40.0f);
        if (m_justOpened) {
            ImGui::SetKeyboardFocusHere();
            m_justOpened = false;
        }
        const bool submit = ImGui::InputText("##ConsoleInput", m_input, sizeof(m_input),
            ImGuiInputTextFlags_EnterReturnsTrue
            | ImGuiInputTextFlags_AutoSelectAll
            | ImGuiInputTextFlags_CallbackCharFilter,
            consoleCharFilter);
        if (submit) {
            std::string line(m_input);
            m_input[0] = '\0';
            execute(line);
        }
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    m_autoScroll = false;
}
