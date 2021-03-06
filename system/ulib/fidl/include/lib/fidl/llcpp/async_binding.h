// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_ASYNC_CPP_ASYNC_BIND_INTERNAL_H_
#define LIB_FIDL_ASYNC_CPP_ASYNC_BIND_INTERNAL_H_

#include <lib/async/dispatcher.h>
#include <lib/async/task.h>
#include <lib/async/wait.h>
#include <lib/fidl/llcpp/transaction.h>
#include <lib/fit/function.h>
#include <lib/fit/result.h>
#include <lib/sync/completion.h>
#include <lib/zx/channel.h>
#include <zircon/fidl.h>

#include <mutex>

namespace fidl {

// Reason for unbinding the channel. Accompanied in the channel unbound callback by a zx_status_t
// status argument.
enum class UnboundReason {
  // The user invoked Unbind(). Status is ZX_OK.
  kUnbind,
  // Server only. The user invoked Close(epitaph) on a BindingRef or Completer and the epitaph was
  // sent. Status is the status from sending the epitaph.
  kClose,
  // The channel peer was closed. For a server, status is ZX_ERR_PEER_CLOSED. For a client, status
  // is the epitaph. If no epitaph was sent, the behavior is equivalent to having received a
  // ZX_ERR_PEER_CLOSED epitaph.
  kPeerClosed,
  // An unexpected channel read/write error or dispatcher error occurred. Accompanying status is the
  // error code.
  kInternalError,
};

template <typename Interface>
using OnUnboundFn = fit::callback<void(Interface*, UnboundReason, zx_status_t, zx::channel)>;

namespace internal {

using TypeErasedServerDispatchFn = bool (*)(void*, fidl_msg_t*, ::fidl::Transaction*);
using TypeErasedOnUnboundFn = fit::callback<void(void*, UnboundReason, zx_status_t, zx::channel)>;

class AsyncTransaction;
class ClientBase;

class AsyncBinding {
 public:
  using DispatchFn =
      fit::function<void(std::shared_ptr<AsyncBinding>&, fidl_msg_t*, bool*, zx_status_t*)>;

  // Creates a binding that remains bound until either it is explicitly unbound via |Unbind|,
  // a peer close is received in the channel from the remote end, or all transactions generated
  // from it are destructed and an error occurred (either Close() is called from a transaction or
  // an internal error like not being able to write to the channel occur).
  // The binding is destroyed once no more references are held, including the one returned by
  // this static method.
  static std::shared_ptr<AsyncBinding> CreateServerBinding(
      async_dispatcher_t* dispatcher, zx::channel channel, void* impl,
      TypeErasedServerDispatchFn dispatch_fn, TypeErasedOnUnboundFn on_unbound_fn);

  static std::shared_ptr<AsyncBinding> CreateClientBinding(
      async_dispatcher_t* dispatcher, zx::channel channel, void* impl, DispatchFn dispatch_fn,
      TypeErasedOnUnboundFn on_unbound_fn);

  ~AsyncBinding();

  zx_status_t BeginWait() __TA_EXCLUDES(lock_);

  zx_status_t EnableNextDispatch() __TA_EXCLUDES(lock_);

  void Unbind(std::shared_ptr<AsyncBinding>&& calling_ref) __TA_EXCLUDES(lock_) {
    UnbindInternal(std::move(calling_ref), nullptr);
  }

  void Close(std::shared_ptr<AsyncBinding>&& calling_ref, zx_status_t epitaph)
      __TA_EXCLUDES(lock_) {
    UnbindInternal(std::move(calling_ref), &epitaph);
  }

  zx::unowned_channel channel() const { return zx::unowned_channel(channel_); }

 protected:
  AsyncBinding(async_dispatcher_t* dispatcher, zx::channel channel, void* impl, bool is_server,
               TypeErasedOnUnboundFn on_unbound_fn, DispatchFn dispatch_fn);

 private:
  friend fidl::internal::AsyncTransaction;

  struct UnboundTask {
    async_task_t task;
    TypeErasedOnUnboundFn on_unbound_fn;
    void* intf;
    zx::channel channel;
    zx_status_t status;
    UnboundReason reason;
  };

  static void OnMessage(async_dispatcher_t* dispatcher, async_wait_t* wait, zx_status_t status,
                        const zx_packet_signal_t* signal) {
    static_assert(std::is_standard_layout<AsyncBinding>::value, "Need offsetof.");
    static_assert(offsetof(AsyncBinding, wait_) == 0, "Cast async_wait_t* to AsyncBinding*.");
    reinterpret_cast<AsyncBinding*>(wait)->MessageHandler(status, signal);
  }

  static void OnUnboundTask(async_dispatcher_t* dispatcher, async_task_t* task,
                            zx_status_t status) {
    static_assert(std::is_standard_layout<UnboundTask>::value, "Need offsetof.");
    static_assert(offsetof(UnboundTask, task) == 0, "Cast async_task_t* to UnboundTask*.");
    auto* unbound_task = reinterpret_cast<UnboundTask*>(task);
    unbound_task->on_unbound_fn(unbound_task->intf, unbound_task->reason, unbound_task->status,
                                std::move(unbound_task->channel));
    delete unbound_task;
  }

  void MessageHandler(zx_status_t status, const zx_packet_signal_t* signal) __TA_EXCLUDES(lock_);

  // Used by both Close() and Unbind().
  void UnbindInternal(std::shared_ptr<AsyncBinding>&& calling_ref, zx_status_t* epitaph)
      __TA_EXCLUDES(lock_);

  // Triggered by explicit Unbind(), channel peer closed, or other channel/dispatcher error in the
  // context of a dispatcher thread with exclusive ownership of the internal binding reference. If
  // the binding is still bound, waits for all other references to be released, sends the epitaph
  // (except for Unbind()), and invokes the error handler if specified.
  void OnUnbind(zx_status_t status, UnboundReason reason) __TA_EXCLUDES(lock_);

  // Destroys calling_ref and waits for the release of any other outstanding references to the
  // binding. Recovers the channel endpoint.
  zx::channel WaitForDelete(std::shared_ptr<AsyncBinding>&& calling_ref);

  // Invokes OnUnbind() with the appropriate arguments based on the status.
  void OnDispatchError(zx_status_t error);

  // First member of struct so an async_wait_t* can be casted to its containing AsyncBinding*.
  async_wait_t wait_ __TA_GUARDED(lock_);

  async_dispatcher_t* dispatcher_ = nullptr;
  sync_completion_t* on_delete_ = nullptr;
  zx::channel channel_ = {};
  void* interface_ = nullptr;
  TypeErasedOnUnboundFn on_unbound_fn_ = {};
  const DispatchFn dispatch_fn_;
  std::shared_ptr<AsyncBinding> keep_alive_ = {};
  zx::channel* out_channel_ = nullptr;
  const bool is_server_;

  std::mutex lock_;
  struct {
    UnboundReason reason;
    zx_status_t status;
  } unbind_info_ __TA_GUARDED(lock_) = {UnboundReason::kUnbind, ZX_OK};
  bool unbind_ __TA_GUARDED(lock_) = false;
  bool begun_ __TA_GUARDED(lock_) = false;
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_ASYNC_CPP_ASYNC_BIND_INTERNAL_H_
