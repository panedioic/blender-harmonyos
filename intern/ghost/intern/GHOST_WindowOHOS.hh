/** \file
 * \ingroup GHOST
 * Declaration of GHOST_WindowOHOS class.
 */

#pragma once

#include "GHOST_Window.hh"

/* OHOS native window forward-decl, avoid dragging the whole header. */
struct NativeWindow;
typedef struct NativeWindow OHNativeWindow;

class GHOST_SystemOHOS;

/**
 * OHOS (HarmonyOS) implementation of GHOST_IWindow.
 *
 * On OHOS we only ever have a single XComponent surface supplied by the
 * ArkTS host. That surface is passed in from the NAPI layer (see
 * Blender_SetNativeWindow()) and this class simply wraps it so the rest
 * of GHOST / WM can treat it like any other window.
 */
class GHOST_WindowOHOS : public GHOST_Window {
 public:
  GHOST_WindowOHOS(GHOST_SystemOHOS *system,
                   const char *title,
                   int32_t left,
                   int32_t top,
                   uint32_t width,
                   uint32_t height,
                   GHOST_TWindowState state,
                   GHOST_TDrawingContextType type,
                   const bool stereoVisual,
                   const bool is_debug,
                   const GHOST_GPUDevice &preferred_device);

  ~GHOST_WindowOHOS() override;

  /* --- GHOST_IWindow interface --- */

  bool getValid() const override;

  void setTitle(const char *title) override;
  std::string getTitle() const override;

  void getWindowBounds(GHOST_Rect &bounds) const override;
  void getClientBounds(GHOST_Rect &bounds) const override;

  GHOST_TSuccess setClientWidth(uint32_t width) override;
  GHOST_TSuccess setClientHeight(uint32_t height) override;
  GHOST_TSuccess setClientSize(uint32_t width, uint32_t height) override;

  void screenToClient(int32_t inX, int32_t inY, int32_t &outX, int32_t &outY) const override;
  void clientToScreen(int32_t inX, int32_t inY, int32_t &outX, int32_t &outY) const override;

  GHOST_TWindowState getState() const override;
  GHOST_TSuccess setState(GHOST_TWindowState state) override;
  GHOST_TSuccess setOrder(GHOST_TWindowOrder order) override;

  GHOST_TSuccess invalidate() override;

  GHOST_TSuccess setProgressBar(float progress) override;
  GHOST_TSuccess endProgressBar() override;

  bool isDialog() const override;

  uint16_t getDPIHint() override;

  /* OHOS-specific: let the system feed resize events back in. */
  void notifyResize(uint32_t width, uint32_t height);

  OHNativeWindow *getNativeWindow() const
  {
    return m_nativeWindow;
  }

  /** Returns true (and clears the flag) if the surface was resized since last check. */
  bool consumeResizeFlag();

 protected:
  /**
   * Create the drawing context (Vulkan only for OHOS port).
   */
  GHOST_Context *newDrawingContext(GHOST_TDrawingContextType type) override;

  /* --- Cursor management: stubs, real implementation TBD --- */

  GHOST_TSuccess setWindowCursorVisibility(bool visible) override;
  GHOST_TSuccess setWindowCursorGrab(GHOST_TGrabCursorMode mode) override;
  GHOST_TSuccess setWindowCursorShape(GHOST_TStandardCursor shape) override;
  GHOST_TSuccess hasCursorShape(GHOST_TStandardCursor shape) override;
  GHOST_TSuccess setWindowCustomCursorShape(uint8_t *bitmap,
                                            uint8_t *mask,
                                            int sizex,
                                            int sizey,
                                            int hotX,
                                            int hotY,
                                            bool canInvertColor) override;

 private:
  /* Disable copy. */
  GHOST_WindowOHOS(const GHOST_WindowOHOS &) = delete;
  GHOST_WindowOHOS &operator=(const GHOST_WindowOHOS &) = delete;

  GHOST_SystemOHOS *m_system;

  /** Native surface handed to us from ArkTS/XComponent. */
  OHNativeWindow *m_nativeWindow;

  /** Cached geometry (OHOS has no desktop coords, so bounds == client). */
  int32_t m_left;
  int32_t m_top;
  uint32_t m_width;
  uint32_t m_height;

  std::string m_title;

  GHOST_TWindowState m_state;

  bool m_is_debug_context;
  GHOST_GPUDevice m_preferred_device;

  bool m_valid_setup;

  // resize window
  std::atomic<bool> m_resize_pending{false};
};