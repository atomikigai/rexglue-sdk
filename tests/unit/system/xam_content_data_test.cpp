#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <rex/memory.h>
#include <rex/string.h>
#include <rex/system/xam/content_manager.h>

namespace {

using rex::system::xam::XCONTENT_DATA;

TEST_CASE("UTF conversions replace malformed content names and stop at NUL",
          "[xam][content][utf8]") {
  const std::array<char, 4> invalid_utf8 = {'A', static_cast<char>(0xFF), 'B', '\0'};
  CHECK_NOTHROW(rex::string::to_utf16(std::string_view(invalid_utf8.data(), invalid_utf8.size())));
  CHECK(rex::string::to_utf16(std::string_view(invalid_utf8.data(), invalid_utf8.size())) ==
        u"A\uFFFDB");

  const std::array<char16_t, 5> invalid_utf16 = {u'A', static_cast<char16_t>(0xD800), u'B', u'\0',
                                                 static_cast<char16_t>(0xDC00)};
  auto converted =
      rex::string::to_utf8(std::u16string_view(invalid_utf16.data(), invalid_utf16.size()));
  CHECK_NOTHROW(rex::string::to_utf16(converted));
  CHECK(rex::string::to_utf16(converted) == u"A\uFFFDB");
}

TEST_CASE("XCONTENT_DATA bounds unterminated display and file names", "[xam][content][utf8]") {
  XCONTENT_DATA data{};
  for (auto& raw_char : data.display_name_raw.uint) {
    rex::memory::store_and_swap<uint16_t>(&raw_char, uint16_t(u'A'));
  }

  std::fill(std::begin(data.file_name_raw), std::end(data.file_name_raw), 'a');
  data.file_name_raw[4] = static_cast<char>(0xFF);

  std::u16string display_name;
  CHECK_NOTHROW(display_name = data.display_name());
  REQUIRE(display_name.size() == std::size(data.display_name_raw.uint));
  CHECK(
      std::all_of(display_name.begin(), display_name.end(), [](char16_t c) { return c == u'A'; }));
  CHECK_NOTHROW(rex::string::to_utf8(display_name));

  std::string file_name;
  CHECK_NOTHROW(file_name = data.file_name());
  CHECK(rex::string::to_utf16(file_name).size() == std::size(data.file_name_raw));
}

}  // namespace
