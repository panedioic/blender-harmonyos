
/** \file
 * \ingroup GHOST
 * Declaration of GHOST_SystemOHOS class.
 */

#pragma once

#include "../GHOST_Types.h"
#include "GHOST_System.hh"
#include "GHOST_ModifierKeys.hh"

#include <atomic>
#include <deque>
#include <mutex>
#include <vector>
#include <condition_variable>
#include <unordered_map>

class GHOST_WindowOHOS;

/**
 * OHOS (HarmonyOS) implementation of GHOST_System.
 *
 * OHOS is quite different from X11/Win32/macOS:
 *  - There is no desktop; the only "window" is an XComponent surface
 *    given to us by the ArkTS host.
 *  - Input events arrive via XComponent callbacks on the UI thread and
 *    must be forwarded into GHOST's event queue by the host glue code.
 *  - We therefore do not run a native event loop here; processEvents()
 *    simply drives timers and dispatches whatever has been pushed.
 */
class GHOST_SystemOHOS : public GHOST_System {
 public:
  GHOST_SystemOHOS();
  ~GHOST_SystemOHOS() override;

  GHOST_TSuccess init() override;

  /* --- Time / displays --- */

  uint64_t getMilliSeconds() const override;

  uint8_t getNumDisplays() const override;
  void getMainDisplayDimensions(uint32_t &width, uint32_t &height) const override;
  void getAllDisplayDimensions(uint32_t &width, uint32_t &height) const override;

  /* --- Windows / contexts --- */

  GHOST_IWindow *createWindow(const char *title,
                              int32_t left,
                              int32_t top,
                              uint32_t width,
                              uint32_t height,
                              GHOST_TWindowState state,
                              GHOST_GPUSettings gpuSettings,
                              const bool exclusive = false,
                              const bool is_dialog = false,
                              const GHOST_IWindow *parentWindow = nullptr) override;

  GHOST_IContext *createOffscreenContext(GHOST_GPUSettings gpuSettings) override;
  GHOST_TSuccess disposeContext(GHOST_IContext *context) override;

  /* --- Event pump --- */

  bool processEvents(bool waitForEvent) override;

  /* --- Input state queries --- */

  GHOST_TSuccess getCursorPosition(int32_t &x, int32_t &y) const override;
  GHOST_TSuccess setCursorPosition(int32_t x, int32_t y) override;
  GHOST_TSuccess getPixelAtCursor(float r_color[3]) const override;

  GHOST_TSuccess getModifierKeys(GHOST_ModifierKeys &keys) const override;
  GHOST_TSuccess getButtons(GHOST_Buttons &buttons) const override;

  GHOST_TCapabilityFlag getCapabilities() const override;

  /* --- Clipboard --- */

  char *getClipboard(bool selection) const override;
  void putClipboard(const char *buffer, bool selection) const override;

  /* --- Dialogs / console --- */

  GHOST_TSuccess showMessageBox(const char *title,
                                const char *message,
                                const char *help_label,
                                const char *continue_label,
                                const char *link,
                                GHOST_DialogOptions dialog_options) const override;

  bool setConsoleWindowState(GHOST_TConsoleWindowState /*action*/) override
  {
    return false;
  }

  /* --- OHOS-specific --- */

  /**
   * Called by the NAPI glue code whenever the XComponent surface is
   * resized. Finds the single window and forwards the new size.
   */
  void notifySurfaceResized(uint32_t width, uint32_t height);

  /**
   * Mark a window as needing a redraw.
   */
  void addDirtyWindow(GHOST_WindowOHOS *window);

  /** Dirty windows to push GHOST_kEventWindowUpdate for on next pump. */
  std::vector<GHOST_WindowOHOS *> m_dirty_windows;

  bool generateWindowExposeEvents();

  /**
   * The following three methods are the ONLY safe way for the ArkTS /
   * XComponent (UI thread) side to feed input into GHOST. They never touch
   * the GHOST EventManager directly — they only update atomics and push
   * into an internal mutex-protected queue. The queue is drained on the
   * render thread inside processEvents().
   */
  void postCursorMove(int32_t x, int32_t y);
  void postButtonEvent(bool pressed, GHOST_TButton button, int32_t x, int32_t y);
  enum TouchPhase { TOUCH_DOWN = 0, TOUCH_UP = 1, TOUCH_MOVE = 2, TOUCH_CANCEL = 3 };

  /** Single-touch ≡ left mouse button emulation (touch id = primary finger). */
  void postTouchEvent(TouchPhase phase, int32_t touch_id, int32_t x, int32_t y);

