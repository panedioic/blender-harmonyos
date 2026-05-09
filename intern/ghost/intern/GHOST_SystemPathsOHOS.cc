/* SPDX-FileCopyrightText: 2024 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "GHOST_SystemPathsOHOS.hh"

#include <cstdlib>
#include <string>

/* getSystemDir 返回的是 Blender 数据根目录（不含 /scripts），
 * appdir 会在后面自动拼 /scripts/modules 等子路径。
 * 例：BLENDER_SYSTEM_SCRIPTS = .../files/blender/scripts
 *     → 返回 .../files/blender
 * 然后 get_path_system_ex 拼出 .../files/blender/scripts/modules  */
const char *GHOST_SystemPathsOHOS::getSystemDir(int /*version*/,
                                                 const char * /*versionstr*/) const
{
    // 先写死
    static std::string s_dir;
    const char *ss = "/data/storage/el2/base/haps/entry/files/blender";
    if (ss && ss[0]) {
        s_dir = ss;
        return s_dir.c_str();
    }
  return nullptr;
}

const char *GHOST_SystemPathsOHOS::getUserDir(int /*version*/,
                                               const char * /*versionstr*/) const
{
  /* OHOS 上没有传统意义的用户目录，复用 system dir 即可。
   * 这样 BLENDER_USER_CONFIG 等也能找到合理的落点。 */
    // 先写死
    static std::string s_dir;
    const char *ss = "/data/storage/el2/base/haps/entry/files/blender";
    if (ss && ss[0]) {
        s_dir = ss;
        return s_dir.c_str();
    }
  return nullptr;
}

const char *GHOST_SystemPathsOHOS::getUserSpecialDir(GHOST_TUserSpecialDirTypes /*type*/) const
{
  return nullptr;
}

const char *GHOST_SystemPathsOHOS::getBinaryDir() const
{
  return nullptr;
}

void GHOST_SystemPathsOHOS::addToSystemRecentFiles(const char * /*filepath*/) const
{
  /* no-op */
}