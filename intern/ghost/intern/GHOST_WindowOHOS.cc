/** \file
 * \ingroup GHOST
 */

#include "GHOST_WindowOHOS.hh"
#include "GHOST_SystemOHOS.hh"

#ifdef WITH_VULKAN_BACKEND
#  include "GHOST_ContextVK.hh"
#endif

#include <cstdio>
#include <cstring>

#ifdef __OHOS__
  #include <hilog/log.h>
  #undef LOG_TAG
  #define LOG_TAG "BlenderGPU"
  #define LOGI(f, ...) OH_LOG_INFO(LOG_APP, f, ##__VA_ARGS__)
  #define LOGE(f, ...) OH_LOG_ERROR(LOG_APP, f, ##__VA_ARGS__)
#else
  #define LOGI(f, ...) ((void)0)
  #define LOGE(f, ...) ((void)0)
#endif

/* Globals populated by Blender_SetNativeWindow() in creator.c.
 * These are the only way we currently get an OHNativeWindow*. */
extern "C" {
extern void *g_ghost_ohos_native_window;
extern uint32_t g_ghost_ohos_win_w;
extern uint32_t g_ghost_ohos_win_h;
}

GHOST_WindowOHOS::GHOST_WindowOHOS(GHOST_SystemOHOS *system,
                                   const char *title,
                                   int32_t left,
                                   int32_t top,
                                   uint32_t width,
                                   uint32_t height,
                                   GHOST_TWindowState state,
                                   GHOST_TDrawingContextType type,
                                   const bool stereoVisual,
                                   const bool is_debug,
                                   const GHOST_GPUDevice &preferred_device)
    : GHOST_Window(width, height, state, stereoVisual, false),
      m_system(system),
      m_nativeWindow(nullptr),
      m_left(left),
      m_top(top),
      m_width(width),
      m_height(height),
      m_title(title ? title : "Blender"),
      m_state(state),
      m_is_debug_context(is_debug),
      m_preferred_device(preferred_device),
      m_valid_setup(false)
{
    LOGI("[GHOST_WindowOHOS] ctor: native_window=%{public}p\n", g_ghost_ohos_native_window);
  /* Pick up the NativeWindow supplied by the NAPI host. */
  m_nativeWindow = reinterpret_cast<OHNativeWindow *>(g_ghost_ohos_native_window);

  if (m_nativeWindow == nullptr) {
    LOGI("[GHOST_WindowOHOS] ERROR: native window is NULL\n");
    fprintf(stderr,
            "GHOST_WindowOHOS: no native window set. "
            "Did you forget to call Blender_SetNativeWindow()?\n");
    return;
  }
  LOGI("[GHOST_WindowOHOS] calling setDrawingContextType(%{public}d)\n", (int)type);

  /* If the host already gave us the real surface size, prefer it. */
  if (g_ghost_ohos_win_w && g_ghost_ohos_win_h) {
    m_width = g_ghost_ohos_win_w;
    m_height = g_ghost_ohos_win_h;
  }

  /* Spin up the drawing context (Vulkan). */
  if (setDrawingContextType(type) == GHOST_kSuccess) {
    LOGI("[GHOST_WindowOHOS] valid_setup = true\n");
    m_valid_setup = true;
  }
  else {
    LOGI("[GHOST_WindowOHOS] ERROR: setDrawingContextType FAILED\n");
    fprintf(stderr, "GHOST_WindowOHOS: failed to create drawing context (type=%d)\n", int(type));
  }
}

GHOST_WindowOHOS::~GHOST_WindowOHOS()
{
  releaseNativeHandles();
  /* The OHNativeWindow* is owned by the ArkTS host; do NOT destroy it here. */
}

GHOST_Context *GHOST_WindowOHOS::newDrawingContext(GHOST_TDrawingContextType type)
{
  LOGI("[GHOST_WindowOHOS] newDrawingContext: type=%{public}d, Vulkan=%{public}d\n",
          (int)type, (int)GHOST_kDrawingContextTypeVulkan);
  switch (type) {
#ifdef WITH_VULKAN_BACKEND
    case GHOST_kDrawingContextTypeVulkan: {
      LOGI("[GHOST_WindowOHOS] creating VK context, native_window=%{public}p\n",
              (void*)m_nativeWindow);
      /* NOTE: GHOST_kVulkanPlatformOHOS must be added to GHOST_ContextVK.hh,
       * and GHOST_ContextVK.cc must handle it by calling vkCreateSurfaceOHOS.
       * See the patch notes at the bottom of this file.
       */
      GHOST_Context *context = new GHOST_ContextVK(m_wantStereoVisual,
                                                   GHOST_kVulkanPlatformOHOS,
                                                   /*window=*/(void *)m_nativeWindow,
                                                   /*display=*/nullptr,
                                                   /*extra1=*/nullptr,
                                                   /*extra2=*/nullptr,
                                                   /*extra3=*/nullptr,
                                                   1,
                                                   2,
                                                   m_is_debug_context,
                                                   m_preferred_device);
      if (context->initializeDrawingContext()) {
        return context;
      }
      delete context;
      return nullptr;
    }
#endif
    default:
      LOGI("[GHOST_WindowOHOS] ERROR: unsupported context type %{public}d\n", (int)type);
      /* OHOS port is Vulkan-only. */
      return nullptr;
  }
}

