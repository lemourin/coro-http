#ifndef CORO_HTTP_GENERATOR_H
#define CORO_HTTP_GENERATOR_H

#include <exception>
#include <memory>
#include <utility>

#include "coro/stdx/coroutine.h"
#include "coro/task.h"

namespace coro {

template <typename T>
class Generator;

namespace detail {
template <typename T>
class async_generator_iterator;
class async_generator_yield_operation;
class async_generator_advance_operation;

class async_generator_promise_base {
 public:
  async_generator_promise_base() noexcept : exception_(nullptr) {
    // Other variables left intentionally uninitialised as they're
    // only referenced in certain states by which time they should
    // have been initialised.
  }

  async_generator_promise_base(const async_generator_promise_base& other) =
      delete;
  async_generator_promise_base& operator=(
      const async_generator_promise_base& other) = delete;

  [[nodiscard]] stdx::suspend_always initial_suspend() const noexcept {
    return {};
  }

  async_generator_yield_operation final_suspend() noexcept;

  void unhandled_exception() noexcept { exception_ = std::current_exception(); }

  void return_void() noexcept {}

  /// Query if the generator has reached the end of the sequence.
  ///
  /// Only valid to call after resuming from an awaited advance operation.
  /// i.e. Either a begin() or iterator::operator++() operation.
  bool finished() const noexcept { return current_value_ == nullptr; }

  void rethrow_if_unhandled_exception() {
    if (exception_) {
      std::rethrow_exception(std::move(exception_));
    }
  }

 protected:
  async_generator_yield_operation internal_yield_value() noexcept;

 private:
  friend class async_generator_yield_operation;
  friend class async_generator_advance_operation;

  std::exception_ptr exception_;
  stdx::coroutine_handle<void> consumer_coroutine_;

 protected:
  void* current_value_;
};

class async_generator_yield_operation final {
 public:
  async_generator_yield_operation(
      stdx::coroutine_handle<void> consumer) noexcept
      : consumer_(consumer) {}

  [[nodiscard]] bool await_ready() const noexcept { return false; }

  stdx::coroutine_handle<void> await_suspend(
      stdx::coroutine_handle<void>) noexcept {
    return consumer_;
  }

  void await_resume() noexcept {}

 private:
  stdx::coroutine_handle<void> consumer_;
};

inline async_generator_yield_operation
async_generator_promise_base::final_suspend() noexcept {
  current_value_ = nullptr;
  return internal_yield_value();
}

inline async_generator_yield_operation
async_generator_promise_base::internal_yield_value() noexcept {
  return async_generator_yield_operation{consumer_coroutine_};
}

class async_generator_advance_operation {
 protected:
  async_generator_advance_operation(std::nullptr_t) noexcept
      : promise_(nullptr), producer_coroutine_() {}

  async_generator_advance_operation(
      async_generator_promise_base& promise,
      stdx::coroutine_handle<void> producer_coroutine) noexcept
      : promise_(std::addressof(promise)),
        producer_coroutine_(producer_coroutine) {}

 public:
  [[nodiscard]] bool await_ready() const noexcept { return false; }

  stdx::coroutine_handle<void> await_suspend(
      stdx::coroutine_handle<void> consumer_coroutine) noexcept {
    promise_->consumer_coroutine_ = consumer_coroutine;
    return producer_coroutine_;
  }

 protected:
  async_generator_promise_base* promise_;
  stdx::coroutine_handle<void> producer_coroutine_;
};

template <typename T>
class async_generator_promise final : public async_generator_promise_base {
  using value_type = std::remove_reference_t<T>;

 public:
  async_generator_promise() noexcept = default;

  Generator<T> get_return_object() noexcept;

  async_generator_yield_operation yield_value(value_type& value) noexcept {
    current_value_ = std::addressof(value);
    return internal_yield_value();
  }

  async_generator_yield_operation yield_value(value_type&& value) noexcept {
    return yield_value(value);
  }

  T& value() const noexcept { return *static_cast<T*>(current_value_); }
};

template <typename T>
class async_generator_promise<T&&> final : public async_generator_promise_base {
 public:
  async_generator_promise() noexcept = default;

  Generator<T> get_return_object() noexcept;

  async_generator_yield_operation yield_value(T&& value) noexcept {
    current_value_ = std::addressof(value);
    return internal_yield_value();
  }

  T&& value() const noexcept {
    return std::move(*static_cast<T*>(current_value_));
  }
};

template <typename T>
class async_generator_increment_operation final
    : public async_generator_advance_operation {
 public:
  using type = async_generator_iterator<T>&;

  async_generator_increment_operation(
      async_generator_iterator<T>& iterator) noexcept
      : async_generator_advance_operation(iterator.coroutine_.promise(),
                                          iterator.coroutine_),
        iterator_(iterator) {}

  async_generator_iterator<T>& await_resume();

 private:
  async_generator_iterator<T>& iterator_;
};

template <typename T>
class async_generator_iterator final {
  using promise_type = async_generator_promise<T>;
  using handle_type = stdx::coroutine_handle<promise_type>;

