#ifndef CORO_HTTP_GENERATOR_H
#define CORO_HTTP_GENERATOR_H

#include <coro/stdx/coroutine.h>
#include <coro/task.h>

#include <exception>
#include <memory>
#include <utility>

namespace coro {

template <class T>
class Generator {
 public:
  ~Generator() {
    if (promise_) {
      promise_->handle().destroy();
    }
  }

  Generator(const Generator&) = delete;
  Generator(Generator&& generator) noexcept
      : promise_(std::exchange(generator.promise_, nullptr)) {}

  Generator& operator=(const Generator&) = delete;
  Generator& operator=(Generator&& generator) noexcept {
    promise_ = std::exchange(generator.promise_, nullptr);
    return *this;
  }

  struct sentinel_iterator {};

  class iterator {
   public:
    explicit iterator(Generator* generator) : generator_(generator) {}

    bool operator==(const sentinel_iterator&) const {
      return generator_->promise_->handle().done();
    }

    iterator& operator++() {
      generator_->promise_->handle().resume();
      return *this;
    }

    T& operator*() { return *generator_->promise_->value_; }

    [[nodiscard]] bool await_ready() const { return true; }
    void await_suspend(stdx::coroutine_handle<void>) {}
    iterator& await_resume() { return *this; }

   private:
    Generator* generator_;
  };

  class promise_type {
   public:
    Generator get_return_object() { return Generator(this); }
    stdx::suspend_never initial_suspend() { return {}; }
    stdx::suspend_always final_suspend() { return {}; }
    void unhandled_exception() { std::terminate(); }

    void return_void() {}

    template <typename V>
    stdx::suspend_always yield_value(V&& value) {
      value_ = std::make_unique<T>(std::forward<V>(value));
      return {};
    }

    stdx::coroutine_handle<void> handle() {
      return stdx::coroutine_handle<promise_type>::from_promise(*this);
    }

   private:
    friend class iterator;
    friend class Generator;

    std::unique_ptr<T> value_;
  };

  iterator begin() { return iterator{this}; }
  sentinel_iterator end() { return {}; }

 private:
  explicit Generator(promise_type* promise) : promise_(promise) {}

  promise_type* promise_;
};

// clang-format off
template <typename T>
concept GeneratorLike = requires(T v) {
  { v.begin() } -> Awaitable;
  v.end();
};
// clang-format on

}  // namespace coro

#endif  // CORO_HTTP_GENERATOR_H
