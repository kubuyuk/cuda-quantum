/*******************************************************************************
 * Copyright (c) 2026 NVIDIA Corporation & Affiliates.                         *
 * All rights reserved.                                                        *
 *                                                                             *
 * This source code and the accompanying materials are made available under    *
 * the terms of the Apache License 2.0 which accompanies this distribution.    *
 ******************************************************************************/

#include "common/PollingPolicy.h"
#include <gtest/gtest.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std::chrono_literals;
using cudaq::polling::Options;

namespace {
/// Fake clock + sleep: sleeping advances the clock and records the interval;
/// `log` records the order of every event.
struct FakeEnv {
  struct State {
    std::chrono::steady_clock::time_point t{};
    std::vector<std::chrono::microseconds> sleeps;
    std::vector<std::string> log;
  };
  State *s;
  std::chrono::steady_clock::time_point now() const { return s->t; }
  void sleep(std::chrono::microseconds d) const {
    s->log.push_back("sleep");
    s->sleeps.push_back(d);
    s->t += d;
  }
};
} // namespace

TEST(PollingPolicyTester, doneImmediatelyNeverSleepsOrFetches) {
  FakeEnv::State st;
  int fetches = 0;
  auto r = cudaq::polling::pollUntilDone(
      7, [](int) { return true; },
      [&] {
        ++fetches;
        return 0;
      },
      cudaq::polling::constantInterval(5s), Options{}, FakeEnv{&st});
  EXPECT_EQ(r, 7);
  EXPECT_EQ(fetches, 0);
  EXPECT_TRUE(st.sleeps.empty());
}

TEST(PollingPolicyTester, sleepsBeforeEveryFetchAndStopsOnDone) {
  FakeEnv::State st;
  int next = 0;
  auto r = cudaq::polling::pollUntilDone(
      0,
      [&](int v) {
        st.log.push_back("done?");
        return v == 3;
      },
      [&] {
        st.log.push_back("fetch");
        return ++next;
      },
      [&](int) {
        st.log.push_back("interval");
        return 100us;
      },
      Options{}, FakeEnv{&st});
  EXPECT_EQ(r, 3);
  EXPECT_EQ(st.sleeps.size(), 3u);
  std::vector<std::string> expected = {"done?"};
  for (int i = 0; i < 3; ++i)
    for (auto e : {"interval", "sleep", "fetch", "done?"})
      expected.push_back(e);
  EXPECT_EQ(st.log, expected);
}

TEST(PollingPolicyTester, intervalIsRequeriedFromLatestResponse) {
  FakeEnv::State st;
  // Response value doubles as "queue position"; interval follows it.
  std::vector<int> script = {5, 3, 1, 0};
  std::size_t i = 0;
  cudaq::polling::pollUntilDone(
      script[0], [](int v) { return v == 0; }, [&] { return script[++i]; },
      [](int v) { return std::chrono::seconds(v); }, Options{}, FakeEnv{&st});
  std::vector<std::chrono::microseconds> expected = {5s, 3s, 1s};
  EXPECT_EQ(st.sleeps, expected);
}

TEST(PollingPolicyTester, exceptionsPropagateFromEveryCallable) {
  FakeEnv::State st;
  auto never = [](int) { return false; };
  auto zero = cudaq::polling::constantInterval(1us);
  EXPECT_THROW(cudaq::polling::pollUntilDone(
                   0, [](int) -> bool { throw std::logic_error("done"); },
                   [] { return 0; }, zero, Options{}, FakeEnv{&st}),
               std::logic_error);
  EXPECT_THROW(cudaq::polling::pollUntilDone(
                   0, never, []() -> int { throw std::runtime_error("get"); },
                   zero, Options{}, FakeEnv{&st}),
               std::runtime_error);
  EXPECT_THROW(cudaq::polling::pollUntilDone(
                   0, never, [] { return 0; },
                   [](int) -> std::chrono::microseconds {
                     throw std::out_of_range("i");
                   },
                   Options{}, FakeEnv{&st}),
               std::out_of_range);
  // A fetch failure happens after the sleep (sleep-before-fetch ordering).
  EXPECT_EQ(st.sleeps.size(), 1u);
}

TEST(PollingPolicyTester, noDeadlineByDefaultRunsForever) {
  FakeEnv::State st;
  int n = 0;
  cudaq::polling::pollUntilDone(
      0, [](int v) { return v == 100000; }, [&] { return ++n; },
      cudaq::polling::constantInterval(1h), Options{}, FakeEnv{&st});
  EXPECT_EQ(st.sleeps.size(), 100000u);
}

TEST(PollingPolicyTester, deadlineThrowsWhenExpiredBeforeSleeping) {
  FakeEnv::State st;
  Options opts;
  opts.deadline = 10s;
  int n = 0;
  EXPECT_THROW(cudaq::polling::pollUntilDone(
                   0, [](int) { return false; }, [&] { return ++n; },
                   cudaq::polling::constantInterval(4s), opts, FakeEnv{&st}),
               std::runtime_error);
  // Elapsed 0, 4, 8 -> sleeps; at 12 >= 10 -> throws without sleeping.
  EXPECT_EQ(st.sleeps.size(), 3u);
  EXPECT_EQ(n, 3);
}

TEST(PollingPolicyTester, deadlineDoesNotAffectLoopThatFinishesInTime) {
  FakeEnv::State st;
  Options opts;
  opts.deadline = 10s;
  int n = 0;
  auto r = cudaq::polling::pollUntilDone(
      0, [](int v) { return v == 2; }, [&] { return ++n; },
      cudaq::polling::constantInterval(4s), opts, FakeEnv{&st});
  EXPECT_EQ(r, 2);
}