 public:
  using iterator_category = std::input_iterator_tag;
  // Not sure what type should be used for difference_type as we don't
  // allow calculating difference between two iterators.
  using difference_type = std::ptrdiff_t;
  using value_type = std::remove_reference_t<T>;
  using reference = std::add_lvalue_reference_t<T>;
  using pointer = std::add_pointer_t<value_type>;

  async_generator_iterator(std::nullptr_t) noexcept : coroutine_(nullptr) {}

  async_generator_iterator(handle_type coroutine) noexcept
      : coroutine_(coroutine) {}

  async_generator_increment_operation<T> operator++() noexcept {
    return async_generator_increment_operation<T>{*this};
  }

  reference operator*() const noexcept { return coroutine_.promise().value(); }

  bool operator==(const async_generator_iterator& other) const noexcept {
    return coroutine_ == other.coroutine_;
  }

  bool operator!=(const async_generator_iterator& other) const noexcept {
    return !(*this == other);
  }

 private:
  friend class async_generator_increment_operation<T>;

  handle_type coroutine_;
};

template <typename T>
async_generator_iterator<T>&
async_generator_increment_operation<T>::await_resume() {
  if (promise_->finished()) {
    // Update iterator to end()
    iterator_ = async_generator_iterator<T>{nullptr};
    promise_->rethrow_if_unhandled_exception();
  }

  return iterator_;
}

template <typename T>
class async_generator_begin_operation final
    : public async_generator_advance_operation {
  using promise_type = async_generator_promise<T>;
  using handle_type = stdx::coroutine_handle<promise_type>;

 public:
  using type = async_generator_iterator<T>;

  async_generator_begin_operation(std::nullptr_t) noexcept
      : async_generator_advance_operation(nullptr) {}

  async_generator_begin_operation(handle_type producer_coroutine) noexcept
      : async_generator_advance_operation(producer_coroutine.promise(),
                                          producer_coroutine) {}

  bool await_ready() const noexcept {
    return promise_ == nullptr ||
           async_generator_advance_operation::await_ready();
  }

  async_generator_iterator<T> await_resume() {
    if (promise_ == nullptr) {
      // Called begin() on the empty generator.
      return async_generator_iterator<T>{nullptr};
    } else if (promise_->finished()) {
      // Completed without yielding any values.
      promise_->rethrow_if_unhandled_exception();
      return async_generator_iterator<T>{nullptr};
    }

    return async_generator_iterator<T>{
        handle_type::from_promise(*static_cast<promise_type*>(promise_))};
  }
};
}  // namespace detail

template <typename T>
class [[nodiscard]] Generator {
 public:
  using promise_type = detail::async_generator_promise<T>;
  using iterator = detail::async_generator_iterator<T>;

  Generator() noexcept : coroutine_(nullptr) {}

  explicit Generator(promise_type& promise) noexcept
      : coroutine_(
            stdx::coroutine_handle<promise_type>::from_promise(promise)) {}

  Generator(Generator&& other) noexcept : coroutine_(other.coroutine_) {
    other.coroutine_ = nullptr;
  }

  ~Generator() {
    if (coroutine_) {
      coroutine_.destroy();
    }
  }

  Generator& operator=(Generator&& other) noexcept {
    Generator temp(std::move(other));
    swap(temp);
    return *this;
  }

  Generator(const Generator&) = delete;
  Generator& operator=(const Generator&) = delete;

  auto begin() noexcept {
    if (!coroutine_) {
      return detail::async_generator_begin_operation<T>{nullptr};
    }

    return detail::async_generator_begin_operation<T>{coroutine_};
  }

  auto end() noexcept { return iterator{nullptr}; }

  void swap(Generator& other) noexcept {
    using std::swap;
    swap(coroutine_, other.coroutine_);
  }

 private:
  stdx::coroutine_handle<promise_type> coroutine_;
};

template <typename T>
void swap(Generator<T>& a, Generator<T>& b) noexcept {
  a.swap(b);
}

namespace detail {
template <typename T>
Generator<T> async_generator_promise<T>::get_return_object() noexcept {
  return Generator<T>{*this};
}
}  // namespace detail

template <typename T, typename R>
concept GeneratorLike =
    requires(T v, detail::AwaitableTypeT<decltype(v.begin())> awaitable,
             stdx::coroutine_handle<void> handle) {
      awaitable.await_suspend(handle);
      { awaitable.await_ready() } -> stdx::same_as<bool>;
      { *awaitable.await_resume() } -> stdx::convertible_to<R>;
      v.end();
    };

}  // namespace coro

#endif  // CORO_HTTP_GENERATOR_H
