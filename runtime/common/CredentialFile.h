/****************************************************************-*- C++ -*-****
 * Copyright (c) 2026 NVIDIA Corporation & Affiliates.                         *
 * All rights reserved.                                                        *
 *                                                                             *
 * This source code and the accompanying materials are made available under    *
 * the terms of the Apache License 2.0 which accompanies this distribution.    *
 ******************************************************************************/

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace cudaq {

/// @brief Describes the accepted layout of a vendor credential file (e.g.
/// `~/.quantinuum_config`), where each line has the form `<key> : <value>`.
struct CredentialFileFormat {
  /// Keys that may appear in the file. Any other key is an error.
  std::vector<std::string> allowedKeys;

  /// Completes the sentence "Key-value pairs must be in ..." in the error
  /// reported for an ill-formed line.
  std::string formatHint;

  /// Optional. A key (written exactly like this, without whitespace before the
  /// `:`) whose value may itself contain `:` (the line then splits into four
  /// `:`-separated pieces, e.g. `credentials:{username:u, password:p}`). The
  /// value handed to the callback is the trimmed first piece after the key,
  /// followed by the remaining pieces re-joined with `:` and not trimmed.
  /// Empty means every line must split into exactly two pieces.
  std::string multiColonKey;
};

/// @brief Parse a credential file line by line, in file order.
///
/// Each line is split on `:`. For every well-formed line the key and the value
/// are trimmed and, if the key is in `format.allowedKeys`, `onEntry(key,
/// value)` is called. A later line with the same key is reported again, so the
/// callback decides which one wins. Exceptions thrown by the callback
/// propagate unchanged.
///
/// An unreadable or non-existent file is treated like an empty one (no
/// callbacks); callers check for existence beforehand if they care.
///
/// @throws std::runtime_error for a line that is not well-formed (including a
/// blank line) or whose key is not allowed.
void parseCredentialFile(
    const std::string &path, const CredentialFileFormat &format,
    const std::function<void(const std::string &key, const std::string &value)>
        &onEntry);

} // namespace cudaq
