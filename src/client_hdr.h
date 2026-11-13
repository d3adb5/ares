/**
 * @file src/client_hdr.h
 * @brief The HDR display a client reports at launch (GameStream's clientHdrCapDisplayData).
 */
#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>

namespace client_hdr {

  /// NVAPI's NV_HDR_CAPABILITIES display data, in its order and units.
  struct display_t {
    std::uint16_t max_luminance {};  ///< cd/m^2
    std::uint16_t min_luminance {};  ///< 0.0001 cd/m^2
    std::uint16_t max_frame_average {};  ///< cd/m^2

    bool known() const {
      return max_luminance != 0;
    }
  };

  /**
   * Primaries x/y (3), white point x/y, max luminance, min luminance, max frame average, joined by 'x'.
   * Moonlight sends eleven zeros, which is a display that said nothing.
   */
  inline std::optional<display_t> parse(std::string_view text) {
    std::array<std::uint16_t, 11> values {};
    std::size_t count = 0;
    for (std::size_t pos = 0;; ++count) {
      const auto end = std::min(text.find('x', pos), text.size());
      const auto field = text.substr(pos, end - pos);
      if (count == values.size() || field.empty()) {
        return std::nullopt;
      }
      const auto [ptr, ec] = std::from_chars(field.data(), field.data() + field.size(), values[count]);
      if (ec != std::errc {} || ptr != field.data() + field.size()) {
        return std::nullopt;
      }
      if (end == text.size()) {
        break;
      }
      pos = end + 1;
    }
    if (count + 1 != values.size()) {
      return std::nullopt;
    }
    return display_t {values[8], values[9], values[10]};
  }

}  // namespace client_hdr
