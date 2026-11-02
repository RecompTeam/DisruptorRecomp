#include "imgui.h"
#include "imgui_internal.h"
#include "misc/freetype/imgui_freetype.h"

#include <cmath>
#include <iostream>

namespace {

int g_failures = 0;

void expect(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << "\n";
    ++g_failures;
}

float letter_height(ImFont* font, float size, ImWchar letter) {
    const ImFontGlyph* glyph = font->GetFontBaked(size)->FindGlyph(letter);
    return glyph->Y1 - glyph->Y0;
}

} // namespace

int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(640.0f, 480.0f);
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.Fonts->SetFontLoader(ImGuiFreeType::GetFontLoader());
    io.Fonts->FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;
    ImFont* font = io.Fonts->AddFontDefaultVector();
    ImGui::NewFrame();

    // 40 px is the size the loader switched to last, and dropping 20 px moves 30 px into 40 px's old place.
    ImFontBaked* dropped = font->GetFontBaked(20.0f);
    font->GetFontBaked(40.0f);
    font->GetFontBaked(30.0f);
    font->GetFontBaked(40.0f)->FindGlyph('A');
    ImFontAtlasBakedDiscard(io.Fonts, font, dropped);
    ImGui::Render();
    ImGui::NewFrame();

    const float middle = letter_height(font, 30.0f, 'B');
    const float large = letter_height(font, 40.0f, 'B');
    expect(large > 0.0f, "a letter at 40 px has a height");
    expect(std::abs(middle / large - 0.75f) < 0.1f,
           "a letter first drawn at 30 px after a size was dropped is drawn at 30 px, not at the last size the loader used");

    ImGui::EndFrame();
    ImGui::DestroyContext();
    if (g_failures != 0) return 1;
    std::cout << "launcher fonts: PASS\n";
    return 0;
}
