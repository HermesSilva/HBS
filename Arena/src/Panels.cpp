#include "App.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <cstring>
#include <cmath>
#include <cctype>

namespace ed {

// small helpers for editing arrays
static bool editVec3(const char* label, Vec3& v, float step = 0.01f) {
    float f[3] = {(float)v[0], (float)v[1], (float)v[2]};
    if (ImGui::DragFloat3(label, f, step)) { v = {f[0], f[1], f[2]}; return true; }
    return false;
}
static bool editText(const char* label, std::string& s) {
    char buf[256]; std::strncpy(buf, s.c_str(), sizeof(buf)-1); buf[sizeof(buf)-1]=0;
    if (ImGui::InputText(label, buf, sizeof(buf))) { s = buf; return true; }
    return false;
}

void App::drawMenuBar() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New")) newModel();
            if (ImGui::MenuItem("Open .mass...", "Ctrl+O")) openMass();
            if (ImGui::MenuItem("Save", "Ctrl+S")) saveMass(false);
            if (ImGui::MenuItem("Save As...")) saveMass(true);
            ImGui::Separator();
            if (ImGui::MenuItem("Import env.xml (skeleton+muscle+params)")) bootstrap();
            if (ImGui::MenuItem("Export env.xml + skeleton + muscle...")) exportLegacy();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) undo();
            if (ImGui::MenuItem("Redo", "Ctrl+Y")) redo();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Anatomy")) {
            if (ImGui::MenuItem("Import OpenSim atlas (.osim)...")) importOsim();
            if (ImGui::MenuItem("Import mesh as skin (obj/glb/fbx/stl)...")) importSkinMesh();
            if (ImGui::MenuItem("Load rigged character (FBX/GLB, plays its clip)...")) importRiggedDialog();
            if (ImGui::MenuItem("Mirror muscle L<->R")) mirrorSelectedMuscle();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Floor", nullptr, &mDrawGrid);
            ImGui::MenuItem("Muscles", nullptr, &mShowMuscles);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
}

// ---- vector icons drawn with ImDrawList (no font asset) ----
enum Icon { IC_NEW, IC_OPEN, IC_SAVE, IC_PLAY, IC_STOP, IC_RESET, IC_FILL, IC_EYE, IC_EXPORT, IC_REC };

