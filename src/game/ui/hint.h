#pragma once

#include "imgui.h"

namespace disruptor::ui {

// Shows `text` while the pointer rests on the control drawn last.
inline void hint(const char *text) {
    if (!ImGui::BeginItemTooltip()) return;
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

}  // namespace disruptor::ui
