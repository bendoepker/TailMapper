#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include "gui.hh"
#include "common.hh"

void render_site_dialog(Global *g);
void draw_toolbar(Global *g);
void draw_available_routes_table(Global *g);
/* Taken from https://github.com/ocornut/imgui/issues/707#issuecomment-4107169777 with love */
void imgui_dark_style();
void sites_table(Global *g);
void mappings_table(Global *g);
void window(Global *g);
void frame(Global *g);

static void glfw_error_callback(int error, const char* description)
{
    std::fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

ULONG UI::thread(void *_g) {
    TRACE("%s", "GUI Thread Initialized");
    Global *g = (Global*)_g;
    if(!g) {
        PostMessage(g->main_window, WM_TM_FATAL_ERROR, TME_CORRUPTED_GLOBAL_VARIABLE, 0);
        return 1;
    }

    /* prevent a weird reentrancy bug */
    if(g->shutdown_event)
        return 1;

    window(g);

    g->gui_thread = 0;
    g->gui_active = false;
    return 0;
}

void window(Global *g) {
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
    if(!aptos) {
        PostMessage(g->main_window, WM_TM_FATAL_ERROR, TME_CORRUPTED_EXE, 0);
        return;
    }

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    imgui_dark_style();

    while (!glfwWindowShouldClose(window))
    {
        if(g->shutdown_event) {
            TRACE("%s", "Received shutdown event");
            glfwSetWindowShouldClose(window, 1);
            break;
        }

        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();

        /* This is contributing to the danger described in tailnet.cc,
         * once again, I'm just going to ignore it... */
        while(g->ts.status_refresh_ongoing);
        g->gui_sleeping = false;
        ImGui::NewFrame();

        frame(g);

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

void frame(Global *g) {
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

    draw_toolbar(g);

    sites_table(g);

    ImGui::End();
}

void mappings_table(Global *g) {
    /* These are the activated mappings */
    
}

void sites_table(Global *g) {
    /* This is the available sites on the tailnet */
    ImGui::Text("Sites (%llu)", g->gui.site_map->size());
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

        for (const auto& [site_id, site] : *(g->gui.site_map))
        {
            ImGui::TableNextRow();

            /* site id */
            ImGui::TableSetColumnIndex(0);

            ImGui::Text("%u", site_id);

            /* site name */
            ImGui::TableSetColumnIndex(1);

            ImGui::PushID(static_cast<int>(site_id));

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

            draw_available_routes_table(g);

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

void draw_toolbar(Global *g) {
    constexpr float toolbar_height = 36.0f;

    ImGui::BeginChild(
            "##toolbar",
            ImVec2(0.0f, toolbar_height)
            );

    using namespace std::chrono_literals;
    auto refresh_disabled = g->ts.last_refresh_sent > (sclock::now() - 5s);
    ImGui::BeginDisabled(refresh_disabled);
    if(ImGui::Button("Refresh Available Sites")) {
        PostMessage(g->main_window, WM_TM_REFRESH, 0, 0);
    }
    ImGui::EndDisabled();

    ImGui::SameLine();

    if(ImGui::Button("Submit Issue")) {
        ShellExecute(0, "open", "https://github.com/bendoepker/TailMapper/issues", 0, 0, SW_SHOWNORMAL);
    }
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("https://github.com/bendoepker/TailMapper/issues");

    ImGui::EndChild();
}

void draw_available_routes_table(Global *g) {
    if(ImGui::BeginTable("Advertised Routes", 3)) {
        ImGui::TableSetupColumn(
            "Local Subnet",
            ImGuiTableColumnFlags_WidthFixed,
            240.0f
        );

        ImGui::TableSetupColumn(
            "Remote Subnet",
            ImGuiTableColumnFlags_WidthStretch
        );

        ImGui::TableSetupColumn(
            "",
            ImGuiTableColumnFlags_WidthStretch
        );

        ImGui::TableHeadersRow();

        auto& v = (*g->gui.site_map)[g->gui.selected_site].advertised_routes;
        for (auto i = 0; i < v.size(); i++)
        {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);

            ImGui::Text("%s", ip_to_str(v[i].local_ip).c_str());

            ImGui::TableSetColumnIndex(1);

            ImGui::Text("%s", ip_to_str(v[i].remote_ip).c_str());

            ImGui::TableSetColumnIndex(2);

            ImGui::PushID(i);

            bool added = g->rmap.route_in_map(v[i]);

            ImGui::BeginDisabled(added);
                if(ImGui::Button("Add to map")) {
                    g->rmap.add_route(v[i]);
                }
            ImGui::EndDisabled();

            ImGui::SameLine();

            ImGui::BeginDisabled(!added);
                ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor(255, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor(200, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor(150, 0, 0));
                if(ImGui::Button("Remove from map")) {
                    g->rmap.remove_route(v[i]);
                }
                ImGui::PopStyleColor(3);
            ImGui::EndDisabled();

            ImGui::PopID();
        }

        ImGui::EndTable();
    }
}

void imgui_dark_style()
{
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();

    // --- 1. Sizing and Spacing (Modern & Tight) ---
    style.WindowPadding = ImVec2(8.0f, 8.0f);
    style.FramePadding = ImVec2(5.0f, 3.0f);
    style.CellPadding = ImVec2(6.0f, 4.0f);
    style.ItemSpacing = ImVec2(6.0f, 4.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.ScrollbarSize = 13.0f;
    style.GrabMinSize = 10.0f;

    // --- 2. Borders & Rounding ---
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;

    style.WindowRounding = 4.0f;
    style.ChildRounding = 3.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 3.0f;
    style.ScrollbarRounding = 9.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 3.0f;
}