  /* ─── OHOS 多窗口支持 ───────────────────────────────── */ 
  /** 由宿主 (napi_init.cpp) 注册的两个 hook。都在 Blender 主线程被调， 
   *  实现内部必须切到 UI 线程去操作 ArkTS 状态。 */ 
  using RequestCreateSubWindowFn  = void (*)(int id, int w, int h, const char *title); 
  using RequestDestroySubWindowFn = void (*)(int id); 
  void setSubWindowHooks(RequestCreateSubWindowFn create_cb, 
                         RequestDestroySubWindowFn destroy_cb) 
  { 
    m_request_create  = create_cb; 
    m_request_destroy = destroy_cb; 
  } 
  /** 由宿主线程调用：新 XComponent 的 surface 已就绪。 */ 
  void onSubWindowSurfaceReady(int id, void *native_window, uint32_t w, uint32_t h); 
  /** 由宿主线程调用：某个子窗口的 surface 被销毁（用户关掉了 ArkTS 面板）。 */ 
  void onSubWindowClosed(int id); 
  /** 反向查询：native_window → window_id。事件回调里会用到。 */ 
  int findWindowIdByNativeWindow(void *native_window) const; 
  GHOST_WindowOHOS *findGhostWindow(int id) const;
  void onGhostWindowDestroyed(int id);
  /** 主窗口 id 常量。 */ 
  static constexpr int kMainWindowId = 0; 
  /* 事件按 id 分发版本 —— UI 线程调用 */ 
  void postCursorMoveForId(int window_id, int32_t x, int32_t y); 
  void postButtonEventForId(int window_id, bool pressed, GHOST_TButton b, int32_t x, int32_t y); 
  void postTouchEventForId(int window_id, TouchPhase phase, int32_t touch_id, int32_t x, int32_t y); 
  void postKeyEventForId(int window_id, bool pressed, GHOST_TKey k, char utf8_char); 
  void postSurfaceResizedForId(int window_id, uint32_t w, uint32_t h); 

  // 先全都当成public处理
//  private:
  /* Queued input from the UI thread, waiting to be dispatched on the render thread. */
  struct QueuedInput {
    enum Kind { CURSOR_MOVE, BUTTON_DOWN, BUTTON_UP, KEY_DOWN, KEY_UP, RESIZE } kind;
    int window_id;          /* ★ 新增 */ 
    GHOST_TButton button;
    GHOST_TKey key;
    char utf8_char;
    int32_t x;
    int32_t y;
    uint32_t rw, rh;        /* 用于 RESIZE */ 
    uint64_t time_ms;
  };

  std::mutex m_input_mutex;
  std::deque<QueuedInput> m_input_queue;

  /* Latest cursor / button state, readable from any thread. */
  std::atomic<int32_t> m_cursor_x{0};
  std::atomic<int32_t> m_cursor_y{0};
  std::atomic<uint32_t> m_button_mask{0};   /* bit N == (1u << GHOST_kButtonMaskN) */
  std::atomic<int32_t> m_primary_touch_id{-1};

  bool drainInputQueue();

  GHOST_IWindow *getPrimaryWindow() const;
  
  // （写错了，回头删掉）
  bool getSystemDir(int version, char *dir, int dirlen) const;

  // （写错了，回头删掉）
  const char *getSystemDir(int version, const char *versionstr) const;
  const char *getUserDir(int version, const char *versionstr) const;
  const char *getBinaryDir() const;

  /** Called from UI thread when XComponent surface size changes.
   *  Only records the new size; real dispatch happens on render thread. */
  void postSurfaceResized(uint32_t width, uint32_t height);

  /* Pending surface resize, produced on UI thread, consumed on render thread. */
  std::atomic<uint32_t> m_pending_resize_w{0};
  std::atomic<uint32_t> m_pending_resize_h{0};
  std::atomic<bool>     m_has_pending_resize{false};

  /** Called from UI thread when a key is pressed/released. */
  void postKeyEvent(bool pressed, GHOST_TKey ghost_key, char utf8_char);

  // key events
  GHOST_ModifierKeys m_modifierKeys;
  void updateModifierFromKey(bool pressed, GHOST_TKey key);

  /* ─── OHOS 多窗口支持 ───────────────────────────────── */ 
  struct WindowRecord { 
    int id = -1; 
    void *native_window = nullptr; 
    uint32_t w = 0, h = 0; 
    GHOST_WindowOHOS *ghost_window = nullptr;  /* 绑定后填 */ 
    bool surface_ready = false;                /* onSubWindowSurfaceReady 后置位 */ 
  }; 
  mutable std::mutex m_reg_mutex; 
  std::condition_variable m_reg_cv; 
  std::unordered_map<int, WindowRecord> m_records; 
  int m_next_window_id = 1;   /* 0 保留给主窗口 */ 
  RequestCreateSubWindowFn  m_request_create  = nullptr; 
  RequestDestroySubWindowFn m_request_destroy = nullptr; 
  /** 分配一个新的子窗口 id，插入一条"pending"记录。 */ 
  int allocateSubWindowId(uint32_t w, uint32_t h); 
  /** 阻塞等待 `id` 对应的 surface 就绪。返回 nullptr 表示超时。 */ 
  WindowRecord *waitForSurface(int id, int timeout_ms); 
  /* QueuedInput 新增 window_id 字段（见 .cc） */

};