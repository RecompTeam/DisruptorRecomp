#include "art.h"

#include <SDL3/SDL.h>

#include <windows.h>

namespace disruptor::launcher {

namespace {

constexpr int first_run_resource = 2, app_icon_resource = 7;

SDL_Texture* pixel_texture(SDL_Renderer* renderer, SDL_Surface* surface, SDL_ScaleMode mode = SDL_SCALEMODE_NEAREST) {
    if (!surface) return nullptr;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    if (texture) SDL_SetTextureScaleMode(texture, mode);
    return texture;
}

SDL_Surface* bitmap(int id) {
    const auto bytes = resource_bytes(id);
    if (bytes.empty()) return nullptr;
    SDL_IOStream* stream = SDL_IOFromConstMem(bytes.data(), bytes.size());
    return stream ? SDL_LoadBMP_IO(stream, true) : nullptr;
}

} // namespace

SDL_Texture* picture_texture(SDL_Renderer* renderer, const Picture& picture) {
    if (picture.width <= 0 || picture.height <= 0 ||
        picture.pixels.size() != static_cast<std::size_t>(picture.width) * static_cast<std::size_t>(picture.height))
        return nullptr;
    SDL_Surface* surface = SDL_CreateSurfaceFrom(picture.width, picture.height, SDL_PIXELFORMAT_RGBA32,
                                                 const_cast<std::uint32_t*>(picture.pixels.data()), picture.width * 4);
    return pixel_texture(renderer, surface);
}

SDL_Texture* first_run_texture(SDL_Renderer* renderer) { return pixel_texture(renderer, bitmap(first_run_resource)); }

SDL_Texture* app_icon_texture(SDL_Renderer* renderer) {
    return pixel_texture(renderer, bitmap(app_icon_resource), SDL_SCALEMODE_LINEAR);
}

std::span<const std::byte> resource_bytes(int id) {
    HRSRC found = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    HGLOBAL loaded = found ? LoadResource(nullptr, found) : nullptr;
    const void* bytes = loaded ? LockResource(loaded) : nullptr;
    if (!bytes) return {};
    return {static_cast<const std::byte*>(bytes), SizeofResource(nullptr, found)};
}

} // namespace disruptor::launcher
