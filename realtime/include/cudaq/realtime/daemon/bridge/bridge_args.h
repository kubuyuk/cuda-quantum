/****************************************************************-*- C++ -*-****
 * Copyright (c) 2026 NVIDIA Corporation & Affiliates.                         *
 * All rights reserved.                                                        *
 *                                                                             *
 * This source code and the accompanying materials are made available under    *
 * the terms of the Apache License 2.0 which accompanies this distribution.    *
 ******************************************************************************/

#pragma once

/// @file bridge_args.h
/// @brief Transport-neutral helpers for the `--key=value` argument lists the
/// bridge `create()` entry points receive.  Header-only, no CUDA or transport
/// dependencies.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <string>

namespace cudaq::realtime::bridge_args {

/// @brief True when `s` begins with `prefix`.
inline bool starts_with(const std::string &s, const char *prefix) {
  const std::size_t n = std::strlen(prefix);
  return s.size() >= n && std::memcmp(s.data(), prefix, n) == 0;
}

/// @brief If `arg` begins with `prefix` (e.g. "--port="), store the remainder
/// in `value` and return true.
inline bool take_value(const std::string &arg, const char *prefix,
                       std::string &value) {
  if (!starts_with(arg, prefix))
    return false;
  value = arg.substr(std::strlen(prefix));
  return true;
}

/// @brief `std::stoul` narrowed to `T` with a plain `static_cast` (values that
/// do not fit `T` wrap, as the bridges always did).  Throws what `std::stoul`
/// throws (`std::invalid_argument`, `std::out_of_range`).  `base` 0 accepts
/// decimal, `0x` hex and leading-0 octal.
template <typename T>
T to_unsigned(const std::string &text, int base = 10) {
  return static_cast<T>(std::stoul(text, nullptr, base));
}

/// @brief Walk an argv-style list, calling `handler(arg)` once per element.
/// A null element is presented as "".  Elements the handler does not
/// recognize are simply ignored by it (callers forward their whole transport
/// argument list).  If the handler throws a `std::exception` (bad number),
/// prints `ERROR: <bridge_name> bridge: bad numeric value in '<arg>'` to
/// stderr and returns false; otherwise returns true.
template <typename Handler>
bool for_each_arg(int argc, char **argv, const char *bridge_name,
                  Handler &&handler) {
  for (int i = 0; i < argc; ++i) {
    const std::string a = argv[i] ? argv[i] : "";
    try {
      handler(a);
    } catch (const std::exception &) {
      std::cerr << "ERROR: " << bridge_name << " bridge: bad numeric value in '"
                << a << "'" << std::endl;
      return false;
    }
  }
  return true;
}

} // namespace cudaq::realtime::bridge_args
