// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/proxy/ppp_messaging_proxy.h"

#include <algorithm>

#include "ppapi/c/ppp_messaging.h"
#include "ppapi/proxy/host_dispatcher.h"
#include "ppapi/proxy/plugin_resource_tracker.h"
#include "ppapi/proxy/plugin_var_tracker.h"
#include "ppapi/proxy/ppapi_messages.h"
#include "ppapi/proxy/serialized_var.h"
#include "ppapi/shared_impl/ppapi_globals.h"
#include "ppapi/shared_impl/proxy_lock.h"
#include "ppapi/shared_impl/var_tracker.h"

namespace ppapi {
namespace proxy {

PPP_Messaging_Proxy::PPP_Messaging_Proxy(Dispatcher* dispatcher)
    : InterfaceProxy(dispatcher),
      ppp_messaging_impl_(NULL) {
  if (dispatcher->IsPlugin()) {
    ppp_messaging_impl_ = static_cast<const PPP_Messaging*>(
        dispatcher->local_get_interface()(PPP_MESSAGING_INTERFACE));
  }
}

PPP_Messaging_Proxy::~PPP_Messaging_Proxy() {
}

bool PPP_Messaging_Proxy::OnMessageReceived(const IPC::Message& msg) {
  if (!dispatcher()->IsPlugin())
    return false;

  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(PPP_Messaging_Proxy, msg)
    IPC_MESSAGE_HANDLER(PpapiMsg_PPPMessaging_HandleMessage,
                        OnMsgHandleMessage)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void PPP_Messaging_Proxy::OnMsgHandleMessage(
    PP_Instance instance, SerializedVarReceiveInput message_data) {
  PP_Var received_var(message_data.GetForInstance(dispatcher(), instance));
  // SerializedVarReceiveInput will decrement the reference count, but we want
  // to give the recipient a reference.
  PpapiGlobals::Get()->GetVarTracker()->AddRefVar(received_var);
  CallWhileUnlocked(ppp_messaging_impl_->HandleMessage,
                    instance,
                    received_var);
}

}  // namespace proxy
}  // namespace ppapi
