#pragma once

#include <string>

class CRestoreManager
{
public:
  static bool Restore();

private:
  struct RestoreConfig
  {
    std::string host;
    std::string share;
    std::string username;
    std::string password;
  };

  static bool PromptForConfig(RestoreConfig& config);
  static bool LoadConfig(RestoreConfig& config);
  static bool SaveConfig(const RestoreConfig& config);
  static bool CopyFromSMB(const RestoreConfig& config);
  static bool CopyDirectory(const std::string& sourcePath, const std::string& destPath);
};
