#pragma once

#include "game/render/sky_extension.h"

#include <cstdint>
#include <optional>

namespace disruptor::sky_art {

std::optional<sky_extension::Extension> find(const sky_panorama::Picture &picture, int32_t minimum_rows);

}
