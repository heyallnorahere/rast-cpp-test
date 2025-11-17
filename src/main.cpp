#include <string>
#include <memory>
#include <vector>
#include <stdexcept>
#include <numbers>
#include <chrono>

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

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

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

    void InitImGui() { window_init_imgui(m_Window); }

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

    void RenderIndexed(indexed_render_call& call) const { render_indexed(m_Rasterizer, &call); }

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

struct Uniforms {
    glm::mat4 Projection, View;
};

struct Vertex {
    glm::vec3 Position;
};

struct Instance {
    glm::mat4 Model;
    std::uint32_t Color;
};

struct WorkingData {
    std::uint32_t Color;
};

static const std::vector<Vertex> s_Vertices = {
    {
        .Position = glm::vec3(0.f, -0.5f, 0.f),
    },
    {
        .Position = glm::vec3(0.5f, 0.5f, 0.f),
    },
    {
        .Position = glm::vec3(-0.5f, 0.5f, 0.f),
    },
};

static const std::vector<std::uint16_t> s_Indices = {
    0,
    1,
    2,
};

static void VertexShader(const void* const* vertexData, const shader_context* context,
                         float* position) {
    auto vertex = (const Vertex*)vertexData[0];
    auto instance = (const Instance*)vertexData[1];
    auto uniforms = (const Uniforms*)context->uniform_data;

    auto vertexPos = glm::vec4(vertex->Position, 1.f);
    auto worldPos = instance->Model * vertexPos;
    auto viewPos = uniforms->View * worldPos;
    auto screenPos = uniforms->Projection * viewPos;

    memcpy(position, &screenPos, 4 * sizeof(float));

    auto workingData = (WorkingData*)context->working_data;
    workingData->Color = instance->Color;
}

static std::uint32_t FragmentShader(const shader_context* context) {
    auto workingData = (const WorkingData*)context->working_data;
    return workingData->Color;
}

static bool IsDepthBufferValid(image_t* buffer, std::uint32_t width, std::uint32_t height) {
    if (!buffer) {
        return false;
    }

    return buffer->width == width && buffer->height == height;
}

static void ValidateDepthBuffer(const Window& window, image_t** buffer) {
    std::uint32_t width, height;
    window.GetFramebufferSize(&width, &height);

    if (!IsDepthBufferValid(*buffer, width, height)) {
        image_free(*buffer);
        *buffer = image_allocate(width, height, IMAGE_FORMAT_DEPTH);
    }
}

