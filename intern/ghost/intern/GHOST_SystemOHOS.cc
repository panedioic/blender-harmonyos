/** \file
 * \ingroup GHOST
 */

#include "GHOST_SystemOHOS.hh"
#include "GHOST_WindowOHOS.hh"

#include "GHOST_Event.hh"
#include "GHOST_EventButton.hh"
#include "GHOST_EventCursor.hh"
#include "GHOST_EventKey.hh"
#include "GHOST_TimerManager.hh"
#include "GHOST_WindowManager.hh"
#include "GHOST_EventKey.hh"

#ifdef WITH_VULKAN_BACKEND
#  include "GHOST_ContextVK.hh"
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

// #ifdef __OHOS__
  #include <hilog/log.h>
  #undef LOG_TAG
  #define LOG_TAG "BlenderGPU"
  #define LOGI(f, ...) OH_LOG_INFO(LOG_APP, f, ##__VA_ARGS__)
  #define LOGE(f, ...) OH_LOG_ERROR(LOG_APP, f, ##__VA_ARGS__)
// #else
//   #define LOGI(f, ...) ((void)0)
//   #define LOGE(f, ...) ((void)0)
// #endif

/* Globals set from the NAPI host (napi_init.cpp -> Blender_SetNativeWindow). */
extern "C" {
extern void *g_ghost_ohos_native_window;
extern uint32_t g_ghost_ohos_win_w;
extern uint32_t g_ghost_ohos_win_h;
}

GHOST_SystemOHOS::GHOST_SystemOHOS() : GHOST_System() {}

GHOST_SystemOHOS::~GHOST_SystemOHOS() {}

GHOST_TSuccess GHOST_SystemOHOS::init()
{
  return GHOST_System::init();
}

/* --- Time --- */

