/*******************************************************************************
 * Copyright (c) 2026 NVIDIA Corporation & Affiliates.                         *
 * All rights reserved.                                                        *
 *                                                                             *
 * This source code and the accompanying materials are made available under    *
 * the terms of the Apache License 2.0 which accompanies this distribution.    *
 ******************************************************************************/

#include "CredentialFile.h"
#include "cudaq/utils/cudaq_utils.h"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>

void cudaq::parseCredentialFile(
    const std::string &path, const CredentialFileFormat &format,
    const std::function<void(const std::string &key, const std::string &value)>
        &onEntry) {
  std::ifstream stream(path);
  std::string contents((std::istreambuf_iterator<char>(stream)),
                       std::istreambuf_iterator<char>());

  const auto illFormed = [&]() {
    return std::runtime_error("Ill-formed configuration file (" + path +
                              "). Key-value pairs must be in " +
                              format.formatHint);
  };

  for (const std::string &l : cudaq::split(contents, '\n')) {
    std::vector<std::string> keyAndValue = cudaq::split(l, ':');
    const bool isMultiColon = !format.multiColonKey.empty() &&
                              keyAndValue.size() == 4 &&
                              keyAndValue[0] == format.multiColonKey;
    if (keyAndValue.size() != 2 && !isMultiColon)
      throw illFormed();
    cudaq::trim(keyAndValue[0]);
    cudaq::trim(keyAndValue[1]);
    // A line such as `credentials : x` has a multi-colon key but too few
    // pieces to hold its value.
    if (!format.multiColonKey.empty() &&
        keyAndValue[0] == format.multiColonKey && !isMultiColon)
      throw illFormed();
    if (std::find(format.allowedKeys.begin(), format.allowedKeys.end(),
                  keyAndValue[0]) == format.allowedKeys.end())
      throw std::runtime_error(
          "Unknown key in configuration file: " + keyAndValue[0] + ".");
    std::string value = keyAndValue[1];
    if (isMultiColon)
      value += ":" + keyAndValue[2] + ":" + keyAndValue[3];
    onEntry(keyAndValue[0], value);
  }
}
