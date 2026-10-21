#pragma once

#include <cstdint>

struct SDL_Window {};

using Uint8 = std::uint8_t;
using SDL_Scancode = int;
using SDL_WindowFlags = std::uint64_t;
using SDL_JoystickID = std::uint32_t;

constexpr int SDL_SCANCODE_ESCAPE = 41;
constexpr int SDL_SCANCODE_UNKNOWN = 0;
constexpr int SDL_SCANCODE_A = 4;
constexpr int SDL_SCANCODE_D = 7;
constexpr int SDL_SCANCODE_E = 8;
constexpr int SDL_SCANCODE_F = 9;
constexpr int SDL_SCANCODE_P = 19;
constexpr int SDL_SCANCODE_Q = 20;
constexpr int SDL_SCANCODE_R = 21;
constexpr int SDL_SCANCODE_S = 22;
constexpr int SDL_SCANCODE_W = 26;
constexpr int SDL_SCANCODE_Z = 29;
constexpr int SDL_SCANCODE_KP_PLUS = 87;
constexpr int SDL_SCANCODE_LALT = 226;
constexpr int SDL_SCANCODE_RALT = 230;
constexpr int SDL_SCANCODE_1 = 30;
constexpr int SDL_SCANCODE_2 = 31;
constexpr int SDL_SCANCODE_3 = 32;
constexpr int SDL_SCANCODE_4 = 33;
constexpr int SDL_SCANCODE_5 = 34;
constexpr int SDL_SCANCODE_6 = 35;
constexpr int SDL_SCANCODE_7 = 36;
constexpr int SDL_SCANCODE_8 = 37;
constexpr int SDL_SCANCODE_9 = 38;
constexpr int SDL_SCANCODE_0 = 39;
constexpr int SDL_SCANCODE_KP_1 = 89;
constexpr int SDL_SCANCODE_KP_2 = 90;
constexpr int SDL_SCANCODE_KP_3 = 91;
constexpr int SDL_SCANCODE_KP_4 = 92;
constexpr int SDL_SCANCODE_KP_5 = 93;
constexpr int SDL_SCANCODE_KP_6 = 94;
constexpr int SDL_SCANCODE_KP_7 = 95;
constexpr int SDL_SCANCODE_KP_8 = 96;
constexpr int SDL_SCANCODE_KP_9 = 97;
constexpr int SDL_SCANCODE_KP_0 = 98;
constexpr int SDL_SCANCODE_RETURN = 40;
constexpr int SDL_SCANCODE_TAB = 43;
constexpr int SDL_SCANCODE_SPACE = 44;
constexpr int SDL_SCANCODE_RIGHT = 79;
constexpr int SDL_SCANCODE_LEFT = 80;
constexpr int SDL_SCANCODE_DOWN = 81;
constexpr int SDL_SCANCODE_UP = 82;
constexpr std::uint32_t SDL_BUTTON_LMASK = 0x00000001u;
constexpr std::uint32_t SDL_BUTTON_MMASK = 0x00000002u;
constexpr std::uint32_t SDL_BUTTON_RMASK = 0x00000004u;
constexpr std::uint32_t SDL_BUTTON_X1MASK = 0x00000008u;
constexpr std::uint32_t SDL_BUTTON_X2MASK = 0x00000010u;
constexpr SDL_WindowFlags SDL_WINDOW_INPUT_FOCUS = 0x00000200u;
constexpr SDL_WindowFlags SDL_WINDOW_MINIMIZED = 0x00000040u;

bool SDL_SetWindowRelativeMouseMode(SDL_Window *window, bool enabled);
std::uint32_t SDL_GetMouseState(float *x, float *y);
std::uint32_t SDL_GetRelativeMouseState(float *x, float *y);
const Uint8 *SDL_GetKeyboardState(int *count);
SDL_WindowFlags SDL_GetWindowFlags(SDL_Window *window);
bool SDL_SetWindowTitle(SDL_Window *window, const char *title);
const char *SDL_GetError();
int SDL_NumJoysticks();
SDL_JoystickID SDL_JoystickGetDeviceInstanceID(int index);
const char *SDL_GetJoystickNameForID(SDL_JoystickID device);
