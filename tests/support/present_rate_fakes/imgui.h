#ifndef DISRUPTOR_TEST_FAKE_IMGUI_H
#define DISRUPTOR_TEST_FAKE_IMGUI_H
enum { ImGuiSliderFlags_AlwaysClamp = 16, ImGuiCol_Text = 0 };
struct ImVec4 {
    ImVec4(float red, float green, float blue, float alpha) : x(red), y(green), z(blue), w(alpha) {}
    float x, y, z, w;
};
namespace ImGui {
void BeginDisabled();
bool BeginItemTooltip();
bool Checkbox(const char *label, bool *value);
void EndDisabled();
void EndTooltip();
float GetFontSize();
void PopStyleColor(int count = 1);
void PopTextWrapPos();
void PushStyleColor(int index, const ImVec4 &colour);
void PushTextWrapPos(float position);
void SameLine(float offset = 0.0f, float spacing = -1.0f);
void SetNextItemWidth(float width);
bool SliderInt(const char *label, int *value, int lowest, int highest, const char *format, int flags);
void TextDisabled(const char *text, ...);
void TextUnformatted(const char *text, const char *end = nullptr);
}  // namespace ImGui
#endif
