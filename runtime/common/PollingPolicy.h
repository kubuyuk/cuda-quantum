/*******************************************************************************
 * Copyright (c) 2026 NVIDIA Corporation & Affiliates.                         *
 * All rights reserved.                                                        *
 *                                                                             *
 * This source code and the accompanying materials are made available under    *
 * the terms of the Apache License 2.0 which accompanies this distribution.    *
 ******************************************************************************/

#pragma once

#include <chrono>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

/// @file PollingPolicy.h
/// Shared building blocks for the "poll a remote job until it is done" and
/// "retry a fetch with a delay between attempts" loops used by the REST
/// server helpers and executors.
///
/// The helpers are stateless (all state lives on the caller's stack), so they
/// are as thread-safe as the callables passed in. Every loop takes an
/// optional `Environment` (a clock and a sleep function) so tests can replace
/// real time with a fake.
///
/// Both loops are timing-neutral by default: no deadline, no back-off and no
/// extra sleeps are added; they sleep exactly what the caller's interval
/// callable returns, at the same point in the sequence as the hand-written
/// loops they replace.

namespace cudaq::polling {

/// @brief Real clock and sleep. Test code supplies a type with the same two
/// members to simulate time.
struct SystemEnvironment {
  std::chrono::steady_clock::time_point now() const {
    return std::chrono::steady_clock::now();
  }
  void sleep(std::chrono::microseconds d) const {
    std::this_thread::sleep_for(d);
  }
};

/// @brief Optional limits. A zero deadline (the default) means "no deadline".
struct Options {
  /// Maximum time since the loop started. It is checked only when the loop
  /// is about to sleep, and expires when elapsed time >= deadline.
  std::chrono::microseconds deadline = std::chrono::microseconds::zero();
};

/// @brief An interval callable that ignores the latest response.
inline auto constantInterval(std::chrono::microseconds d) {
  return [d](const auto &) { return d; };
}

/// @brief Whole-second delay for `attempt` (0-based): `base` for the first
/// attempt, `base * factor^attempt` afterwards, truncated to whole seconds
/// (the arithmetic of the original qBraid loop, kept bit-for-bit).
inline auto exponentialBackoffSeconds(int base, float factor) {
  return [base, factor](int attempt) {
    int sleepTime = (attempt == 0) ? base : base * std::pow(factor, attempt);
    return std::chrono::seconds(sleepTime);
  };
}

/// @brief Poll until `isDone(response)` is true.
///
/// Sequence (identical to the loop previously in `future::get()`):
///   check `initial`; while not done { interval = nextInterval(response);
///   sleep(interval); response = fetch(); check }.
/// The check always runs before the first sleep, the interval is re-queried
/// from the latest response on every iteration, and any exception thrown by
/// a callable propagates unchanged.
///
/// @param initial First response (already fetched by the caller).
/// @param isDone `bool(const Response&)`.
/// @param fetch `Response()`; fetches the next response.
/// @param nextInterval `std::chrono::microseconds(const Response&)` (any
/// duration convertible to microseconds).
/// @throws std::runtime_error if `options.deadline` is set and has expired.
template <typename Response, typename IsDone, typename Fetch,
          typename NextInterval, typename Environment = SystemEnvironment>
Response pollUntilDone(Response initial, IsDone &&isDone, Fetch &&fetch,
                       NextInterval &&nextInterval, Options options = {},
                       const Environment &env = {}) {
  const bool hasDeadline = options.deadline > std::chrono::microseconds::zero();
  const auto start = hasDeadline ? env.now() : decltype(env.now()){};
  Response response = std::move(initial);
  while (!isDone(response)) {
    std::chrono::microseconds interval = nextInterval(response);
    if (hasDeadline && env.now() - start >= options.deadline)
      throw std::runtime_error("Timed out while polling for job completion.");
    env.sleep(interval);
    response = fetch();
  }
  return response;
}

/// @brief Run `attempt(i)` for i in [0, maxAttempts) until it yields a value.
///
/// Sequence (identical to the loop previously in qBraid `processResults`):
///   for i in 0..maxAttempts-1 { try { r = attempt(i); if r return r; (not
///   ready -> sleep) } catch (std::exception &e) { onError(e, i); (may
///   throw to terminate) -> sleep } }
/// where "sleep" happens only if `i < maxAttempts - 1`, for `delay(i)`
/// seconds, after `onRetry(i, delay, threw)` is called (for logging). There
/// is no sleep before the first attempt, none after the last, and an
/// exception thrown by `onError` (or any non-std exception) propagates out.
/// A std::exception thrown by `attempt` itself, including one meant to be
/// terminal, is routed to `onError`, exactly as with the original try/catch.
///
/// @return the value, or std::nullopt when all attempts are exhausted (the
/// caller decides how to report that).
/// @throws std::runtime_error if `options.deadline` is set and has expired
/// when about to sleep.
template <typename Attempt, typename OnError, typename Delay, typename OnRetry,
          typename Environment = SystemEnvironment>
auto retryWithDelay(int maxAttempts, Attempt &&attempt, OnError &&onError,
                    Delay &&delay, OnRetry &&onRetry, Options options = {},
                    const Environment &env = {}) -> decltype(attempt(0)) {
  const bool hasDeadline = options.deadline > std::chrono::microseconds::zero();
  const auto start = hasDeadline ? env.now() : decltype(env.now()){};
  auto pause = [&](int i, bool threw) {
    if (i >= maxAttempts - 1)
      return;
    auto d = delay(i);
    onRetry(i, d, threw);
    if (hasDeadline && env.now() - start >= options.deadline)
      throw std::runtime_error("Timed out while retrying.");
    env.sleep(std::chrono::duration_cast<std::chrono::microseconds>(d));
  };
  for (int i = 0; i < maxAttempts; ++i) {
    bool threw = false;
    try {
      auto result = attempt(i);
      if (result)
        return result;
    } catch (const std::exception &e) {
      onError(e, i);
      threw = true;
    }
    pause(i, threw);
  }
  return std::nullopt;
}

} // namespace cudaq::polling
