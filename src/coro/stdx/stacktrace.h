#ifndef CORO_STDX_STACKTRACE_H
#define CORO_STDX_STACKTRACE_H

#include <memory>
#include <string>

namespace coro::stdx {

class stacktrace {
 public:
  stacktrace(const stacktrace&);
  stacktrace(stacktrace&&) noexcept;

  ~stacktrace();

  stacktrace& operator=(const stacktrace&);
  stacktrace& operator=(stacktrace&&) noexcept;

  bool empty() const noexcept;

  static stacktrace current() noexcept;

 private:
  struct Impl;

  explicit stacktrace(std::unique_ptr<Impl>);

  friend Impl* GetImpl(stacktrace* s) { return s->impl_.get(); }
  friend const Impl* GetImpl(const stacktrace* s) { return s->impl_.get(); }

  std::unique_ptr<Impl> impl_;
};

}  // namespace coro::stdx

namespace coro {

std::string GetHtmlStacktrace(const stdx::stacktrace&);
std::string ToString(const stdx::stacktrace&);

}  // namespace coro

#endif  // CORO_STDX_STACKTRACE_H