int main(int argc, const char** argv) {
    auto rast = Rasterizer::Create();
    auto window = Window::Create("Test", 1600, 900);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    window->InitImGui();
    auto renderer = std::make_unique<ImGuiRenderer>(rast);

    std::vector<image_t*> attachments = { nullptr, nullptr };
    framebuffer fb;
    fb.attachment_count = (uint32_t)attachments.size();
    fb.attachments = attachments.data();

    static const std::vector<vertex_binding> bindings = {
        {
            .stride = sizeof(Vertex),
            .input_rate = VERTEX_INPUT_RATE_VERTEX,
        },
        {
            .stride = sizeof(Instance),
            .input_rate = VERTEX_INPUT_RATE_INSTANCE,
        }
    };

    struct blended_parameter color_parameter;
    color_parameter.count = 4;
    color_parameter.type = ELEMENT_TYPE_BYTE;
    color_parameter.offset = 0;

    struct pipeline pipeline{};
    pipeline.shader.working_size = sizeof(WorkingData);
    pipeline.shader.vertex_stage = VertexShader;
    pipeline.shader.fragment_stage = FragmentShader;
    pipeline.shader.inter_stage_parameter_count = 1;
    pipeline.shader.inter_stage_parameters = &color_parameter;
    pipeline.depth.test = true;
    pipeline.depth.write = true;
    pipeline.binding_count = (uint32_t)bindings.size();
    pipeline.bindings = bindings.data();
    pipeline.cull_back = false;
    pipeline.winding = WINDING_ORDER_CCW;
    pipeline.topology = TOPOLOGY_TYPE_TRIANGLES;

    std::array<Instance, 6> instances;
    for (std::size_t i = 0; i < instances.size(); i++) {
        auto& instance = instances[i];

        instance.Color = 0xFF;
        for (size_t j = 0; j < 3; j++) {
            uint8_t channel = rand() & 0xFF;
            instance.Color |= channel << ((j + 1) * 8);
        }

        // scale
        glm::mat4 scale(1.f);
        for (glm::length_t j = 0; j < 3; j++) {
            // scale[j, j]
            scale[j][j] *= 0.25f;
        }

        float theta = std::numbers::pi_v<float> * 2.f * (float)i / (float)instances.size();

        // rotation
        glm::mat4 rotation(1.f);

        // rotating around y
        // this means that i is now i'cos(theta) - k'sin(theta)
        // accordingly, k is now i'sin(theta) + k'cos(theta)

        float cos_theta = glm::cos(theta);
        float sin_theta = glm::sin(theta);

        rotation[0][0] = cos_theta;
        rotation[2][0] = -sin_theta;
        rotation[0][2] = sin_theta;
        rotation[2][2] = cos_theta;

        // translation
        glm::mat4 translation(1.f);

        // negative z
        translation[3][2] = -0.5f;

        glm::mat4 displacement = rotation * translation;
        instance.Model = scale * displacement;
    }

    const std::vector<vertex_buffer> vbufs = {
        {
            .data = s_Vertices.data(),
            .size = s_Vertices.size() * sizeof(Vertex),
        },
        {
            .data = instances.data(),
            .size = instances.size() * sizeof(Instance),
        },
    };

    Uniforms uniforms;
    indexed_render_call call{};
    call.pipeline = &pipeline;
    call.framebuffer = &fb;
    call.vertices = vbufs.data();
    call.uniform_data = &uniforms;
    call.indices = s_Indices.data();
    call.index_count = (uint32_t)s_Indices.size();
    call.instance_count = (uint32_t)instances.size();

    static const std::vector<image_pixel> clearValues = { { .color = 0x787878FF },
                                                          { .depth = 1.f } };

    auto t0 = std::chrono::high_resolution_clock::now();
    float cameraTheta = 0.f;

    while (!window->IsCloseRequested()) {
        Window::Poll();
        ImGui::NewFrame();

        /* unnecessary
        static bool showDemo = true;
        if (showDemo) {
            ImGui::ShowDemoWindow(&showDemo);
        }
        */

        ImGui::Render();

        window->GetFramebufferSize(&fb.width, &fb.height);
        float aspect = (float)fb.width / (float)fb.height;

        attachments[0] = window->GetBackbuffer();
        ValidateDepthBuffer(*window, &attachments[1]);

        auto t1 = std::chrono::high_resolution_clock::now();
        auto delta = std::chrono::duration_cast<std::chrono::duration<float>>(t1 - t0);
        t0 = t1;

        float cosTheta = glm::cos(cameraTheta);
        float sinTheta = glm::sin(cameraTheta);

        float phi = cosTheta * std::numbers::pi_v<float> / 4.f;
        float cosPhi = glm::cos(phi);
        float sinPhi = glm::sin(phi);

        cameraTheta += delta.count() * 0.1f;
        float cameraDistance = glm::abs(cosTheta) * 5.f;

        glm::vec3 eye = { cosTheta * cosPhi * cameraDistance, sinPhi * cameraDistance,
                          sinTheta * cosPhi * cameraDistance };

        static const glm::vec3 center = glm::vec3(0.f);
        static const glm::vec3 up = glm::vec3(0.f, 1.f, 0.f);

        uniforms.Projection = glm::perspective(glm::radians(45.f), aspect, 0.1f, 100.f);
        uniforms.View = glm::lookAt(eye, center, up);

        rast->ClearFramebuffer(&fb, clearValues);
        rast->RenderIndexed(call);
        renderer->Render(ImGui::GetDrawData(), &fb);

        window->SwapBuffers();
    }

    image_free(attachments[1]);

    renderer.reset();
    window.reset();
    ImGui::DestroyContext();

    rast.reset();
    return 0;
}