uint64_t GHOST_SystemOHOS::getMilliSeconds() const
{
  using namespace std::chrono;
  return uint64_t(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

/* --- Displays --- */

uint8_t GHOST_SystemOHOS::getNumDisplays() const
{
  return 1;
}

void GHOST_SystemOHOS::getMainDisplayDimensions(uint32_t &width, uint32_t &height) const
{
  width = g_ghost_ohos_win_w ? g_ghost_ohos_win_w : 1080;
  height = g_ghost_ohos_win_h ? g_ghost_ohos_win_h : 1920;
}

void GHOST_SystemOHOS::getAllDisplayDimensions(uint32_t &width, uint32_t &height) const
{
  getMainDisplayDimensions(width, height);
}

/* --- Windows --- */
GHOST_IWindow *GHOST_SystemOHOS::createWindow(const char *title, 
                                              int32_t left, int32_t top, 
                                              uint32_t width, uint32_t height, 
                                              GHOST_TWindowState state, 
                                              GHOST_GPUSettings gpuSettings, 
                                              const bool /*exclusive*/, 
                                              const bool /*is_dialog*/, 
                                              const GHOST_IWindow * /*parentWindow*/) 

{ 
  LOGI("[GHOST_OHOS] createWindow enter (title=%{public}s w=%{public}u h=%{public}u)", 
       title ? title : "(null)", width, height); 

  gpuSettings.context_type = GHOST_kDrawingContextTypeVulkan; 
  int win_id = -1; 
  void *nw   = nullptr; 
  uint32_t win_w = width, win_h = height; 
  const bool is_main = m_records.empty(); 

  LOGI("[GHOST_OHOS] createWindow: m_records.size=%{public}zu is_main=%{public}d",
     m_records.size(), (int)is_main);
  if (is_main) { 
    /* 主窗口：沿用启动时宿主填的全局 native window */ 
    if (!g_ghost_ohos_native_window) { 
      LOGE("[GHOST_OHOS] main window: g_ghost_ohos_native_window == NULL"); 
      return nullptr; 
    } 

    win_id = kMainWindowId; 
    nw     = g_ghost_ohos_native_window; 
    win_w  = g_ghost_ohos_win_w ? g_ghost_ohos_win_w : width; 
    win_h  = g_ghost_ohos_win_h ? g_ghost_ohos_win_h : height; 

    /* 直接登记为 ready */ 
    std::lock_guard<std::mutex> lk(m_reg_mutex); 
    WindowRecord rec; 
    rec.id = win_id; 
    rec.native_window = nw; 
    rec.w = win_w; 
    rec.h = win_h; 
    rec.surface_ready = true; 
    m_records[win_id] = rec; 
  } 
  else { 
    /* 子窗口：请宿主创建，然后阻塞等待 surface */ 
    if (!m_request_create) { 
      LOGE("[GHOST_OHOS] SUB-WINDOW REQUESTED BUT m_request_create==nullptr "
         "(did ArkTS call registerSubWindowCallback?)");
      return nullptr; 
    } 
    
    win_id = allocateSubWindowId(width, height); 
    LOGI("[GHOST_OHOS] calling host_request_create(id=%{public}d w=%{public}u h=%{public}u title=%{public}s)",
        win_id, width, height, title ? title : "(null)");
    m_request_create(win_id, int(width), int(height), title ? title : "Blender"); 
    LOGI("[GHOST_OHOS] waitForSurface(id=%{public}d, 5000ms)...", win_id);
    WindowRecord *rec = waitForSurface(win_id, /*timeout_ms=*/5000); 

    if (!rec) { 
      LOGE("[GHOST_OHOS] waitForSurface TIMEOUT id=%{public}d", win_id);
      if (m_request_destroy) m_request_destroy(win_id); 
      std::lock_guard<std::mutex> lk(m_reg_mutex); 
      m_records.erase(win_id); 
      return nullptr; 
    } 

    nw    = rec->native_window; 
    win_w = rec->w; 
    win_h = rec->h; 
    LOGI("[GHOST_OHOS] waitForSurface OK id=%{public}d nw=%{public}p",
       win_id, rec->native_window);
    LOGI("[GHOST_OHOS] sub-window %{public}d ready nw=%{public}p %{public}ux%{public}u", 
         win_id, nw, win_w, win_h); 
  } 

  GHOST_WindowOHOS *window = new GHOST_WindowOHOS( 
      this, title, left, top, win_w, win_h, state, 
      gpuSettings.context_type, 
      (gpuSettings.flags & GHOST_gpuStereoVisual) != 0, 
      (gpuSettings.flags & GHOST_gpuDebugContext) != 0, 
      gpuSettings.preferred_device, 
      /*native_window=*/nw, 
      /*window_id=*/win_id); 

  if (!window->getValid()) { 
    LOGE("[GHOST_OHOS] window %{public}d not valid, cleaning up", win_id); 
    delete window; 
    if (!is_main && m_request_destroy) m_request_destroy(win_id); 
    std::lock_guard<std::mutex> lk(m_reg_mutex); 
    m_records.erase(win_id); 
    return nullptr; 
  } 

  { 
    std::lock_guard<std::mutex> lk(m_reg_mutex); 
    m_records[win_id].ghost_window = window; 
  } 

  m_windowManager->addWindow(window); 
  m_windowManager->setActiveWindow(window); 
  pushEvent(new GHOST_Event(getMilliSeconds(), GHOST_kEventWindowSize, window)); 

  return window; 
}

GHOST_IContext *GHOST_SystemOHOS::createOffscreenContext(GHOST_GPUSettings gpuSettings)
{
  const bool debug_context = (gpuSettings.flags & GHOST_gpuDebugContext) != 0;

  switch (gpuSettings.context_type) {
#ifdef WITH_VULKAN_BACKEND
    case GHOST_kDrawingContextTypeVulkan: {
      GHOST_Context *context = new GHOST_ContextVK(false,
                                                   GHOST_kVulkanPlatformOHOS,
                                                   /*window=*/nullptr,
                                                   /*display=*/nullptr,
                                                   nullptr,
                                                   nullptr,
                                                   nullptr,
                                                   1,
                                                   2,
                                                   debug_context,
                                                   gpuSettings.preferred_device);
      if (context->initializeDrawingContext()) {
        return context;
      }
      delete context;
      return nullptr;
    }
#endif
    default:
      return nullptr;
  }
}

GHOST_TSuccess GHOST_SystemOHOS::disposeContext(GHOST_IContext *context)
{
  delete context;
  return GHOST_kSuccess;
}

/* --- Event pump ---
 *
 * On OHOS we don't own the input event source (ArkUI/XComponent owns it).
 * Input events are expected to be pushed into the GHOST queue from the
 * NAPI/XComponent callback thread. Here we only:
 *   1) fire timers,
 *   2) flush dirty-window expose events.
 */

/* ─────────────────────────────────────────────────────────────
 *  Input queue — UI-thread producers
 * ───────────────────────────────────────────────────────────── */
 
/* ─────────────────────────────────────────────────────────────
 *  Input queue — render-thread consumer
 * ───────────────────────────────────────────────────────────── */
GHOST_IWindow *GHOST_SystemOHOS::getPrimaryWindow() const
{
  if (!m_windowManager) return nullptr;
  GHOST_IWindow *w = m_windowManager->getActiveWindow();
  if (w) return w;
  const std::vector<GHOST_IWindow *> &all = m_windowManager->getWindows();
  return all.empty() ? nullptr : all.front();
}

bool GHOST_SystemOHOS::drainInputQueue() 
{ 
  std::deque<QueuedInput> local; 
  { 
    std::lock_guard<std::mutex> lk(m_input_mutex); 
    if (m_input_queue.empty()) return false; 
    local.swap(m_input_queue); 
  } 
  bool any = false; 
  for (const QueuedInput &ev : local) { 
    GHOST_WindowOHOS *win = findGhostWindow(ev.window_id); 
    if (!win) win = static_cast<GHOST_WindowOHOS *>(getPrimaryWindow()); 
    if (!win) continue; 
    switch (ev.kind) { 
      case QueuedInput::CURSOR_MOVE: 
        pushEvent(new GHOST_EventCursor(ev.time_ms, GHOST_kEventCursorMove, win, 
                                        ev.x, ev.y, GHOST_TABLET_DATA_NONE)); 
        any = true; 
        break; 
      case QueuedInput::BUTTON_DOWN: 
        pushEvent(new GHOST_EventButton(ev.time_ms, GHOST_kEventButtonDown, win, 
                                        ev.button, GHOST_TABLET_DATA_NONE)); 
        any = true; 
        break; 
      case QueuedInput::BUTTON_UP: 
        pushEvent(new GHOST_EventButton(ev.time_ms, GHOST_kEventButtonUp, win, 
                                        ev.button, GHOST_TABLET_DATA_NONE)); 
        any = true; 
        break; 
      case QueuedInput::KEY_DOWN: { 
        updateModifierFromKey(true, ev.key); 
        char utf8_buf[8] = {0}; 
        if (ev.utf8_char) utf8_buf[0] = ev.utf8_char; 
        pushEvent(new GHOST_EventKey(ev.time_ms, GHOST_kEventKeyDown, win, ev.key, false, utf8_buf)); 
        any = true; 
        break; 
      } 
      case QueuedInput::KEY_UP: { 
        updateModifierFromKey(false, ev.key); 
        char utf8_buf[8] = {0}; 
        pushEvent(new GHOST_EventKey(ev.time_ms, GHOST_kEventKeyUp, win, ev.key, false, utf8_buf)); 
        any = true; 
        break; 
      } 
      case QueuedInput::RESIZE: { 
        win->notifyResize(ev.rw, ev.rh); 
        GHOST_Context *ctx = win->getContext(); 
        if (ctx) static_cast<GHOST_ContextVK *>(ctx)->markSwapchainDirty(); 
        pushEvent(new GHOST_Event(ev.time_ms, GHOST_kEventWindowSize, win)); 
        any = true; 
        break; 
      } 
    } 
  } 
  return any; 
}

bool GHOST_SystemOHOS::processEvents(bool waitForEvent)
{
  bool any = false;

  /* 0) Drain pending surface resize from the UI thread. */
  if (m_has_pending_resize.exchange(false, std::memory_order_acquire)) {
    const uint32_t w = m_pending_resize_w.load(std::memory_order_relaxed);
    const uint32_t h = m_pending_resize_h.load(std::memory_order_relaxed);
    LOGI("[GHOST_OHOS] drain resize %{public}ux%{public}u", w, h);
    notifySurfaceResized(w, h);   /* already pushes GHOST_kEventWindowSize */
    any = true;
  }

  /* 1) Drain whatever the UI thread has posted since last pump. */
  if (drainInputQueue()) any = true;

  GHOST_TimerManager *timerMgr = getTimerManager();

  /* 2) Block briefly only if we truly have nothing to do. */
  if (waitForEvent && !any && m_dirty_windows.empty()) {
    uint64_t next = timerMgr->nextFireTime();
    uint64_t now = getMilliSeconds();
    uint64_t sleepMs = 16;
    if (next != GHOST_kFireTimeNever && next > now) {
      sleepMs = std::min<uint64_t>(next - now, 32);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    if (drainInputQueue()) any = true;
  }

  /* 3) Timers + expose. */
  if (timerMgr->fireTimers(getMilliSeconds())) any = true;
  if (generateWindowExposeEvents()) any = true;
  return any;
}

bool GHOST_SystemOHOS::generateWindowExposeEvents()
{
  bool any = false;
  for (GHOST_WindowOHOS *w : m_dirty_windows) {
    pushEvent(new GHOST_Event(getMilliSeconds(), GHOST_kEventWindowUpdate, w));
    any = true;
  }
  m_dirty_windows.clear();
  return any;
}

void GHOST_SystemOHOS::addDirtyWindow(GHOST_WindowOHOS *window)
{
  if (window) {
    m_dirty_windows.push_back(window);
  }
}

void GHOST_SystemOHOS::notifySurfaceResized(uint32_t width, uint32_t height)
{
  g_ghost_ohos_win_w = width;
  g_ghost_ohos_win_h = height;

  if (!m_windowManager) {
    return;
  }
  for (GHOST_IWindow *iw : m_windowManager->getWindows()) {
    GHOST_WindowOHOS *w = static_cast<GHOST_WindowOHOS *>(iw);
    w->notifyResize(width, height);
    
    /* 标记 context 需要重建 swapchain */
    GHOST_Context *ctx = w->getContext();
    if (ctx) {
      static_cast<GHOST_ContextVK *>(ctx)->markSwapchainDirty();
    }
    
    pushEvent(new GHOST_Event(getMilliSeconds(), GHOST_kEventWindowSize, w));
  }
}


/* ─────────────────────────────────────────────────────────────
 *  Input state queries — now backed by atomics
 * ───────────────────────────────────────────────────────────── */
GHOST_TSuccess GHOST_SystemOHOS::getCursorPosition(int32_t &x, int32_t &y) const
{
  x = m_cursor_x.load(std::memory_order_relaxed);
  y = m_cursor_y.load(std::memory_order_relaxed);
  return GHOST_kSuccess;
}
GHOST_TSuccess GHOST_SystemOHOS::setCursorPosition(int32_t, int32_t)
{
  /* OHOS has no way to warp the cursor; CapabilityCursorWarp is already disabled. */
  return GHOST_kSuccess;
}
GHOST_TSuccess GHOST_SystemOHOS::getPixelAtCursor(float r_color[3]) const
{
  r_color[0] = r_color[1] = r_color[2] = 0.0f;
  return GHOST_kFailure;
}
GHOST_TSuccess GHOST_SystemOHOS::getModifierKeys(GHOST_ModifierKeys &keys) const
{
  // keys = GHOST_ModifierKeys();  /* No keyboard yet. */
  keys = m_modifierKeys;
  return GHOST_kSuccess;
}
GHOST_TSuccess GHOST_SystemOHOS::getButtons(GHOST_Buttons &buttons) const
{
  const uint32_t m = m_button_mask.load(std::memory_order_relaxed);
  buttons = GHOST_Buttons();
  buttons.set(GHOST_kButtonMaskLeft,   (m & (1u << GHOST_kButtonMaskLeft))   != 0);
  buttons.set(GHOST_kButtonMaskMiddle, (m & (1u << GHOST_kButtonMaskMiddle)) != 0);
  buttons.set(GHOST_kButtonMaskRight,  (m & (1u << GHOST_kButtonMaskRight))  != 0);
  return GHOST_kSuccess;
}
GHOST_TCapabilityFlag GHOST_SystemOHOS::getCapabilities() const
{
  return GHOST_TCapabilityFlag(GHOST_CAPABILITY_FLAG_ALL &
                               ~(GHOST_kCapabilityClipboardImages |
                                 GHOST_kCapabilityInputIME |
                                 GHOST_kCapabilityWindowDecorationStyles |
                                 GHOST_kCapabilityCursorWarp |
                                 GHOST_kCapabilityGPUReadFrontBuffer |
                                 GHOST_kCapabilityDesktopSample));
}

/* --- Clipboard stubs (all stubs for now) --- */

char *GHOST_SystemOHOS::getClipboard(bool /*selection*/) const
{
  return nullptr;
}

void GHOST_SystemOHOS::putClipboard(const char * /*buffer*/, bool /*selection*/) const {}

/* --- Dialog stub --- */

GHOST_TSuccess GHOST_SystemOHOS::showMessageBox(const char *title,
                                                const char *message,
                                                const char * /*help_label*/,
                                                const char * /*continue_label*/,
                                                const char * /*link*/,
                                                GHOST_DialogOptions /*dialog_options*/) const
{
  fprintf(stderr, "[GHOST_OHOS MessageBox] %s: %s\n", title ? title : "", message ? message : "");
  return GHOST_kSuccess;
}

/* ─────────────────────────────────────────────────────────────
 *  C bridge exported from libblender.so
 *  The host (napi_init.cpp) calls these from the XComponent
 *  callback thread. Keep this ABI stable — the host uses dlsym.
 *
 *  kind:
 *     0 = mouse move
 *     1 = mouse button down     (button: 0=left 1=middle 2=right)
 *     2 = mouse button up
 *     10 = touch down
 *     11 = touch move
 *     12 = touch up
 *     13 = touch cancel
 *  For touch events, 'button' carries the touch id.
 * ───────────────────────────────────────────────────────────── */
#include "GHOST_ISystem.hh"
static inline GHOST_SystemOHOS *ohos_system()
{
  GHOST_ISystem *s = GHOST_ISystem::getSystem();
  return s ? static_cast<GHOST_SystemOHOS *>(s) : nullptr;
}
extern "C" __attribute__((visibility("default")))
void Blender_OnPointerEvent(int kind, int button, float fx, float fy)
{
    // LOGI("[GHOST_SystemOHOS.cc] kind=%{public}d button=%{public}d xy=(%{public}.1f,%{public}.1f)", kind, button, fx, fy);
  GHOST_SystemOHOS *sys = ohos_system();
  if (!sys) return;
  const int32_t x = (int32_t)fx;
  const int32_t y = (int32_t)fy;
  auto mouse_btn = [](int b) {
    switch (b) {
      case 0:  return GHOST_kButtonMaskLeft;
      case 1:  return GHOST_kButtonMaskMiddle;
      case 2:  return GHOST_kButtonMaskRight;
      default: return GHOST_kButtonMaskLeft;
    }
  };
  switch (kind) {
    case 0:  sys->postCursorMove(x, y); break;
    case 1:  sys->postButtonEvent(true,  mouse_btn(button), x, y); break;
    case 2:  sys->postButtonEvent(false, mouse_btn(button), x, y); break;
    case 10: sys->postTouchEvent(GHOST_SystemOHOS::TOUCH_DOWN,   button, x, y); break;
    case 11: sys->postTouchEvent(GHOST_SystemOHOS::TOUCH_MOVE,   button, x, y); break;
    case 12: sys->postTouchEvent(GHOST_SystemOHOS::TOUCH_UP,     button, x, y); break;
    case 13: sys->postTouchEvent(GHOST_SystemOHOS::TOUCH_CANCEL, button, x, y); break;
    default: break;
  }
}

bool GHOST_SystemOHOS::getSystemDir(int /*version*/, char *dir, int dirlen) const
{
    const char *ss = getenv("BLENDER_SYSTEM_SCRIPTS");
    if (ss && ss[0]) {
        strncpy(dir, ss, dirlen - 1);
        dir[dirlen - 1] = '\0';
    LOGI("[debug] %{public}s:%{public}d [%{public}s]", __FILE__, __LINE__, dir);
        return true;
    }
    return false;
}

const char *GHOST_SystemOHOS::getSystemDir(int /*version*/, const char * /*versionstr*/) const
{
    LOGI("[debug] %{public}s:%{public}d", __FILE__, __LINE__);
    // 先写死
    static std::string s_dir;
    const char *ss = "/data/storage/el2/base/haps/entry/files/blender";
    if (ss && ss[0]) {
        s_dir = ss;
        return s_dir.c_str();
    }
    return nullptr;
}

const char *GHOST_SystemOHOS::getUserDir(int /*version*/, const char * /*versionstr*/) const
{
    LOGI("[debug] %{public}s:%{public}d", __FILE__, __LINE__);
    // 先写死
    static std::string s_dir;
    const char *ss = "/data/storage/el2/base/haps/entry/files/blender";
    if (ss && ss[0]) {
        s_dir = ss;
        return s_dir.c_str();
    }
    return nullptr;
}

const char *GHOST_SystemOHOS::getBinaryDir() const
{
    return nullptr;
}

extern "C" __attribute__((visibility("default")))
void Blender_OnSurfaceResize(uint32_t w, uint32_t h)
{
  GHOST_SystemOHOS *sys = ohos_system();
  if (!sys) return;
  sys->postSurfaceResized(w, h);
}

/* OHOS KeyCode → GHOST_TKey 映射表 */
static GHOST_TKey ohos_keycode_to_ghost(int keyCode)
{
  /* HarmonyOS KeyCode, 实测验证:
   * A=2017, Space=2050, LShift=2047, LCtrl=2072,
   * Enter=2054, Esc=2070, Tab=2049, Delete=2071 */

  /* 字母 A-Z = 2017-2042 */
  if (keyCode >= 2017 && keyCode <= 2042) {
    return (GHOST_TKey)(GHOST_kKeyA + (keyCode - 2017));
  }
  /* 数字 0-9 = 2000-2009 */
  if (keyCode >= 2000 && keyCode <= 2009) {
    return (GHOST_TKey)(GHOST_kKey0 + (keyCode - 2000));
  }

  switch (keyCode) {
    /* 方向键 */
    case 2012: return GHOST_kKeyUpArrow;     /* KEYCODE_DPAD_UP */
    case 2013: return GHOST_kKeyDownArrow;   /* KEYCODE_DPAD_DOWN */
    case 2014: return GHOST_kKeyLeftArrow;   /* KEYCODE_DPAD_LEFT */
    case 2015: return GHOST_kKeyRightArrow;  /* KEYCODE_DPAD_RIGHT */

    /* 标点 */
    case 2043: return GHOST_kKeyComma;       /* , */
    case 2044: return GHOST_kKeyPeriod;      /* . */
    case 2056: return GHOST_kKeyAccentGrave; /* ` */
    case 2057: return GHOST_kKeyMinus;       /* - */
    case 2058: return GHOST_kKeyEqual;       /* = */
    case 2059: return GHOST_kKeyLeftBracket; /* [ */
    case 2060: return GHOST_kKeyRightBracket;/* ] */
    case 2061: return GHOST_kKeyBackslash;   /* \ */
    case 2062: return GHOST_kKeySemicolon;   /* ; */
    case 2063: return GHOST_kKeyQuote;       /* ' */
    case 2064: return GHOST_kKeySlash;       /* / */

    /* 修饰键 */
    case 2045: return GHOST_kKeyLeftAlt;     /* KEYCODE_ALT_LEFT */
    case 2046: return GHOST_kKeyRightAlt;    /* KEYCODE_ALT_RIGHT */
    case 2047: return GHOST_kKeyLeftShift;   /* KEYCODE_SHIFT_LEFT ✓实测 */
    case 2048: return GHOST_kKeyRightShift;  /* KEYCODE_SHIFT_RIGHT */
    case 2072: return GHOST_kKeyLeftControl; /* KEYCODE_CTRL_LEFT ✓实测 */
    case 2073: return GHOST_kKeyRightControl;/* KEYCODE_CTRL_RIGHT */
    case 2076: return GHOST_kKeyLeftOS;      /* KEYCODE_META_LEFT */
    case 2077: return GHOST_kKeyRightOS;     /* KEYCODE_META_RIGHT */
    case 2074: return GHOST_kKeyCapsLock;    /* KEYCODE_CAPS_LOCK */

    /* 控制键 */
    case 2049: return GHOST_kKeyTab;         /* ✓实测 */
    case 2050: return GHOST_kKeySpace;       /* ✓实测 */
    case 2054: return GHOST_kKeyEnter;       /* ✓实测 */
    case 2055: return GHOST_kKeyBackSpace;   /* KEYCODE_DEL (Backspace) */
    case 2070: return GHOST_kKeyEsc;         /* ✓实测 */
    case 2071: return GHOST_kKeyDelete;      /* ✓实测 (Forward Delete) */

    /* 导航键 */
    case 2081: return GHOST_kKeyHome;
    case 2082: return GHOST_kKeyEnd;
    case 2083: return GHOST_kKeyInsert;
    case 2085: return GHOST_kKeyUpPage;      /* PAGE_UP */
    case 2086: return GHOST_kKeyDownPage;    /* PAGE_DOWN */

    /* 功能键 F1-F12 */
    case 2108: return GHOST_kKeyF1;
    case 2109: return GHOST_kKeyF2;
    case 2110: return GHOST_kKeyF3;
    case 2111: return GHOST_kKeyF4;
    case 2112: return GHOST_kKeyF5;
    case 2113: return GHOST_kKeyF6;
    case 2114: return GHOST_kKeyF7;
    case 2115: return GHOST_kKeyF8;
    case 2116: return GHOST_kKeyF9;
    case 2117: return GHOST_kKeyF10;
    case 2118: return GHOST_kKeyF11;
    case 2119: return GHOST_kKeyF12;

    default: return GHOST_kKeyUnknown;
  }
}

/* 更新 modifier 状态 */
void GHOST_SystemOHOS::updateModifierFromKey(bool pressed, GHOST_TKey key)
{
  /* 修复：这里必须是 GHOST_TModifierKey */
  GHOST_TModifierKey mod;
  switch (key) {
    case GHOST_kKeyLeftShift:
    case GHOST_kKeyRightShift:   mod = GHOST_kModifierKeyLeftShift; break;
    case GHOST_kKeyLeftControl:
    case GHOST_kKeyRightControl: mod = GHOST_kModifierKeyLeftControl; break;
    case GHOST_kKeyLeftAlt:
    case GHOST_kKeyRightAlt:     mod = GHOST_kModifierKeyLeftAlt; break;
    default: return;
  }
  m_modifierKeys.set(mod, pressed);
}

extern "C" __attribute__((visibility("default")))
void Blender_OnKeyEvent(int action, int keyCode, int metaState)
{
  (void)metaState;  /* TODO: 后续可用来同步 modifier 状态 */

  GHOST_SystemOHOS *sys = ohos_system();
  if (!sys) return;

  /* action: 1=down, 0=up (与 OHOS KeyAction 一致) */
  bool pressed = (action == 1);
  GHOST_TKey ghost_key = ohos_keycode_to_ghost(keyCode);

  if (ghost_key == GHOST_kKeyUnknown) {
    LOGI("[Key] unmapped keyCode=%{public}d action=%{public}d", keyCode, action);
    return;
  }

  LOGI("[Key] action=%{public}d keyCode=%{public}d -> ghost_key=%{public}d",
       action, keyCode, (int)ghost_key);

  /* 对于可打印字符，传 ascii 值给 Blender（用于文本输入） */
  char ch = '\0';
  if (ghost_key >= GHOST_kKeyA && ghost_key <= GHOST_kKeyZ) {
    ch = 'a' + (ghost_key - GHOST_kKeyA);
  } else if (ghost_key >= GHOST_kKey0 && ghost_key <= GHOST_kKey9) {
    ch = '0' + (ghost_key - GHOST_kKey0);
  } else if (ghost_key == GHOST_kKeySpace) {
    ch = ' ';
  }

  sys->postKeyEvent(pressed, ghost_key, ch);
}

// 新增子窗口注册/查询/等待方法:

int GHOST_SystemOHOS::allocateSubWindowId(uint32_t w, uint32_t h) 
{ 
  std::lock_guard<std::mutex> lk(m_reg_mutex); 
  int id = m_next_window_id++; 
  WindowRecord rec; 
  rec.id = id; 
  rec.w  = w; 
  rec.h  = h; 
  rec.surface_ready = false; 
  m_records[id] = rec; 
  return id; 
} 

GHOST_SystemOHOS::WindowRecord* GHOST_SystemOHOS::waitForSurface(int id, int timeout_ms) 
{ 
  std::unique_lock<std::mutex> lk(m_reg_mutex); 
  const bool ok = m_reg_cv.wait_for( 
      lk, std::chrono::milliseconds(timeout_ms), 
      [&]() { 
        auto it = m_records.find(id); 
        return it != m_records.end() && it->second.surface_ready; 
      }); 
  if (!ok) return nullptr; 
  auto it = m_records.find(id); 
  return (it == m_records.end()) ? nullptr : &it->second; 
} 

void GHOST_SystemOHOS::onSubWindowSurfaceReady(int id, void *nw, uint32_t w, uint32_t h) 
{ 
  LOGI("[GHOST_OHOS] onSubWindowSurfaceReady id=%{public}d nw=%{public}p %{public}ux%{public}u", 
       id, nw, w, h); 
  { 
    std::lock_guard<std::mutex> lk(m_reg_mutex); 
    auto it = m_records.find(id); 
    if (it == m_records.end()) { 
      /* 主窗口情况：宿主可能在启动阶段直接回传主 surface —— 建一条记录以便反查 */ 
      WindowRecord rec; 
      rec.id = id; 
      rec.native_window = nw; 
      rec.w = w; 
      rec.h = h; 
      rec.surface_ready = true; 
      m_records[id] = rec; 
    } else { 
      it->second.native_window = nw; 
      it->second.w = w; 
      it->second.h = h; 
      it->second.surface_ready = true; 
    } 
  } 
  m_reg_cv.notify_all(); 
} 

void GHOST_SystemOHOS::onSubWindowClosed(int id) 
{ 
  LOGI("[GHOST_OHOS] onSubWindowClosed id=%{public}d", id); 
  GHOST_WindowOHOS *w = nullptr; 

  { 
    std::lock_guard<std::mutex> lk(m_reg_mutex); 
    auto it = m_records.find(id); 
    if (it != m_records.end()) w = it->second.ghost_window; 
  } 

  if (w) { 
    /* 走 Blender 常规的 WindowClose 事件；WM 会调用 disposeWindow */ 
    pushEvent(new GHOST_Event(getMilliSeconds(), GHOST_kEventWindowClose, w)); 
  } else { 
    /* 还没绑定 ghost_window（超时被抛弃或竞态），直接删记录 */ 
    std::lock_guard<std::mutex> lk(m_reg_mutex); 
    m_records.erase(id); 
  } 
} 

int GHOST_SystemOHOS::findWindowIdByNativeWindow(void *nw) const
{ 
  std::lock_guard<std::mutex> lk(m_reg_mutex); 
  for (const auto &kv : m_records) { 
    if (kv.second.native_window == nw) return kv.first; 
  } 
  return -1; 
}

// 改造事件入口 — 全部带 window_id 版

void GHOST_SystemOHOS::postCursorMoveForId(int window_id, int32_t x, int32_t y) 
{ 
  m_cursor_x.store(x, std::memory_order_relaxed); 
  m_cursor_y.store(y, std::memory_order_relaxed); 
  QueuedInput q{}; 
  q.kind = QueuedInput::CURSOR_MOVE; 
  q.window_id = window_id; 
  q.x = x; q.y = y; 
  q.time_ms = getMilliSeconds(); 
  std::lock_guard<std::mutex> lk(m_input_mutex); 
  if (!m_input_queue.empty() 
      && m_input_queue.back().kind == QueuedInput::CURSOR_MOVE
      && m_input_queue.back().window_id == window_id) { 
    m_input_queue.back() = q; 
  } else { 
    m_input_queue.push_back(q); 
  } 
} 

void GHOST_SystemOHOS::postButtonEventForId(int window_id, bool pressed, 
                                            GHOST_TButton button, int32_t x, int32_t y) 
{ 
  m_cursor_x.store(x, std::memory_order_relaxed); 
  m_cursor_y.store(y, std::memory_order_relaxed); 
  const uint32_t bit = (1u << uint32_t(button)); 
  if (pressed) m_button_mask.fetch_or(bit, std::memory_order_relaxed); 
  else         m_button_mask.fetch_and(~bit, std::memory_order_relaxed); 
  const uint64_t t = getMilliSeconds(); 
  QueuedInput mv{}, bt{}; 
  mv.kind = QueuedInput::CURSOR_MOVE; mv.window_id = window_id; 
  mv.x = x; mv.y = y; mv.time_ms = t; 
  bt.kind = pressed ? QueuedInput::BUTTON_DOWN : QueuedInput::BUTTON_UP; 
  bt.window_id = window_id; 
  bt.button = button; bt.x = x; bt.y = y; bt.time_ms = t; 
  std::lock_guard<std::mutex> lk(m_input_mutex); 
  m_input_queue.push_back(mv); 
  m_input_queue.push_back(bt); 
} 

void GHOST_SystemOHOS::postTouchEventForId(int window_id, TouchPhase phase, 
                                           int32_t touch_id, int32_t x, int32_t y) 
{ 
  switch (phase) { 
    case TOUCH_DOWN: { 
      int32_t expected = -1; 
      if (m_primary_touch_id.compare_exchange_strong(expected, touch_id)) 
        postButtonEventForId(window_id, true, GHOST_kButtonMaskLeft, x, y); 
      break; 
    } 
    case TOUCH_MOVE: 
      if (m_primary_touch_id.load() == touch_id) 
        postCursorMoveForId(window_id, x, y); 
      break; 
    case TOUCH_UP: 
    case TOUCH_CANCEL: { 
      int32_t expected = touch_id; 
      if (m_primary_touch_id.compare_exchange_strong(expected, -1)) 
        postButtonEventForId(window_id, false, GHOST_kButtonMaskLeft, x, y); 
      break; 
    } 
  } 
} 

void GHOST_SystemOHOS::postKeyEventForId(int window_id, bool pressed, 
                                         GHOST_TKey key, char utf8_char) 
{
  QueuedInput q{}; 
  q.kind = pressed ? QueuedInput::KEY_DOWN : QueuedInput::KEY_UP; 
  q.window_id = window_id; 
  q.key = key; 
  q.utf8_char = utf8_char; 
  q.time_ms = getMilliSeconds(); 
  std::lock_guard<std::mutex> lk(m_input_mutex); 
  m_input_queue.push_back(q); 
} 

void GHOST_SystemOHOS::postSurfaceResizedForId(int window_id, uint32_t w, uint32_t h) 
{ 
  QueuedInput q{}; 
  q.kind = QueuedInput::RESIZE; 
  q.window_id = window_id; 
  q.rw = w; q.rh = h; 
  q.time_ms = getMilliSeconds(); 
  std::lock_guard<std::mutex> lk(m_input_mutex); 
  m_input_queue.push_back(q); 
}

// 保留旧无 id 版作为兼容 shim(内部调 id=0):
void GHOST_SystemOHOS::postCursorMove(int32_t x, int32_t y) { 
  postCursorMoveForId(kMainWindowId, x, y); 
} 
void GHOST_SystemOHOS::postButtonEvent(bool p, GHOST_TButton b, int32_t x, int32_t y) { 
  postButtonEventForId(kMainWindowId, p, b, x, y); 
} 
void GHOST_SystemOHOS::postTouchEvent(TouchPhase ph, int32_t id, int32_t x, int32_t y) { 
  postTouchEventForId(kMainWindowId, ph, id, x, y); 
} 
void GHOST_SystemOHOS::postKeyEvent(bool p, GHOST_TKey k, char c) { 
  postKeyEventForId(kMainWindowId, p, k, c); 
} 
void GHOST_SystemOHOS::postSurfaceResized(uint32_t w, uint32_t h) { 
  postSurfaceResizedForId(kMainWindowId, w, h); 
}

// 改造 drainInputQueue,按 window_id 查目标 window:
// static GHOST_WindowOHOS *find_window_by_id_locked( GHOST_SystemOHOS *sys, int id) 
// { 
//   /* 通过 records 反查 */ 
//   std::lock_guard<std::mutex> lk(sys->m_reg_mutex);   /* 需要把 m_reg_mutex/m_records 放 public 或加 friend/getter */ 
//   auto it = sys->m_records.find(id); 
//   return (it == sys->m_records.end()) ? nullptr : it->second.ghost_window; 
// }

GHOST_WindowOHOS *GHOST_SystemOHOS::findGhostWindow(int id) const
{ 
  std::lock_guard<std::mutex> lk(m_reg_mutex); 
  auto it = m_records.find(id); 
  return (it == m_records.end()) ? nullptr : it->second.ghost_window; 
}

void GHOST_SystemOHOS::onGhostWindowDestroyed(int id)
{
  {
    std::lock_guard<std::mutex> lk(m_reg_mutex);
    m_records.erase(id);
  }
  if (m_request_destroy) m_request_destroy(id);
}

// 新增导出 C API:
extern "C" __attribute__((visibility("default"))) 
void Blender_SetSubWindowHooks(void (*on_create)(int, int, int, const char *), 
                               void (*on_destroy)(int)) 
{ 
  auto *sys = ohos_system(); 
  if (sys) sys->setSubWindowHooks(on_create, on_destroy); 
} 

extern "C" __attribute__((visibility("default"))) 
void Blender_OnSubWindowSurfaceReady(int id, void *nw, uint32_t w, uint32_t h) 
{ 
  auto *sys = ohos_system(); 
  if (sys) sys->onSubWindowSurfaceReady(id, nw, w, h); 
} 

extern "C" __attribute__((visibility("default"))) 
void Blender_OnSubWindowClosed(int id) 
{ 
  auto *sys = ohos_system(); 
  if (sys) sys->onSubWindowClosed(id); 
} 

/* 带 window_id 的事件入口 */ 
extern "C" __attribute__((visibility("default"))) 
void Blender_OnPointerEventW(int window_id, int kind, int button, float fx, float fy) 
{ 
  auto *sys = ohos_system(); 
  if (!sys) return; 
  int32_t x = int32_t(fx), y = int32_t(fy); 
  auto mouse_btn = [](int b) { 
    switch (b) { case 1: return GHOST_kButtonMaskMiddle; 
                 case 2: return GHOST_kButtonMaskRight; 
                 default: return GHOST_kButtonMaskLeft; } 
  }; 

  switch (kind) { 
    case 0:  sys->postCursorMoveForId(window_id, x, y); break; 
    case 1:  sys->postButtonEventForId(window_id, true,  mouse_btn(button), x, y); break; 
    case 2:  sys->postButtonEventForId(window_id, false, mouse_btn(button), x, y); break; 
    case 10: sys->postTouchEventForId(window_id, GHOST_SystemOHOS::TOUCH_DOWN,   button, x, y); break; 
    case 11: sys->postTouchEventForId(window_id, GHOST_SystemOHOS::TOUCH_MOVE,   button, x, y); break; 
    case 12: sys->postTouchEventForId(window_id, GHOST_SystemOHOS::TOUCH_UP,     button, x, y); break; 
    case 13: sys->postTouchEventForId(window_id, GHOST_SystemOHOS::TOUCH_CANCEL, button, x, y); break; 
  } 
} 

extern "C" __attribute__((visibility("default"))) 
void Blender_OnKeyEventW(int window_id, int action, int keyCode, int /*metaState*/) 
{ 
  auto *sys = ohos_system(); 
  if (!sys) return; 
  GHOST_TKey k = ohos_keycode_to_ghost(keyCode); 
  if (k == GHOST_kKeyUnknown) return; 
  char ch = '\0'; 
  if (k >= GHOST_kKeyA && k <= GHOST_kKeyZ) ch = 'a' + (k - GHOST_kKeyA); 
  else if (k >= GHOST_kKey0 && k <= GHOST_kKey9) ch = '0' + (k - GHOST_kKey0); 
  else if (k == GHOST_kKeySpace) ch = ' '; 
  sys->postKeyEventForId(window_id, action == 1, k, ch); 
} 

extern "C" __attribute__((visibility("default"))) 
void Blender_OnSurfaceResizeW(int window_id, uint32_t w, uint32_t h) 
{ 
  auto *sys = ohos_system(); 
  if (sys) sys->postSurfaceResizedForId(window_id, w, h); 
}
