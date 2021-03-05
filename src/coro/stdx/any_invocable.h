#ifndef CORO_CLOUDSTORAGE_ANY_INVOCABLE_H
#define CORO_CLOUDSTORAGE_ANY_INVOCABLE_H

#include <memory>
#include <utility>

namespace coro::stdx {

namespace internal {

template <typename>
class any_invocable;

template <typename ReturnType, typename... ArgType>
class any_invocable<ReturnType(ArgType...)> {
 public:
  virtual ~any_invocable() = default;
  virtual ReturnType operator()(ArgType...) = 0;
};

template <typename Functor, typename ReturnType, typename... ArgType>
class any_invocable_impl : public any_invocable<ReturnType(ArgType...)> {
 public:
  explicit any_invocable_impl(Functor f) : f_(std::move(f)) {}

  ReturnType operator()(ArgType... args) final {
    return f_(std::move(args)...);
  }

 private:
  Functor f_;
};

template <typename Functor, typename ReturnType, typename... ArgType>
class any_invocable_ptr_impl : public any_invocable<ReturnType(ArgType...)> {
 public:
  explicit any_invocable_ptr_impl(std::unique_ptr<Functor> f)
      : f_(std::move(f)) {}

  ReturnType operator()(ArgType... args) final {
    return (*f_)(std::move(args)...);
  }

 private:
  std::unique_ptr<Functor> f_;
};

}  // namespace internal

template <typename>
class any_invocable;

template <typename ReturnType, typename... ArgType>
class any_invocable<ReturnType(ArgType...)> {
 public:
  template <typename T>
  any_invocable(T functor)
      : f_(std::make_unique<
            internal::any_invocable_impl<T, ReturnType, ArgType...>>(
            std::move(functor))) {}
  template <typename T>
  any_invocable(std::unique_ptr<T> functor)
      : f_(std::make_unique<
            internal::any_invocable_ptr_impl<T, ReturnType, ArgType...>>(
            std::move(functor))) {}

  ReturnType operator()(ArgType... args) { return (*f_)(std::move(args)...); }

 private:
  std::unique_ptr<internal::any_invocable<ReturnType(ArgType...)>> f_;
};

}  // namespace coro::stdx

#endif  // CORO_CLOUDSTORAGE_ANY_INVOCABLE_H
