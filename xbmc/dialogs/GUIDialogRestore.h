#pragma once

#include "guilib/GUIDialog.h"

#include <string>
#include <utility>
#include <vector>

class CGUIDialogProgress;

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
    std::string selected;
    std::vector<std::string> protect;
  };

  SMBConfig ReadFields();
  bool LoadConfig(SMBConfig& config);
  bool SaveConfig(const SMBConfig& config);
  bool CopyFromSMB(const SMBConfig& config);
  bool CopyDirectory(const SMBConfig& config, const std::string& subPath,
                     const std::string& destPath, std::string& diag,
                     CGUIDialogProgress* progress = nullptr,
                     int* progressAnim = nullptr);
  bool UploadDirectory(const std::string& localPath, const std::string& smbBase,
                       const SMBConfig& config, std::string& diag,
                       CGUIDialogProgress* progress, int* progressAnim);

  void OnSave();
  void OnBackup();
  void OnRestore();
  void OnSelectAll();
  void OnClear();

  std::string ResolveUserdataPath(const std::string& fullPath, const SMBConfig& config,
                                  std::string& diag);

  // Reads the checked protect-file toggles into a list of file names.
  std::vector<std::string> ReadProtectedFiles();
  // Sets one toggle button's selected state (GUI_MSG_SET/DESELECTED).
  void SetToggleSelected(int controlId, bool selected);
  // Reads the current content of each protected file from special://home/userdata/.
  // Returns name->data pairs; files that don't exist are skipped. tvOS xml is
  // read from NSUserDefaults via CTVOSFile.
  std::vector<std::pair<std::string, std::string>> CaptureProtectedFiles(
      const std::vector<std::string>& files, std::string& diag);
  // Writes captured content back to special://home/userdata/. tvOS xml goes
  // through CTVOSFile into NSUserDefaults.
  void RestoreProtectedFiles(const std::vector<std::pair<std::string, std::string>>& captured,
                             std::string& diag);

#if defined(TARGET_DARWIN_TVOS)
  void SyncTVOSXmlPersistence(const std::string& realDir, std::string& diag);
#endif

  static constexpr int CONTROL_EDIT_SERVER = 10;
  static constexpr int CONTROL_EDIT_USERNAME = 11;
  static constexpr int CONTROL_EDIT_PASSWORD = 12;
  static constexpr int CONTROL_EDIT_SHAREPATH = 13;
  static constexpr int CONTROL_EDIT_SELECTED = 14;
  static constexpr int CONTROL_BUTTON_BROWSE = 23;
  static constexpr int CONTROL_BUTTON_BACKUP = 24;
  static constexpr int CONTROL_BUTTON_SAVE = 22;
  static constexpr int CONTROL_BUTTON_RESTORE = 20;
  static constexpr int CONTROL_BUTTON_CANCEL = 21;
  static constexpr int CONTROL_BUTTON_SELECT_ALL = 25;
  static constexpr int CONTROL_BUTTON_CLEAR = 26;
  static constexpr int CONTROL_TOGGLE_BASE = 30; // 30..38 = the 9 protect toggles

  void OnBrowse();
};