static void drawIcon(ImDrawList* dl, ImVec2 p, float s, int icon, ImU32 col) {
    ImVec2 c(p.x + s*0.5f, p.y + s*0.5f);
    float t = 1.6f;
    switch (icon) {
        case IC_NEW: {
            dl->AddRect({p.x+s*0.28f,p.y+s*0.18f},{p.x+s*0.72f,p.y+s*0.82f}, col, 2, 0, t);
            dl->AddLine({p.x+s*0.6f,p.y+s*0.18f},{p.x+s*0.6f,p.y+s*0.32f}, col, t);
            dl->AddLine({p.x+s*0.6f,p.y+s*0.32f},{p.x+s*0.72f,p.y+s*0.32f}, col, t);
        } break;
        case IC_OPEN: {
            dl->AddRect({p.x+s*0.22f,p.y+s*0.34f},{p.x+s*0.78f,p.y+s*0.74f}, col, 2, 0, t);
            dl->AddLine({p.x+s*0.22f,p.y+s*0.34f},{p.x+s*0.38f,p.y+s*0.34f}, col, t);
            dl->AddLine({p.x+s*0.38f,p.y+s*0.34f},{p.x+s*0.46f,p.y+s*0.26f}, col, t);
            dl->AddLine({p.x+s*0.46f,p.y+s*0.26f},{p.x+s*0.62f,p.y+s*0.26f}, col, t);
        } break;
        case IC_SAVE: {
            dl->AddRect({p.x+s*0.24f,p.y+s*0.24f},{p.x+s*0.76f,p.y+s*0.76f}, col, 2, 0, t);
            dl->AddRectFilled({p.x+s*0.38f,p.y+s*0.24f},{p.x+s*0.62f,p.y+s*0.4f}, col, 0);
            dl->AddRect({p.x+s*0.36f,p.y+s*0.52f},{p.x+s*0.64f,p.y+s*0.72f}, col, 0, 0, t);
        } break;
        case IC_PLAY:
            dl->AddTriangleFilled({p.x+s*0.36f,p.y+s*0.28f},{p.x+s*0.36f,p.y+s*0.72f},{p.x+s*0.72f,c.y}, col);
            break;
        case IC_STOP:
            dl->AddRectFilled({p.x+s*0.32f,p.y+s*0.32f},{p.x+s*0.68f,p.y+s*0.68f}, col, 1);
            break;
        case IC_RESET: {
            dl->PathArcTo(c, s*0.26f, 0.5f, 6.0f, 20); dl->PathStroke(col, 0, t);
            dl->AddTriangleFilled({c.x+s*0.26f,p.y+s*0.2f},{c.x+s*0.12f,p.y+s*0.28f},{c.x+s*0.3f,p.y+s*0.34f}, col);
        } break;
        case IC_FILL: { // a little body silhouette
            dl->AddCircleFilled({c.x,p.y+s*0.3f}, s*0.12f, col);
            dl->AddRectFilled({c.x-s*0.14f,p.y+s*0.44f},{c.x+s*0.14f,p.y+s*0.78f}, col, 3);
        } break;
        case IC_EYE: {
            dl->AddCircle(c, s*0.26f, col, 20, t);
            dl->AddCircleFilled(c, s*0.09f, col);
        } break;
        case IC_EXPORT: {
            dl->AddLine({c.x,p.y+s*0.24f},{c.x,p.y+s*0.56f}, col, t);
            dl->AddLine({c.x,p.y+s*0.24f},{c.x-s*0.1f,p.y+s*0.36f}, col, t);
            dl->AddLine({c.x,p.y+s*0.24f},{c.x+s*0.1f,p.y+s*0.36f}, col, t);
            dl->AddLine({p.x+s*0.28f,p.y+s*0.7f},{p.x+s*0.72f,p.y+s*0.7f}, col, t);
        } break;
        case IC_REC: {   // auto-export toggle: a dot inside a ring
            dl->AddCircle(c, s*0.28f, col, 20, t);
            dl->AddCircleFilled(c, s*0.13f, col);
        } break;
    }
}

static bool iconButton(const char* id, int icon, const char* tip, bool active=false) {
    const float s = 30.0f;
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_ButtonActive));
    bool clicked = ImGui::Button("##ic", ImVec2(s, s));
    if (active) ImGui::PopStyleColor();
    ImU32 col = ImGui::GetColorU32(active ? ImGuiCol_Text : ImGuiCol_Text);
    drawIcon(ImGui::GetWindowDrawList(), p, s, icon, col);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    ImGui::PopID();
    return clicked;
}

// horizontal action bar rendered at the top of the dock host (under the menu)
void App::drawTopToolbar() {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
    if (iconButton("new", IC_NEW, "New model")) newModel();          ImGui::SameLine();
    if (iconButton("open", IC_OPEN, "Open .mass...")) openMass();    ImGui::SameLine();
    if (iconButton("save", IC_SAVE, "Save (Ctrl+S)")) saveMass(false); ImGui::SameLine();
    ImGui::TextDisabled("|"); ImGui::SameLine();
    if (iconButton("sim", mSimActive ? IC_STOP : IC_PLAY, mSimActive ? "Stop simulation" : "Simulate", mSimActive)) toggleSim();
    ImGui::SameLine();
    if (iconButton("reset", IC_RESET, "Reset view (Ctrl+1)")) resetView(); ImGui::SameLine();
    ImGui::TextDisabled("|"); ImGui::SameLine();
    ImGui::BeginDisabled(mFillRunning);
    if (iconButton("genfill", IC_FILL, "Generate fill")) startFillGeneration(mFillNewName);
    ImGui::EndDisabled(); ImGui::SameLine();
    if (iconButton("showfill", IC_EYE, mShowSkin ? "Hide fill" : "Show fill", mShowSkin)) mShowSkin = !mShowSkin;
    ImGui::SameLine();
    ImGui::TextDisabled("|"); ImGui::SameLine();
    if (iconButton("export", IC_EXPORT, "Export a training set now")) {
        std::string dir, err;
        mStatus = exportTrainingSet(&dir, &err) ? ("Training set: " + dir)
                                                : ("Training set export failed: " + err);
    }
    ImGui::SameLine();
    if (iconButton("autoexport", IC_REC, mAutoExportTraining
            ? "Auto training sets: ON (a set per muscle-structure change)"
            : "Auto training sets: OFF", mAutoExportTraining))
        mAutoExportTraining = !mAutoExportTraining;
    ImGui::PopStyleVar();
    ImGui::Separator();
}











