#include <iostream>
#include <string>
#include <memory>
#include <vector>
#include <stdexcept>

#include <cstdint>

#include <imgui.h>

extern "C" {
#include <graphics/rasterizer.h>
#include <graphics/window.h>
#include <graphics/image.h>

// hacky
#define CIMGUI_INCLUDED
#include <graphics/imgui.h>
}

class Window {
public:
    static std::unique_ptr<Window> Create(const std::string& title, std::uint32_t width,
                                          std::uint32_t height) {
        window_t* window = window_create(title.c_str(), width, height);
        if (window == nullptr) {
            return nullptr;
        }

        return std::unique_ptr<Window>(new Window(window));
    }

    static void Poll() { window_poll(); }

    ~Window() { window_destroy(m_Window); }

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool IsCloseRequested() const { return window_is_close_requested(m_Window); }

    void SwapBuffers() { window_swap_buffers(m_Window); }

    image_t* GetBackbuffer() { return window_get_backbuffer(m_Window); }

    void GetFramebufferSize(std::uint32_t* width, std::uint32_t* height) const {
        window_get_framebuffer_size(m_Window, width, height);
    }

    void InitImGui() {
        window_init_imgui(m_Window);
    }

private:
    Window(window_t* window) { m_Window = window; }

    window_t* m_Window;
};

#ifndef NDEBUG
static constexpr bool s_IsDebug = true;
#else
static constexpr bool s_IsDebug = false;
#endif

class Rasterizer {
public:
    static std::shared_ptr<Rasterizer> Create() {
        rasterizer_t* rast = rasterizer_create(!s_IsDebug);
        if (rast == nullptr) {
            return nullptr;
        }

        return std::shared_ptr<Rasterizer>(new Rasterizer(rast));
    }

    ~Rasterizer() { rasterizer_destroy(m_Rasterizer); }

    Rasterizer(const Rasterizer&) = delete;
    Rasterizer& operator=(const Rasterizer&) = delete;

    rasterizer_t* Get() { return m_Rasterizer; }

    void ClearFramebuffer(framebuffer* fb, const std::vector<image_pixel>& clearValues) const {
        if (clearValues.size() != fb->attachment_count) {
            throw std::runtime_error("Attachment size mismatch!");
        }

        framebuffer_clear(m_Rasterizer, fb, clearValues.data());
    }

private:
    Rasterizer(rasterizer_t* rast) { m_Rasterizer = rast; }

    rasterizer_t* m_Rasterizer;
};

class ImGuiRenderer {
public:
    ImGuiRenderer(const std::shared_ptr<Rasterizer>& rast) {
        m_Rasterizer = rast;

        imgui_init_renderer(m_Rasterizer->Get());
    }

    ~ImGuiRenderer() { imgui_shutdown_renderer(); }

    ImGuiRenderer(const ImGuiRenderer&) = delete;
    ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;

    void Render(ImDrawData* data, framebuffer* fb) const { imgui_render(data, fb); }

private:
    std::shared_ptr<Rasterizer> m_Rasterizer;
};

int main(int argc, const char** argv) {
    auto rast = Rasterizer::Create();
    auto window = Window::Create("Test", 1600, 900);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    window->InitImGui();
    auto renderer = std::make_unique<ImGuiRenderer>(rast);

    image_t* backbuffer = nullptr;
    framebuffer fb;
    fb.attachment_count = 1;
    fb.attachments = &backbuffer;

    static const std::vector<image_pixel> clearValues = { { .color = 0x787878FF } };
    while (!window->IsCloseRequested()) {
        Window::Poll();
        ImGui::NewFrame();

        static bool showDemo = true;
        if (showDemo) {
            ImGui::ShowDemoWindow(&showDemo);
        }

        ImGui::Render();

        window->GetFramebufferSize(&fb.width, &fb.height);
        backbuffer = window->GetBackbuffer();

        rast->ClearFramebuffer(&fb, clearValues);
        renderer->Render(ImGui::GetDrawData(), &fb);

        window->SwapBuffers();
    }

    renderer.reset();
    window.reset();
    ImGui::DestroyContext();

    rast.reset();
    return 0;
}
