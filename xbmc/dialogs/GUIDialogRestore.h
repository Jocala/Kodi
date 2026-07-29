#pragma once

#include "guilib/GUIDialog.h"

#include <string>

class CGUIDialogRestore : public CGUIDialog
{
public:
  CGUIDialogRestore();
  ~CGUIDialogRestore() override = default;

  bool OnMessage(CGUIMessage& message) override;

private:
  struct SMBConfig
  {
    std::string host;
    std::string username;
    std::string password;
    std::string sharepath;
  };

  SMBConfig ReadFields();
  bool LoadConfig(SMBConfig& config);
  bool SaveConfig(const SMBConfig& config);
  bool CopyFromSMB(const SMBConfig& config);
  bool CopyDirectory(const SMBConfig& config, const std::string& subPath,
                     const std::string& destPath);

  void OnSave();
  void OnRestore();

  static constexpr int CONTROL_EDIT_SERVER = 10;
  static constexpr int CONTROL_EDIT_USERNAME = 11;
  static constexpr int CONTROL_EDIT_PASSWORD = 12;
  static constexpr int CONTROL_EDIT_SHAREPATH = 13;
  static constexpr int CONTROL_BUTTON_SAVE = 22;
  static constexpr int CONTROL_BUTTON_RESTORE = 20;
  static constexpr int CONTROL_BUTTON_CANCEL = 21;
};