TEST(PollingPolicyTester, backoffSecondsMatchOriginalArithmetic) {
  auto d = cudaq::polling::exponentialBackoffSeconds(2, 2.0f);
  EXPECT_EQ(d(0), 2s);
  EXPECT_EQ(d(1), 4s);
  EXPECT_EQ(d(2), 8s);
  auto frac = cudaq::polling::exponentialBackoffSeconds(3, 1.5f);
  EXPECT_EQ(frac(1), 4s); // 4.5 truncated, as the original int conversion
}

namespace {
auto noLog = [](int, std::chrono::seconds, bool) {};
auto backoff = cudaq::polling::exponentialBackoffSeconds(2, 2.0f);
} // namespace

TEST(PollingPolicyTester, retryReturnsFirstValueWithoutSleeping) {
  FakeEnv::State st;
  auto r = cudaq::polling::retryWithDelay(
      3, [](int) -> std::optional<int> { return 42; },
      [](const std::exception &, int) {}, backoff, noLog, Options{},
      FakeEnv{&st});
  ASSERT_TRUE(r);
  EXPECT_EQ(*r, 42);
  EXPECT_TRUE(st.sleeps.empty());
}

TEST(PollingPolicyTester, retryNotReadySleepsBetweenAttemptsOnly) {
  FakeEnv::State st;
  int calls = 0;
  auto r = cudaq::polling::retryWithDelay(
      3,
      [&](int) -> std::optional<int> {
        ++calls;
        return std::nullopt;
      },
      [](const std::exception &, int) {}, backoff, noLog, Options{},
      FakeEnv{&st});
  EXPECT_FALSE(r);
  EXPECT_EQ(calls, 3);
  // 3 attempts -> 2 sleeps (none before the first, none after the last).
  std::vector<std::chrono::microseconds> expected = {2s, 4s};
  EXPECT_EQ(st.sleeps, expected);
}

TEST(PollingPolicyTester, retrySucceedsOnLastAttempt) {
  FakeEnv::State st;
  auto r = cudaq::polling::retryWithDelay(
      3,
      [](int i) -> std::optional<int> {
        if (i < 2)
          return std::nullopt;
        return i;
      },
      [](const std::exception &, int) {}, backoff, noLog, Options{},
      FakeEnv{&st});
  ASSERT_TRUE(r);
  EXPECT_EQ(*r, 2);
  EXPECT_EQ(st.sleeps.size(), 2u);
}

TEST(PollingPolicyTester, retryRoutesStdExceptionsToHandlerAndRetries) {
  FakeEnv::State st;
  std::vector<int> handled;
  std::vector<bool> threwFlags;
  auto r = cudaq::polling::retryWithDelay(
      3,
      [](int i) -> std::optional<int> {
        if (i < 2)
          throw std::runtime_error("boom");
        return 9;
      },
      [&](const std::exception &e, int i) {
        EXPECT_STREQ(e.what(), "boom");
        handled.push_back(i);
      },
      backoff,
      [&](int, std::chrono::seconds, bool t) { threwFlags.push_back(t); },
      Options{}, FakeEnv{&st});
  ASSERT_TRUE(r);
  EXPECT_EQ(*r, 9);
  EXPECT_EQ(handled, (std::vector<int>{0, 1}));
  EXPECT_EQ(threwFlags, (std::vector<bool>{true, true}));
  EXPECT_EQ(st.sleeps.size(), 2u);
}

TEST(PollingPolicyTester, retryHandlerThrowTerminatesWithoutSleeping) {
  FakeEnv::State st;
  int calls = 0;
  EXPECT_THROW(cudaq::polling::retryWithDelay(
                   3,
                   [&](int) -> std::optional<int> {
                     ++calls;
                     throw std::runtime_error("401");
                   },
                   [](const std::exception &, int) {
                     throw std::logic_error("terminal");
                   },
                   backoff, noLog, Options{}, FakeEnv{&st}),
               std::logic_error);
  EXPECT_EQ(calls, 1);
  EXPECT_TRUE(st.sleeps.empty());
}

TEST(PollingPolicyTester, retryNonStdExceptionPropagates) {
  FakeEnv::State st;
  EXPECT_THROW(cudaq::polling::retryWithDelay(
                   3, [](int) -> std::optional<int> { throw 5; },
                   [](const std::exception &, int) {}, backoff, noLog,
                   Options{}, FakeEnv{&st}),
               int);
}

TEST(PollingPolicyTester, retryLastAttemptExceptionDoesNotSleep) {
  FakeEnv::State st;
  auto r = cudaq::polling::retryWithDelay(
      2, [](int) -> std::optional<int> { throw std::runtime_error("x"); },
      [](const std::exception &, int) {}, backoff, noLog, Options{},
      FakeEnv{&st});
  EXPECT_FALSE(r);
  EXPECT_EQ(st.sleeps.size(), 1u);
}

TEST(PollingPolicyTester, retryDeadline) {
  FakeEnv::State st;
  Options opts;
  opts.deadline = 3s;
  EXPECT_THROW(cudaq::polling::retryWithDelay(
                   5, [](int) -> std::optional<int> { return std::nullopt; },
                   [](const std::exception &, int) {}, backoff, noLog, opts,
                   FakeEnv{&st}),
               std::runtime_error);
  // Sleeps 2s (elapsed 0), then elapsed 2 < 3 -> sleeps 4s, then 6 >= 3.
  EXPECT_EQ(st.sleeps.size(), 2u);
}
