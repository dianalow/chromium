// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "android_webview/browser/hardware_renderer.h"

#include "android_webview/browser/aw_gl_surface.h"
#include "android_webview/browser/browser_view_renderer_client.h"
#include "android_webview/browser/scoped_app_gl_state_restore.h"
#include "android_webview/public/browser/draw_gl.h"
#include "base/debug/trace_event.h"
#include "base/strings/string_number_conversions.h"
#include "content/public/browser/android/synchronous_compositor.h"
#include "content/public/browser/browser_thread.h"
#include "gpu/command_buffer/service/shader_translator_cache.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/transform.h"

namespace android_webview {

namespace {

base::LazyInstance<scoped_refptr<internal::DeferredGpuCommandService> >
    g_service = LAZY_INSTANCE_INITIALIZER;

}  // namespace

HardwareRenderer::HardwareRenderer(SharedRendererState* state)
    : shared_renderer_state_(state),
      last_egl_context_(eglGetCurrentContext()),
      renderer_manager_key_(GLViewRendererManager::GetInstance()->PushBack(
          shared_renderer_state_)) {
  DCHECK(last_egl_context_);
  if (!g_service.Get()) {
    g_service.Get() = new internal::DeferredGpuCommandService;
    content::SynchronousCompositor::SetGpuService(g_service.Get());
  }

  ScopedAppGLStateRestore state_restore(
      ScopedAppGLStateRestore::MODE_RESOURCE_MANAGEMENT);
  internal::ScopedAllowGL allow_gl;

  gl_surface_ = new AwGLSurface;
  bool success =
      shared_renderer_state_->GetCompositor()->
          InitializeHwDraw(gl_surface_);
  DCHECK(success);
}

HardwareRenderer::~HardwareRenderer() {
  GLViewRendererManager* render_manager = GLViewRendererManager::GetInstance();
  render_manager->Remove(renderer_manager_key_);

  ScopedAppGLStateRestore state_restore(
      ScopedAppGLStateRestore::MODE_RESOURCE_MANAGEMENT);
  internal::ScopedAllowGL allow_gl;

  shared_renderer_state_->GetCompositor()->ReleaseHwDraw();
  gl_surface_ = NULL;
}

bool HardwareRenderer::DrawGL(AwDrawGLInfo* draw_info, DrawGLResult* result) {
  TRACE_EVENT0("android_webview", "HardwareRenderer::DrawGL");
  GLViewRendererManager::GetInstance()->DidDrawGL(renderer_manager_key_);

  // We need to watch if the current Android context has changed and enforce
  // a clean-up in the compositor.
  EGLContext current_context = eglGetCurrentContext();
  if (!current_context) {
    DLOG(ERROR) << "DrawGL called without EGLContext";
    return false;
  }

  // TODO(boliu): Handle context loss.
  if (last_egl_context_ != current_context)
    DLOG(WARNING) << "EGLContextChanged";

  ScopedAppGLStateRestore state_restore(ScopedAppGLStateRestore::MODE_DRAW);
  internal::ScopedAllowGL allow_gl;

  if (draw_info->mode != AwDrawGLInfo::kModeDraw)
    return false;

  // Should only need to access SharedRendererState in kModeDraw and kModeSync.
  const DrawGLInput input = shared_renderer_state_->GetDrawGLInput();
  SetCompositorMemoryPolicy();

  gl_surface_->SetBackingFrameBufferObject(
      state_restore.framebuffer_binding_ext());

  gfx::Transform transform;
  transform.matrix().setColMajorf(draw_info->transform);
  transform.Translate(input.scroll_offset.x(), input.scroll_offset.y());
  gfx::Rect clip_rect(draw_info->clip_left,
                      draw_info->clip_top,
                      draw_info->clip_right - draw_info->clip_left,
                      draw_info->clip_bottom - draw_info->clip_top);

  gfx::Rect viewport(draw_info->width, draw_info->height);
  if (!draw_info->is_layer) {
    gfx::RectF view_rect(input.width, input.height);
    transform.TransformRect(&view_rect);
    viewport.Intersect(gfx::ToEnclosingRect(view_rect));
  }

  bool did_draw = shared_renderer_state_->GetCompositor()->
      DemandDrawHw(
          gfx::Size(draw_info->width, draw_info->height),
          transform,
          viewport,
          clip_rect,
          state_restore.stencil_enabled());
  gl_surface_->ResetBackingFrameBufferObject();

  if (did_draw) {
    result->frame_id = input.frame_id;
    result->clip_contains_visible_rect =
        clip_rect.Contains(input.global_visible_rect);
  }
  return did_draw;
}

void HardwareRenderer::SetCompositorMemoryPolicy() {
  if (shared_renderer_state_->IsMemoryPolicyDirty()) {
    content::SynchronousCompositorMemoryPolicy policy =
        shared_renderer_state_->GetMemoryPolicy();
    // Memory policy is set by BrowserViewRenderer on UI thread.
    shared_renderer_state_->GetCompositor()->SetMemoryPolicy(policy);
    shared_renderer_state_->SetMemoryPolicyDirty(false);
  }
}

namespace internal {

base::LazyInstance<base::ThreadLocalBoolean> ScopedAllowGL::allow_gl;

// static
bool ScopedAllowGL::IsAllowed() {
  return allow_gl.Get().Get();
}

ScopedAllowGL::ScopedAllowGL() {
  DCHECK(!allow_gl.Get().Get());
  allow_gl.Get().Set(true);

  if (g_service.Get())
    g_service.Get()->RunTasks();
}

ScopedAllowGL::~ScopedAllowGL() { allow_gl.Get().Set(false); }

DeferredGpuCommandService::DeferredGpuCommandService() {}

DeferredGpuCommandService::~DeferredGpuCommandService() {
  base::AutoLock lock(tasks_lock_);
  DCHECK(tasks_.empty());
}

// static
void DeferredGpuCommandService::RequestProcessGLOnUIThread() {
  SharedRendererState* renderer_state =
      GLViewRendererManager::GetInstance()->GetMostRecentlyDrawn();
  if (!renderer_state) {
    LOG(ERROR) << "No hardware renderer. Deadlock likely";
    return;
  }
  renderer_state->ClientRequestDrawGL();
}

// Called from different threads!
void DeferredGpuCommandService::ScheduleTask(const base::Closure& task) {
  {
    base::AutoLock lock(tasks_lock_);
    tasks_.push(task);
  }
  if (ScopedAllowGL::IsAllowed()) {
    RunTasks();
  } else {
    RequestProcessGLOnUIThread();
  }
}

void DeferredGpuCommandService::ScheduleIdleWork(
    const base::Closure& callback) {
  // TODO(sievers): Should this do anything?
}

bool DeferredGpuCommandService::UseVirtualizedGLContexts() { return true; }

scoped_refptr<gpu::gles2::ShaderTranslatorCache>
DeferredGpuCommandService::shader_translator_cache() {
  if (!shader_translator_cache_.get())
    shader_translator_cache_ = new gpu::gles2::ShaderTranslatorCache;
  return shader_translator_cache_;
}

void DeferredGpuCommandService::RunTasks() {
  bool has_more_tasks;
  {
    base::AutoLock lock(tasks_lock_);
    has_more_tasks = tasks_.size() > 0;
  }

  while (has_more_tasks) {
    base::Closure task;
    {
      base::AutoLock lock(tasks_lock_);
      task = tasks_.front();
      tasks_.pop();
    }
    task.Run();
    {
      base::AutoLock lock(tasks_lock_);
      has_more_tasks = tasks_.size() > 0;
    }
  }
}

void DeferredGpuCommandService::AddRef() const {
  base::RefCountedThreadSafe<DeferredGpuCommandService>::AddRef();
}

void DeferredGpuCommandService::Release() const {
  base::RefCountedThreadSafe<DeferredGpuCommandService>::Release();
}

}  // namespace internal

}  // namespace android_webview
