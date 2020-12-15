// Compiler for PHP (aka KPHP)
// Copyright (c) 2020 LLC «V Kontakte»
// Distributed under the GPL v3 License, see LICENSE.notice.txt

#pragma once

#include <string>

#include "common/tlo-parsing/tl-objects.h"

namespace vk {
namespace tl {

struct TLOParsingResult {
  std::unique_ptr<tl_scheme> parsed_schema;
  std::string error;
};

TLOParsingResult parse_tlo(const char *tlo_path, bool rename_all_forbidden_names);
void rename_tl_name_if_forbidden(std::string &tl_name);
} // namespace tl
} // namespace vk
