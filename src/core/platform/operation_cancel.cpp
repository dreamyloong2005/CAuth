#include "core/platform/operation_cancel.hpp"

namespace cauth::core::platform {
namespace {

thread_local OperationCancelContext g_current_thread_operation_cancel{};

} // namespace

ScopedCurrentThreadOperationCancel::ScopedCurrentThreadOperationCancel(
    OperationCancelContext context) noexcept
    : previous_(current_thread_operation_cancel_context()) {
    set_current_thread_operation_cancel(context);
}

ScopedCurrentThreadOperationCancel::~ScopedCurrentThreadOperationCancel() {
    set_current_thread_operation_cancel(previous_);
}

void set_current_thread_operation_cancel(OperationCancelContext context) noexcept {
    g_current_thread_operation_cancel = context;
}

void clear_current_thread_operation_cancel() noexcept {
    g_current_thread_operation_cancel = {};
}

OperationCancelContext current_thread_operation_cancel_context() noexcept {
    return g_current_thread_operation_cancel;
}

bool current_thread_operation_cancel_requested() noexcept {
    const auto context = current_thread_operation_cancel_context();
    return context.cancel_hook != nullptr && context.cancel_hook(context.user_data);
}

} // namespace cauth::core::platform