/* --- Geometry / state --- */

bool GHOST_WindowOHOS::getValid() const
{
  return GHOST_Window::getValid() && m_valid_setup && m_nativeWindow != nullptr;
}

void GHOST_WindowOHOS::setTitle(const char *title)
{
  if (title) {
    m_title = title;
  }
}

std::string GHOST_WindowOHOS::getTitle() const
{
  return m_title;
}

void GHOST_WindowOHOS::getWindowBounds(GHOST_Rect &bounds) const
{
  getClientBounds(bounds);
}

void GHOST_WindowOHOS::getClientBounds(GHOST_Rect &bounds) const
{
  // bounds.m_l = 0;
  // bounds.m_t = 0;
  // bounds.m_r = int32_t(m_width);
  // bounds.m_b = int32_t(m_height);
  bounds.set(0, 0, m_width, m_height);
}

GHOST_TSuccess GHOST_WindowOHOS::setClientWidth(uint32_t width)
{
  m_width = width;
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_WindowOHOS::setClientHeight(uint32_t height)
{
  m_height = height;
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_WindowOHOS::setClientSize(uint32_t width, uint32_t height)
{
  m_width = width;
  m_height = height;
  return GHOST_kSuccess;
}

void GHOST_WindowOHOS::screenToClient(int32_t inX,
                                      int32_t inY,
                                      int32_t &outX,
                                      int32_t &outY) const
{
  /* OHOS XComponent: client == screen from our POV. */
  outX = inX;
  outY = inY;
}

void GHOST_WindowOHOS::clientToScreen(int32_t inX,
                                      int32_t inY,
                                      int32_t &outX,
                                      int32_t &outY) const
{
  outX = inX;
  outY = inY;
}

GHOST_TWindowState GHOST_WindowOHOS::getState() const
{
  /* XComponent always occupies the area laid out by ArkUI. Treat as FullScreen. */
  return GHOST_kWindowStateFullScreen;
}

GHOST_TSuccess GHOST_WindowOHOS::setState(GHOST_TWindowState /*state*/)
{
  /* No-op: window state is controlled by ArkUI layout, not by us. */
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_WindowOHOS::setOrder(GHOST_TWindowOrder /*order*/)
{
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_WindowOHOS::invalidate()
{
  /* Tell the system to redraw us next tick. */
  if (m_system) {
    /* System keeps its own dirty list. */
    /* m_system->addDirtyWindow(this); -- see GHOST_SystemOHOS */
  }
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_WindowOHOS::setProgressBar(float /*progress*/)
{
  return GHOST_kFailure;
}

GHOST_TSuccess GHOST_WindowOHOS::endProgressBar()
{
  return GHOST_kFailure;
}

bool GHOST_WindowOHOS::isDialog() const
{
  return false;
}

uint16_t GHOST_WindowOHOS::getDPIHint()
{
  /* TODO: query OH_NativeDisplayManager for real DPI. */
  return 160;
}

void GHOST_WindowOHOS::notifyResize(uint32_t width, uint32_t height)
{
  
  if (m_width == width && m_height == height) {
    return;  /* No actual change. */
  }
  LOGI("[GHOST_WindowOHOS] notifyResize %{public}ux%{public}u -> %{public}ux%{public}u",
       m_width, m_height, width, height);
  m_width  = width;
  m_height = height;
  // m_resize_pending.store(true, std::memory_order_release);
  // /* Force swapchain recreation on next frame.
  //  * GHOST_ContextVK checks the surface extent in swapBuffers() and will
  //  * rebuild if it doesn't match. But some OHOS drivers don't report
  //  * OUT_OF_DATE proactively, so we explicitly destroy the old swapchain
  //  * to guarantee a fresh one is created with the correct extent. */
  // GHOST_Context *ctx = getContext();
  // if (ctx) {
  //   GHOST_ContextVK *vk_ctx = static_cast<GHOST_ContextVK *>(ctx);
  //   /* destroySwapchain is a no-op if already null.
  //    * Next swapBuffers() call will call createSwapchain() with the
  //    * new surface extent queried from vkGetPhysicalDeviceSurfaceCapabilitiesKHR. */
  //   vk_ctx->destroySwapchain();
  // }
}

bool GHOST_WindowOHOS::consumeResizeFlag()
{
  return m_resize_pending.exchange(false, std::memory_order_acquire);
}

/* --- Cursor stubs --- */

GHOST_TSuccess GHOST_WindowOHOS::setWindowCursorVisibility(bool /*visible*/)
{
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_WindowOHOS::setWindowCursorGrab(GHOST_TGrabCursorMode /*mode*/)
{
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_WindowOHOS::setWindowCursorShape(GHOST_TStandardCursor /*shape*/)
{
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_WindowOHOS::hasCursorShape(GHOST_TStandardCursor /*shape*/)
{
  return GHOST_kFailure;
}

GHOST_TSuccess GHOST_WindowOHOS::setWindowCustomCursorShape(uint8_t * /*bitmap*/,
                                                            uint8_t * /*mask*/,
                                                            int /*sizex*/,
                                                            int /*sizey*/,
                                                            int /*hotX*/,
                                                            int /*hotY*/,
                                                            bool /*canInvertColor*/)
{
  return GHOST_kSuccess;
}