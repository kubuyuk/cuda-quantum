/*******************************************************************************
 * Copyright (c) 2026 NVIDIA Corporation & Affiliates.                         *
 * All rights reserved.                                                        *
 *                                                                             *
 * This source code and the accompanying materials are made available under    *
 * the terms of the Apache License 2.0 which accompanies this distribution.    *
 ******************************************************************************/

// Host-only tests for the shared bridge argument helpers.

#include "cudaq/realtime/daemon/bridge/bridge_args.h"
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ba = cudaq::realtime::bridge_args;

namespace {

struct Parsed {
  std::uint16_t port = 0;
  std::uint32_t slots = 8;
  std::uint32_t qp = 2;
  std::string device;
  bool unified = false;
  int unknown = 0;
};

// Mirrors how a bridge's create() uses the helpers.
bool parse(std::vector<const char *> args, Parsed &p) {
  return ba::for_each_arg(
      static_cast<int>(args.size()), const_cast<char **>(args.data()), "test",
      [&](const std::string &a) {
        if (ba::starts_with(a, "--port="))
          p.port = ba::to_unsigned<std::uint16_t>(a.substr(7));
        else if (ba::starts_with(a, "--num-slots="))
          p.slots = ba::to_unsigned<std::uint32_t>(a.substr(12));
        else if (ba::starts_with(a, "--remote-qp="))
          p.qp = ba::to_unsigned<std::uint32_t>(a.substr(12), 0);
        else if (ba::starts_with(a, "--device="))
          p.device = a.substr(9);
        else if (a == "--unified")
          p.unified = true;
        else
          ++p.unknown;
      });
}

} // namespace

TEST(BridgeArgs, StartsWith) {
  EXPECT_TRUE(ba::starts_with("--port=1", "--port="));
  EXPECT_TRUE(ba::starts_with("--port=", "--port="));
  EXPECT_FALSE(ba::starts_with("--port", "--port="));
  EXPECT_FALSE(ba::starts_with("", "--port="));
  EXPECT_TRUE(ba::starts_with("anything", ""));
}

TEST(BridgeArgs, TakeValue) {
  std::string v = "keep";
  EXPECT_FALSE(ba::take_value("--other=1", "--port=", v));
  EXPECT_EQ(v, "keep");
  EXPECT_TRUE(ba::take_value("--port=77", "--port=", v));
  EXPECT_EQ(v, "77");
  EXPECT_TRUE(ba::take_value("--port=", "--port=", v));
  EXPECT_EQ(v, "");
}

TEST(BridgeArgs, ValidValues) {
  Parsed p;
  ASSERT_TRUE(parse(
      {"--port=4000", "--num-slots=16", "--device=mlx5_0", "--unified"}, p));
  EXPECT_EQ(p.port, 4000);
  EXPECT_EQ(p.slots, 16u);
  EXPECT_EQ(p.device, "mlx5_0");
  EXPECT_TRUE(p.unified);
}

TEST(BridgeArgs, RemoteQpBaseZero) {
  Parsed p;
  ASSERT_TRUE(parse({"--remote-qp=0x2a"}, p));
  EXPECT_EQ(p.qp, 42u);
  ASSERT_TRUE(parse({"--remote-qp=17"}, p));
  EXPECT_EQ(p.qp, 17u);
}

TEST(BridgeArgs, MissingValueIsRejected) {
  Parsed p;
  EXPECT_FALSE(parse({"--port="}, p));
}

TEST(BridgeArgs, NonNumericIsRejected) {
  Parsed p;
  EXPECT_FALSE(parse({"--num-slots=abc"}, p));
}

TEST(BridgeArgs, OverflowOfUnsignedLongIsRejected) {
  Parsed p;
  EXPECT_FALSE(parse({"--num-slots=99999999999999999999999"}, p));
}

TEST(BridgeArgs, NarrowingWrapsSilently) {
  Parsed p;
  ASSERT_TRUE(parse({"--port=65537"}, p));
  EXPECT_EQ(p.port, 1); // 65537 wraps in uint16_t, as the bridges always did
}

TEST(BridgeArgs, NegativeWrapsSilently) {
  // std::stoul accepts a leading '-' and negates modulo 2^N.
  Parsed p;
  ASSERT_TRUE(parse({"--num-slots=-1"}, p));
  EXPECT_EQ(p.slots, 0xFFFFFFFFu);
}

TEST(BridgeArgs, TrailingJunkIsIgnored) {
  Parsed p;
  ASSERT_TRUE(parse({"--num-slots=12abc"}, p));
  EXPECT_EQ(p.slots, 12u);
}

TEST(BridgeArgs, UnknownKeysAreIgnored) {
  Parsed p;
  ASSERT_TRUE(parse({"--bogus=1", "positional", "--port"}, p));
  EXPECT_EQ(p.unknown, 3);
  EXPECT_EQ(p.port, 0);
}

TEST(BridgeArgs, RepeatedKeyLastWins) {
  Parsed p;
  ASSERT_TRUE(parse({"--port=1", "--port=2"}, p));
  EXPECT_EQ(p.port, 2);
}

TEST(BridgeArgs, EmptyStringAndNullElements) {
  Parsed p;
  ASSERT_TRUE(parse({"", nullptr, "--port=5"}, p));
  EXPECT_EQ(p.unknown, 2);
  EXPECT_EQ(p.port, 5);
}

TEST(BridgeArgs, EmptyListSucceeds) {
  Parsed p;
  EXPECT_TRUE(ba::for_each_arg(0, nullptr, "test", [&](const std::string &) {
    FAIL() << "handler must not run";
  }));
}

TEST(BridgeArgs, FieldsSetBeforeFailureStay) {
  Parsed p;
  EXPECT_FALSE(parse({"--port=9", "--num-slots=x", "--device=late"}, p));
  EXPECT_EQ(p.port, 9);
  EXPECT_EQ(p.device, ""); // stops at the first bad argument
}
