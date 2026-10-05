#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include "gui.hh"

void render_site_dialog(Global *g);

static void glfw_error_callback(int error, const char* description)
{
    std::fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

ULONG UI::Thread(void *_g) {
    Global *g = (Global*)_g;
    if(!g)
        return 1;
    Window(g);

    g->gui_thread = 0;
    g->gui_active = false;
    return 0;
}

void UI::Window(Global *g) {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    auto window_x = g->gui.last_window_width ? g->gui.last_window_width : 1280;
    auto window_y = g->gui.last_window_height ? g->gui.last_window_height : 720;

    GLFWwindow* window = glfwCreateWindow(
        window_x,
        window_y,
        "TailMapper",
        nullptr,
        nullptr
    );

    if (!window) {
        glfwTerminate();
        return;
    }

    g->gui_window = glfwGetWin32Window(window);

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // VSync

    // Dear ImGui setup
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig config{};
    config.FontDataOwnedByAtlas = false;
    ImFont* aptos = io.Fonts->AddFontFromMemoryTTF(
            (void*)g->gui.font.data,
            g->gui.font.size,
            20.0f,
            &config
            );
    if(!aptos)
        return;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();

        /* This is contributing to the danger described in tailnet.cc,
         * once again, I'm just going to ignore it... */
        while(g->ts.status_refresh_ongoing);
        g->gui_sleeping = false;
        ImGui::NewFrame();

        UI::Frame(g);

        ImGui::Render();
        g->gui_sleeping = true;

        int display_width;
        int display_height;
        glfwGetFramebufferSize(
            window,
            &display_width,
            &display_height
        );

        glViewport(0, 0, display_width, display_height);
        glClearColor(0.10f, 0.10f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(
            ImGui::GetDrawData()
        );

        glfwSwapBuffers(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwGetWindowSize(window, &g->gui.last_window_width, &g->gui.last_window_height);

    glfwDestroyWindow(window);
    glfwTerminate();

    g->gui_window = 0;

    return;
}

void UI::Frame(Global *g) {
    const ImGuiViewport *viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar
            | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("##TailMapperRoot", nullptr, flags);

    SitesTable(g);

    ImGui::End();
}

void UI::MappingsTable(Global *g) {
    
}

void UI::SitesTable(Global *g) {
    ImGui::Text("Sites");
    ImGui::Separator();
    ImGui::Spacing();

    constexpr ImGuiTableFlags table_flags =
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_SizingStretchProp;

    bool open_site_popup = false;

    if (ImGui::BeginTable("sites_table", 3, table_flags))
    {
        ImGui::TableSetupColumn(
            "Site ID",
            ImGuiTableColumnFlags_WidthFixed,
            80.0f
        );

        ImGui::TableSetupColumn(
            "Site Name",
            ImGuiTableColumnFlags_WidthStretch
        );

        ImGui::TableSetupColumn(
            "Address",
            ImGuiTableColumnFlags_WidthStretch
        );

        ImGui::TableHeadersRow();

        for (const auto& [site_id, site] : *g->gui.site_map)
        {
            ImGui::TableNextRow();

            // Site ID
            ImGui::TableSetColumnIndex(0);

            ImGui::Text("%u", site.id);

            // Site Name
            ImGui::TableSetColumnIndex(1);

            ImGui::PushID(static_cast<int>(site.id));

            if (ImGui::Selectable(
                    site.name.c_str(),
                    g->gui.selected_site == site_id,
                    ImGuiSelectableFlags_SpanAllColumns))
            {
                g->gui.selected_site = site.id;
                g->gui.site_popup_open = true;
                open_site_popup = true;
            }

            ImGui::PopID();

            // Address
            ImGui::TableSetColumnIndex(2);

            ImGui::TextUnformatted(ip_to_str(site.ip).c_str());
        }

        ImGui::EndTable();
    }

    if(open_site_popup)
        ImGui::OpenPopup("SiteDetails");
    render_site_dialog(g);
}

void render_site_dialog(Global *g) {
    if (ImGui::BeginPopupModal(
            "SiteDetails",
            &g->gui.site_popup_open,
            ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (!(g->gui.selected_site > U16MAX))
        {
            if (!(*g->gui.site_map).contains(g->gui.selected_site)) {
                ImGui::CloseCurrentPopup();
                g->gui.selected_site = U16MAX;
                g->gui.site_popup_open = false;
            }
            auto site_id = g->gui.selected_site;
            auto site = (*g->gui.site_map).at(site_id);
            auto site_name = site.name.c_str();
            auto site_ip = ip_to_str(site.ip);

            ImGui::Text("Site ID");
            ImGui::SameLine(120.0f);
            ImGui::Text("%u", g->gui.selected_site);

            ImGui::Text("Site Name");
            ImGui::SameLine(120.0f);
            ImGui::TextUnformatted(site_name);

            ImGui::Text("Address");
            ImGui::SameLine(120.0f);
            ImGui::TextUnformatted(site_ip.c_str());

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Close", ImVec2(100.0f, 0.0f)))
            {
                ImGui::CloseCurrentPopup();
                g->gui.site_popup_open = false;
                g->gui.selected_site = U16MAX;
            }
        }

        ImGui::EndPopup();
    }
}
