#ifndef DISRUPTOR_TEST_FAKE_IMGUI_H
#define DISRUPTOR_TEST_FAKE_IMGUI_H
enum { ImGuiSliderFlags_AlwaysClamp = 16 };
namespace ImGui {
void BeginDisabled();
bool Checkbox(const char *label, bool *value);
void EndDisabled();
void SetNextItemWidth(float width);
bool SliderInt(const char *label, int *value, int lowest, int highest, const char *format, int flags);
void TextDisabled(const char *text, ...);
}  // namespace ImGui
#endif
