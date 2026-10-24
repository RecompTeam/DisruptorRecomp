#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace disruptor::launcher {

// input.ini's [mapping]: each PlayStation button's comma list of gamepad sources. The rest of the file is kept.
class PadBindings {
public:
    static constexpr std::size_t field_count = 16;

    explicit PadBindings(std::filesystem::path file) : file_(std::move(file)) { use_defaults(); }

    // A missing file, or one that cannot be read, reads as the runtime's defaults. save() will not replace the latter.
    void load();
    void use_defaults();
    bool uses_defaults() const;
    // The first source of `field`, empty for none.
    std::string source(std::string_view field) const;
    // Puts `source` alone on `field`. Others give it up, and an action left with nothing takes the field's old source.
    std::optional<std::string> assign(std::string_view field, const std::string& source);
    bool save() const;

private:
    std::filesystem::path file_;
    std::array<std::string, field_count> sources_;
};

} // namespace disruptor::launcher
