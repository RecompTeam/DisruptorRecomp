#pragma once

namespace PSXRecompV4 { struct GameUserSettings; }

namespace disruptor::gamepad {

void apply_settings(const PSXRecompV4::GameUserSettings &settings);
void update_vibration();
void stop_vibration();

}
