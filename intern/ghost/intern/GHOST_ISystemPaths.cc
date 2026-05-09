/* SPDX-FileCopyrightText: 2001-2002 NaN Holding BV. All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup GHOST
 */

/**
 * Copyright (C) 2001 NaN Technologies B.V.
 */

#include "GHOST_ISystemPaths.hh"

#ifdef WIN32
#  include "GHOST_SystemPathsWin32.hh"
#else
#  ifdef __APPLE__
#    include "GHOST_SystemPathsCocoa.hh"
#  elif defined(__OHOS__)         
/* OHOS PATCH
 *  增加鸿蒙系统 GHOST_SystemPathsOHOS */
#    include "GHOST_SystemPathsOHOS.hh"
#  else
#    include "GHOST_SystemPathsUnix.hh"
#  endif
#endif

GHOST_ISystemPaths *GHOST_ISystemPaths::m_systemPaths = nullptr;

GHOST_TSuccess GHOST_ISystemPaths::create()
{
  GHOST_TSuccess success;
  if (!m_systemPaths) {
#ifdef WIN32
    m_systemPaths = new GHOST_SystemPathsWin32();
#else
#  ifdef __APPLE__
    m_systemPaths = new GHOST_SystemPathsCocoa();
#  elif defined(__OHOS__) 
/* OHOS PATCH
 *  使用鸿蒙系统 GHOST_SystemPathsOHOS */
    m_systemPaths = new GHOST_SystemPathsOHOS();
#  else
    m_systemPaths = new GHOST_SystemPathsUnix();
#  endif
#endif
    success = m_systemPaths != nullptr ? GHOST_kSuccess : GHOST_kFailure;
  }
  else {
    success = GHOST_kFailure;
  }
  return success;
}

GHOST_TSuccess GHOST_ISystemPaths::dispose()
{
  GHOST_TSuccess success = GHOST_kSuccess;
  if (m_systemPaths) {
    delete m_systemPaths;
    m_systemPaths = nullptr;
  }
  else {
    success = GHOST_kFailure;
  }
  return success;
}

GHOST_ISystemPaths *GHOST_ISystemPaths::get()
{
  if (!m_systemPaths) {
    create();
  }
  return m_systemPaths;
}
