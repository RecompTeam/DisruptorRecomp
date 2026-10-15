#pragma once

#include "game/settings/hint_text.h"
#include "imgui.h"

#include <string_view>

namespace disruptor::ui {

// Shows `text` while the pointer rests on the control drawn last, a line's choice name in green as the launcher has it.
inline void hint(const char *text) {
    if (!ImGui::BeginItemTooltip()) return;
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    for (std::string_view rest = text;;) {
        const std::size_t end = rest.find('\n');
        const std::string_view line = rest.substr(0, end);
        if (const std::size_t name = disruptor::settings::choice_name(line)) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.44f, 0.81f, 0.48f, 1.0f));
            ImGui::TextUnformatted(line.data(), line.data() + name);
            ImGui::PopStyleColor();
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::TextUnformatted(line.data() + name, line.data() + line.size());
        } else {
            ImGui::TextUnformatted(line.data(), line.data() + line.size());
        }
        if (end == std::string_view::npos) break;
        rest.remove_prefix(end + 1);
    }
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

}  // namespace disruptor::ui
