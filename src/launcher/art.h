#pragma once

#include "menu_art.h"

#include <cstddef>
#include <span>

struct SDL_Renderer;
struct SDL_Texture;

namespace disruptor::launcher {

// A texture drawn pixel for pixel, as the PlayStation drew it, however far it is scaled. Null when SDL refuses one.
SDL_Texture* picture_texture(SDL_Renderer* renderer, const Picture& picture);
// The original picture shown before a disc is imported, from the launcher's resources.
SDL_Texture* first_run_texture(SDL_Renderer* renderer);
// The program's icon, 64 pixels a side, smoothed when it is drawn smaller.
SDL_Texture* app_icon_texture(SDL_Renderer* renderer);
// The bytes of one of the launcher's own resources, held for as long as the program runs. Empty when absent.
std::span<const std::byte> resource_bytes(int id);

} // namespace disruptor::launcher