// ---- main frame ----
void App::frame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();

    // One fullscreen window: menu bar, the action button row, and the 3D view
    // filling everything below it. There are no docked panels — the model is
    // driven through the MCP server (see App::mcpPoll), and the buttons cover
    // what a human still needs at hand.
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGuiWindowFlags host = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
    ImGui::Begin("Arena", nullptr, host);
    ImGui::PopStyleVar(3);
    drawMenuBar();
    drawTopToolbar();

    // apply anything an agent queued over the MCP port since the last frame
    mcpPoll();

    // reflect edits in the running sim (debounced rebuild)
    maybeAutoApplySim();

    // and give any new muscle structure its own training set
    maybeExportTrainingSet();

    // fill generation: finalize when the worker finishes, and show a progress modal
    if (mFillRunning && mFillDone.load()) finalizeFill();
    if (mFillRunning) ImGui::OpenPopup("Generating fill");
    if (ImGui::BeginPopupModal("Generating fill", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::Text("Generating tissue fill (marching cubes)...");
        ImGui::ProgressBar(mFillProgress.load(), ImVec2(340, 0));
        if (!mFillRunning) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // the 3D view takes the rest of the host window: render to the FBO, then
    // show it as an image (avoids compositing issues with the ImGui draw list)
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x < 1) size.x = 1; if (size.y < 1) size.y = 1;
    mVpX = pos.x; mVpY = pos.y; mVpW = size.x; mVpH = size.y;

    drawScene();  // renders into the offscreen texture at (mVpW x mVpH)
    ImGui::Image((ImTextureID)(intptr_t)mRen.targetTexture(), size, ImVec2(0,1), ImVec2(1,0));

    bool hovered = ImGui::IsItemHovered();
    ImGuiIO& io = ImGui::GetIO();
    if (hovered && !ImGuizmo::IsUsing()) {
        // LEFT drag = orbit (pivot on selection); wheel = zoom
        bool orbiting = ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !ImGuizmo::IsOver();
        if (mSel.type != SelType::None && (io.MouseWheel != 0 || orbiting))
            mCam.target = selectionCenter();
        if (io.MouseWheel != 0) mCam.zoom(io.MouseWheel);
        if (orbiting) mCam.orbit(io.MouseDelta.x, io.MouseDelta.y);

        // RIGHT drag = pan
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right))
            mCam.pan(io.MouseDelta.x, io.MouseDelta.y);

        // MIDDLE drag = pan
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
            mCam.pan(io.MouseDelta.x, io.MouseDelta.y);
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !ImGuizmo::IsOver()) {
            ImVec2 dr = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
            if (std::fabs(dr.x) < 3 && std::fabs(dr.y) < 3)
                pickAt(io.MousePos.x, io.MousePos.y);
        }
    }
    drawGizmo();
    ImGui::End();   // host

    // keyboard shortcuts
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) undo();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) redo();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) saveMass(false);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O)) openMass();
    if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_1) || ImGui::IsKeyPressed(ImGuiKey_Keypad1)))
        resetView();  // reset camera + pose (top-row or numpad 1)
    if (ImGui::IsKeyPressed(ImGuiKey_Delete)) removeSelected();

    // status bar
    ImGui::SetNextWindowBgAlpha(0.6f);
    if (ImGui::BeginViewportSideBar("##status", vp, ImGuiDir_Down, ImGui::GetFrameHeight(),
            ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoDocking)) {
        if (ImGui::BeginMenuBar()) { ImGui::Text("%s", mStatus.c_str()); ImGui::EndMenuBar(); }
        ImGui::End();
    }

    // ---- render (3D already rendered to FBO during the Viewport window) ----
    ImGui::Render();
    int w, h; glfwGetFramebufferSize(mWin, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0.06f, 0.06f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

} // namespace ed
