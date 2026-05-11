#pragma once

#include <string>

namespace eureka::lex {

bool convert_json_to_agb(const std::string& json_path, const std::string& agb_path) noexcept;

} // namespace eureka::lex
