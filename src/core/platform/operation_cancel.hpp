#ifndef CAUTH_CORE_PLATFORM_OPERATION_CANCEL_HPP
#define CAUTH_CORE_PLATFORM_OPERATION_CANCEL_HPP

namespace cauth::core::platform {

using OperationCancelHook = bool (*)(void* user_data);

struct OperationCancelContext {
    OperationCancelHook cancel_hook = nullptr;
    void* user_data = nullptr;

    [[nodiscard]] bool empty() const noexcept { return cancel_hook == nullptr; }
};

class ScopedCurrentThreadOperationCancel {
  public:
    explicit ScopedCurrentThreadOperationCancel(OperationCancelContext context) noexcept;
    ~ScopedCurrentThreadOperationCancel();

    ScopedCurrentThreadOperationCancel(const ScopedCurrentThreadOperationCancel&) = delete;
    ScopedCurrentThreadOperationCancel& operator=(const ScopedCurrentThreadOperationCancel&) = delete;

  private:
    OperationCancelContext previous_{};
};

void set_current_thread_operation_cancel(OperationCancelContext context) noexcept;
void clear_current_thread_operation_cancel() noexcept;
[[nodiscard]] OperationCancelContext current_thread_operation_cancel_context() noexcept;
[[nodiscard]] bool current_thread_operation_cancel_requested() noexcept;

} // namespace cauth::core::platform

#endif
