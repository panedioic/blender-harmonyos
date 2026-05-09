#pragma once

#include "GHOST_SystemPaths.hh"

class GHOST_SystemPathsOHOS : public GHOST_SystemPaths {
 public:
  GHOST_SystemPathsOHOS() = default;
  ~GHOST_SystemPathsOHOS() override = default;

  const char *getSystemDir(int version, const char *versionstr) const override;
  const char *getUserDir(int version, const char *versionstr) const override;
  const char *getUserSpecialDir(GHOST_TUserSpecialDirTypes type) const override;
  const char *getBinaryDir() const override;
  void addToSystemRecentFiles(const char *filepath) const override;
};