// Copyright 2021 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef GRPC_SRC_CORE_LIB_PROMISE_LOOP_H
#define GRPC_SRC_CORE_LIB_PROMISE_LOOP_H

#include <grpc/support/port_platform.h>

#include <new>
#include <type_traits>
#include <utility>

#include "y_absl/status/status.h"
#include "y_absl/status/statusor.h"
#include "y_absl/types/variant.h"

#include "src/core/lib/promise/detail/promise_factory.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

// Special type - signals to loop to take another iteration, instead of
// finishing
struct Continue {};

// Result of polling a loop promise - either Continue looping, or return a value
// T
template <typename T>
using LoopCtl = y_absl::variant<Continue, T>;

namespace promise_detail {

template <typename T>
struct LoopTraits;

template <typename T>
struct LoopTraits<LoopCtl<T>> {
  using Result = T;
  static LoopCtl<T> ToLoopCtl(LoopCtl<T> value) { return value; }
};

template <typename T>
struct LoopTraits<y_absl::StatusOr<LoopCtl<T>>> {
  using Result = y_absl::StatusOr<T>;
  static LoopCtl<Result> ToLoopCtl(y_absl::StatusOr<LoopCtl<T>> value) {
    if (!value.ok()) return value.status();
    const auto& inner = *value;
    if (y_absl::holds_alternative<Continue>(inner)) return Continue{};
    return y_absl::get<T>(inner);
  }
};

template <>
struct LoopTraits<y_absl::StatusOr<LoopCtl<y_absl::Status>>> {
  using Result = y_absl::Status;
  static LoopCtl<Result> ToLoopCtl(
      y_absl::StatusOr<LoopCtl<y_absl::Status>> value) {
    if (!value.ok()) return value.status();
    const auto& inner = *value;
    if (y_absl::holds_alternative<Continue>(inner)) return Continue{};
    return y_absl::get<y_absl::Status>(inner);
  }
};

template <typename F>
class Loop {
 private:
  using Factory = promise_detail::RepeatedPromiseFactory<void, F>;
  using PromiseType = decltype(std::declval<Factory>().Make());
  using PromiseResult = typename PromiseType::Result;

 public:
  using Result = typename LoopTraits<PromiseResult>::Result;

  explicit Loop(F f) : factory_(std::move(f)), promise_(factory_.Make()) {}
  ~Loop() { promise_.~PromiseType(); }

  Loop(Loop&& loop) noexcept
      : factory_(std::move(loop.factory_)),
        promise_(std::move(loop.promise_)) {}

  Loop(const Loop& loop) = delete;
  Loop& operator=(const Loop& loop) = delete;

  Poll<Result> operator()() {
    while (true) {
      // Poll the inner promise.
      auto promise_result = promise_();
      // If it returns a value:
      if (auto* p = promise_result.value_if_ready()) {
        //  - then if it's Continue, destroy the promise and recreate a new one
        //  from our factory.
        auto lc = LoopTraits<PromiseResult>::ToLoopCtl(*p);
        if (y_absl::holds_alternative<Continue>(lc)) {
          promise_.~PromiseType();
          new (&promise_) PromiseType(factory_.Make());
          continue;
        }
        //  - otherwise there's our result... return it out.
        return y_absl::get<Result>(lc);
      } else {
        // Otherwise the inner promise was pending, so we are pending.
        return Pending();
      }
    }
  }

 private:
  GPR_NO_UNIQUE_ADDRESS Factory factory_;
  GPR_NO_UNIQUE_ADDRESS union {
    GPR_NO_UNIQUE_ADDRESS PromiseType promise_;
  };
};

}  // namespace promise_detail

// Looping combinator.
// Expects F returns LoopCtl<T> - if it's Continue, then run the loop again -
// otherwise yield the returned value as the result of the loop.
template <typename F>
promise_detail::Loop<F> Loop(F f) {
  return promise_detail::Loop<F>(std::move(f));
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_LOOP_H
