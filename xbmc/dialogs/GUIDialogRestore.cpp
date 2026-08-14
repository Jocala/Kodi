#include "GUIDialogRestore.h"

#include "FileItem.h"
#include "FileItemList.h"
#include "MediaSource.h"
#include "PasswordManager.h"
#include "ServiceBroker.h"
#include "dialogs/GUIDialogFileBrowser.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "dialogs/GUIDialogProgress.h"

#include "filesystem/Directory.h"
#include "filesystem/File.h"
#include "filesystem/SpecialProtocol.h"
#include "guilib/GUIComponent.h"
#include "guilib/GUIEditControl.h"
#include "guilib/GUIMessage.h"
#include "guilib/GUIWindowManager.h"
#include "addons/Skin.h"
#include "messaging/ApplicationMessenger.h"
#include "messaging/helpers/DialogOKHelper.h"
#include "profiles/ProfileManager.h"
#include "settings/MediaSourceSettings.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "storage/MediaManager.h"
#include "utils/JSONVariantParser.h"
#include "utils/JSONVariantWriter.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "utils/Variant.h"
#include "utils/log.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

// clang-format off
#if defined(TARGET_DARWIN_TVOS)
#include "platform/darwin/tvos/TVOSNSUserDefaults.h"
#endif
// clang-format on

using namespace KODI::MESSAGING;

static const std::string CONFIG_PATH = "special://home/smb_restore.json";

// Files that a restore must not overwrite. Mirrors adblink's restore-protect
// list. Kept in the same order as the toggle controls (ids 30..38).
static const std::vector<std::string> PROTECTED_FILES = {
    "guisettings.xml",    // 30
    "advancedsettings.xml", // 31
    "sources.xml",        // 32
    "favourites.xml",     // 33
    "profiles.xml",       // 34
    "RssFeeds.xml",       // 35
    "mediasources.xml",   // 36
    "passwords.xml",      // 37
    "Lircmap.xml",        // 38
};

CGUIDialogRestore::CGUIDialogRestore()
  : CGUIDialog(WINDOW_DIALOG_RESTORE, "DialogRestore.xml")
{
  m_loadType = KEEP_IN_MEMORY;
}

bool CGUIDialogRestore::OnMessage(CGUIMessage& message)
{
  switch (message.GetMessage())
  {
    case GUI_MSG_WINDOW_INIT:
    {
      CGUIDialog::OnMessage(message);
      SMBConfig config;
      LoadConfig(config);
      auto setEdit = [this](int id, const std::string& val)
      {
        CGUIEditControl* edit = dynamic_cast<CGUIEditControl*>(GetControl(id));
        if (edit && !val.empty())
          edit->SetLabel2(val);
      };
      setEdit(CONTROL_EDIT_SERVER, config.host);
      setEdit(CONTROL_EDIT_USERNAME, config.username);
      setEdit(CONTROL_EDIT_PASSWORD, config.password);
      setEdit(CONTROL_EDIT_SHAREPATH, config.sharepath);

      // Pre-check protect toggles from saved config
      for (int i = 0; i < static_cast<int>(PROTECTED_FILES.size()); i++)
      {
        bool checked =
            std::find(config.protect.begin(), config.protect.end(), PROTECTED_FILES[i]) !=
            config.protect.end();
        SetToggleSelected(CONTROL_TOGGLE_BASE + i, checked);
      }

      return true;
    }
    case GUI_MSG_CLICKED:
    {
      const int controlId = message.GetSenderId();
      if (controlId == CONTROL_BUTTON_SAVE)
      {
        OnSave();
        return true;
      }
      else if (controlId == CONTROL_BUTTON_BACKUP)
      {
        OnBackup();
        return true;
      }
      else if (controlId == CONTROL_BUTTON_BROWSE)
      {
        OnBrowse();
        return true;
      }
      else if (controlId == CONTROL_BUTTON_RESTORE)
      {
        OnRestore();
        return true;
      }
      else if (controlId == CONTROL_BUTTON_CANCEL)
      {
        Close();
        return true;
      }
      else if (controlId == CONTROL_BUTTON_SELECT_ALL)
      {
        OnSelectAll();
        return true;
      }
      else if (controlId == CONTROL_BUTTON_CLEAR)
      {
        OnClear();
        return true;
      }
    }
    break;
    default:
      break;
  }
  return CGUIDialog::OnMessage(message);
}

CGUIDialogRestore::SMBConfig CGUIDialogRestore::ReadFields()
{
  auto readEdit = [this](int id) -> std::string
  {
    CGUIEditControl* edit = dynamic_cast<CGUIEditControl*>(GetControl(id));
    if (edit)
      return edit->GetLabel2();
    return "";
  };
  return {readEdit(CONTROL_EDIT_SERVER), readEdit(CONTROL_EDIT_USERNAME),
          readEdit(CONTROL_EDIT_PASSWORD), readEdit(CONTROL_EDIT_SHAREPATH),
          readEdit(CONTROL_EDIT_SELECTED)};
}

std::vector<std::string> CGUIDialogRestore::ReadProtectedFiles()
{
  std::vector<std::string> files;
  for (int i = 0; i < static_cast<int>(PROTECTED_FILES.size()); i++)
  {
    CGUIMessage msg(GUI_MSG_IS_SELECTED, GetID(), CONTROL_TOGGLE_BASE + i);
    if (OnMessage(msg) && msg.GetParam1() == 1)
      files.push_back(PROTECTED_FILES[i]);
  }
  return files;
}

void CGUIDialogRestore::SetToggleSelected(int controlId, bool selected)
{
  CGUIMessage msg(selected ? GUI_MSG_SET_SELECTED : GUI_MSG_SET_DESELECTED, GetID(), controlId);
  OnMessage(msg);
}

void CGUIDialogRestore::OnSelectAll()
{
  for (int i = 0; i < static_cast<int>(PROTECTED_FILES.size()); i++)
    SetToggleSelected(CONTROL_TOGGLE_BASE + i, true);
}

void CGUIDialogRestore::OnClear()
{
  for (int i = 0; i < static_cast<int>(PROTECTED_FILES.size()); i++)
    SetToggleSelected(CONTROL_TOGGLE_BASE + i, false);
}

std::vector<std::pair<std::string, std::string>> CGUIDialogRestore::CaptureProtectedFiles(
    const std::vector<std::string>& files, std::string& diag)
{
  auto D = [&diag](const std::string& msg) { diag += msg + "\n"; };

  std::vector<std::pair<std::string, std::string>> captured;
  for (const auto& f : files)
  {
    std::string path = URIUtils::AddFileToFolder("special://home/userdata/", f);
    XFILE::CFile file;
    if (!file.Open(path, XFILE::READ_NO_CACHE))
    {
      D("  protected file not present, skipping: " + f);
      continue;
    }

    std::string data;
    char buf[65536];
    ssize_t n;
    while ((n = file.Read(buf, sizeof(buf))) > 0)
      data.append(buf, n);
    file.Close();

    if (data.empty())
    {
      D("  protected file empty, skipping: " + f);
      continue;
    }

    captured.emplace_back(f, data);
    D("  captured protected file: " + f + " (" + std::to_string(data.size()) + " bytes)");
  }
  return captured;
}

void CGUIDialogRestore::RestoreProtectedFiles(
    const std::vector<std::pair<std::string, std::string>>& captured, std::string& diag)
{
  auto D = [&diag](const std::string& msg) { diag += msg + "\n"; };

  for (const auto& [name, data] : captured)
  {
    std::string path = URIUtils::AddFileToFolder("special://home/userdata/", name);
    XFILE::CFile file;
    if (!file.OpenForWrite(path, true))
    {
      D("  FAILED to open protected file for write: " + name);
      continue;
    }

    if (file.Write(data.c_str(), data.size()) != static_cast<ssize_t>(data.size()))
      D("  FAILED to write protected file: " + name);
    else
      D("  restored protected file: " + name);
    file.Close();
  }
}

bool CGUIDialogRestore::LoadConfig(SMBConfig& config)
{
  XFILE::CFile file;
  if (!file.Open(CONFIG_PATH, XFILE::READ_NO_CACHE))
    return false;

  std::string json;
  char buf[4096];
  ssize_t n;
  while ((n = file.Read(buf, sizeof(buf))) > 0)
    json.append(buf, n);
  file.Close();

  CVariant variant;
  CJSONVariantParser::Parse(json, variant);

  if (!variant.isObject())
    return false;

  if (variant["host"].isString())
    config.host = variant["host"].asString();
  if (variant["username"].isString())
    config.username = variant["username"].asString();
  if (variant["password"].isString())
    config.password = variant["password"].asString();
  if (variant["sharepath"].isString())
    config.sharepath = variant["sharepath"].asString();
  config.protect.clear();
  if (variant["protect"].isArray())
  {
    for (CVariant::const_iterator_array it = variant["protect"].begin_array();
         it != variant["protect"].end_array(); ++it)
    {
      if (it->isString())
        config.protect.push_back(it->asString());
    }
  }
  return true;
}

bool CGUIDialogRestore::SaveConfig(const SMBConfig& config)
{
  CVariant variant(CVariant::VariantTypeObject);
  variant["host"] = config.host;
  variant["username"] = config.username;
  variant["password"] = config.password;
  variant["sharepath"] = config.sharepath;
  variant["protect"] = CVariant(CVariant::VariantTypeArray);
  for (const auto& f : config.protect)
    variant["protect"].push_back(f);

  std::string json;
  if (!CJSONVariantWriter::Write(variant, json, true))
    return false;

  XFILE::CFile file;
  if (!file.OpenForWrite(CONFIG_PATH, true))
    return false;

  ssize_t written = file.Write(json.c_str(), json.size());
  file.Close();
  return written == static_cast<ssize_t>(json.size());
}

void CGUIDialogRestore::OnBrowse()
{
  SMBConfig config = ReadFields();

  if (config.host.empty() || config.username.empty() || config.password.empty())
  {
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Error, "Restore",
                                          "Host, username, and password required");
    return;
  }

  // Build SMB URL starting at sharepath
  std::string smbBase = StringUtils::Format("smb://{}:{}@{}", config.username, config.password,
                                             config.host);
  if (!config.sharepath.empty())
    smbBase = URIUtils::AddFileToFolder(smbBase, config.sharepath);

  // Save credentials to password manager
  CURL pwUrl("smb://" + config.host + "/");
  pwUrl.SetUserName(config.username);
  pwUrl.SetPassword(config.password);
  CPasswordManager::GetInstance().SaveAuthenticatedURL(pwUrl, true);

  // Open file browser at the SMB path, folders only
  std::string chosenPath;
  if (CGUIDialogFileBrowser::ShowAndGetFile(smbBase, "/", "Select backup folder", chosenPath))
  {
    // Extract the relative path from smb://user:pass@host/
    std::string prefix = StringUtils::Format("smb://{}:{}@{}", config.username, config.password,
                                              config.host);
    URIUtils::RemoveSlashAtEnd(prefix);
    std::string relPath;
    if (StringUtils::StartsWithNoCase(chosenPath, prefix + "/"))
    {
      relPath = chosenPath.substr(prefix.length() + 1);
      URIUtils::RemoveSlashAtEnd(relPath);
    }
    else
    {
      std::string prefix2 = StringUtils::Format("smb://{}", config.host);
      URIUtils::RemoveSlashAtEnd(prefix2);
      if (StringUtils::StartsWithNoCase(chosenPath, prefix2 + "/"))
      {
        relPath = chosenPath.substr(prefix2.length() + 1);
        URIUtils::RemoveSlashAtEnd(relPath);
      }
      else
      {
        relPath = chosenPath;
      }
    }

    // Strip the sharepath prefix to get just the selected subfolder
    if (!config.sharepath.empty() && StringUtils::StartsWith(relPath, config.sharepath + "/"))
    {
      std::string sel = relPath.substr(config.sharepath.length() + 1);
      URIUtils::RemoveSlashAtEnd(sel);
      CGUIEditControl* edit = dynamic_cast<CGUIEditControl*>(GetControl(CONTROL_EDIT_SELECTED));
      if (edit)
        edit->SetLabel2(sel);
    }
  }
}

std::string CGUIDialogRestore::ResolveUserdataPath(const std::string& fullPath,
                                                    const SMBConfig& config,
                                                    std::string& diag)
{
  auto D = [&diag](const std::string& msg) { diag += msg + "\n"; };

  D("Resolver: inspecting " + fullPath);

  std::string smbUrl = StringUtils::Format("smb://{}:{}@{}/{}", config.username,
                                             config.password, config.host, fullPath);
  CFileItemList items;
  if (!XFILE::CDirectory::GetDirectory(smbUrl, items, "", XFILE::DIR_FLAG_DEFAULTS))
  {
    D("Resolver: cannot list folder");
    return "";
  }

  bool hasUserdataSubfolder = false;
  bool hasDatabaseFolder = false;
  bool hasGuiSettings = false;
  bool hasAddonData = false;

  for (int i = 0; i < items.Size(); i++)
  {
    std::string itemPath = items[i]->GetPath();
    URIUtils::RemoveSlashAtEnd(itemPath);
    std::string name = URIUtils::GetFileName(itemPath);

    if (items[i]->IsFolder())
    {
      if (name == "userdata")
        hasUserdataSubfolder = true;
      if (name == "Database")
        hasDatabaseFolder = true;
      if (name == "addon_data")
        hasAddonData = true;
    }
    else if (name == "guisettings.xml")
    {
      hasGuiSettings = true;
    }
  }

  // Layout 1: full Kodi backup — has a userdata/ subfolder
  if (hasUserdataSubfolder)
  {
    D("Resolver: full Kodi backup layout (has userdata/)");
    return URIUtils::AddFileToFolder(fullPath, "userdata");
  }

  // Layout 2: direct userdata folder — has Database + guisettings.xml + addon_data
  if (hasDatabaseFolder && hasGuiSettings && hasAddonData)
  {
    D("Resolver: direct userdata folder layout");
    return fullPath;
  }

  D("Resolver: doesn't match any known layout");
  return "";
}

void CGUIDialogRestore::OnSave()
{
  SMBConfig config = ReadFields();

  if (config.host.empty() || config.username.empty() || config.password.empty() ||
      config.sharepath.empty())
  {
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Error, "Restore",
                                          "All fields required");
    return;
  }

  if (SaveConfig(config))
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Info, "Restore",
                                          "Settings saved");
  else
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Error, "Restore",
                                          "Failed to save");
  Close();
}

void CGUIDialogRestore::OnBackup()
{
  std::string diag;
  auto D = [&diag](const std::string& msg) { diag += msg + "\n"; };
  D("=== Backup started ===");

  SMBConfig config = ReadFields();
  D("host=" + config.host + " share=" + config.sharepath);

  if (config.host.empty() || config.username.empty() || config.password.empty() ||
      config.sharepath.empty())
  {
    D("FAILED: validation");
    HELPERS::ShowOKDialogText(CVariant{"Backup"}, CVariant{"All fields required"});
    return;
  }

  SaveConfig(config);
  D("config saved");

  // Build timestamp folder name
  auto now = std::chrono::system_clock::now();
  auto tt = std::chrono::system_clock::to_time_t(now);
  std::tm tm;
  gmtime_r(&tt, &tm);
  std::string ts = StringUtils::Format("backup.{:04d}{:02d}{:02d}.{:02d}{:02d}{:02d}",
                                        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                                        tm.tm_hour, tm.tm_min, tm.tm_sec);

  // Backups go to the sharepath (which is the container folder)
  D("backup path: " + config.sharepath);

  // Build SMB base path with timestamp
  std::string smbBase = StringUtils::Format("smb://{}:{}@{}/{}/{}", config.username,
                                             config.password, config.host, config.sharepath, ts);

  D("backup path: " + smbBase);

  // Open progress dialog
  CGUIDialogProgress* progress = dynamic_cast<CGUIDialogProgress*>(
      CServiceBroker::GetGUI()->GetWindowManager().GetWindow(WINDOW_DIALOG_PROGRESS));
  int progressAnim = 0;
  if (progress)
  {
    progress->SetHeading(CVariant{"Backing up to SMB..."});
    progress->SetLine(0, CVariant{"Uploading files..."});
    progress->SetPercentage(0);
    progress->Open();
  }

  // Create the backup directory on SMB
  D("creating remote dir: " + smbBase);
  XFILE::CDirectory::Create(smbBase);

  // Upload userdata
  D("starting upload");
  std::string localPath = "special://home/userdata/";
  bool ok = UploadDirectory(localPath, smbBase, config, diag, progress, &progressAnim);

  if (progress)
    progress->Close();

  if (!ok)
  {
    D("FAILED: UploadDirectory reported errors");
    HELPERS::ShowOKDialogText(CVariant{"Backup"}, CVariant{"Some files failed to upload"});
    return;
  }

  D("=== Backup completed successfully ===");
  CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Info, "Backup",
                                        "Backup complete!");
}

void CGUIDialogRestore::OnRestore()
{
  std::string diag;

  auto D = [&diag](const std::string& msg)
  {
    diag += msg + "\n";
  };

  D("=== Restore started ===");

  SMBConfig config = ReadFields();
  config.protect = ReadProtectedFiles();

  if (config.host.empty() || config.username.empty() || config.password.empty() ||
      config.sharepath.empty())
  {
    D("FAILED: validation - empty field");
    {
      XFILE::CFile f;
      if (f.OpenForWrite("special://home/restore_diag.txt", true))
      {
        f.Write(diag.c_str(), diag.size());
        f.Close();
      }
    }
    HELPERS::ShowOKDialogText(CVariant{"Restore"}, CVariant{"All fields required"});
    return;
  }

  // Build full restore path: sharepath + selected subfolder
  std::string fullPath = config.sharepath;
  if (!config.selected.empty())
    fullPath = URIUtils::AddFileToFolder(fullPath, config.selected);
  D("host=" + config.host + " fullPath=" + fullPath);

  // Resolve the selected folder to the actual userdata content.
  // Handles two layouts: full Kodi backup (has userdata/ subfolder)
  // or direct userdata folder (has Database/ + guisettings.xml + addon_data/).
  std::string resolvedPath = ResolveUserdataPath(fullPath, config, diag);
  if (resolvedPath.empty())
  {
    D("FAILED: selected folder doesn't look like a valid backup or userdata");
    {
      XFILE::CFile f;
      if (f.OpenForWrite("special://home/restore_diag.txt", true))
      {
        f.Write(diag.c_str(), diag.size());
        f.Close();
      }
    }
    HELPERS::ShowOKDialogText(CVariant{"Restore"},
                              CVariant{"This doesn't look like a valid Kodi backup or userdata "
                                       "folder.\n\nA backup must contain either:\n- A 'userdata' "
                                       "folder (full Kodi backup), or\n- Database/, "
                                       "guisettings.xml, addon_data/ (userdata folder directly)"});
    return;
  }
  fullPath = resolvedPath;
  D("resolved fullPath=" + fullPath);

  SaveConfig(config);
  D("config saved");

  // Save credentials to password manager
  {
    CURL pwUrl("smb://" + config.host + "/" + fullPath);
    pwUrl.SetUserName(config.username);
    pwUrl.SetPassword(config.password);
    CPasswordManager::GetInstance().SaveAuthenticatedURL(pwUrl, true);
    D("credentials saved to password manager");
  }

  // Test connection with credentials embedded in URL
  std::string smbUrl = StringUtils::Format("smb://{}:{}@{}/{}", config.username, config.password,
                                             config.host, fullPath);
  D("GetDirectory: smb://" + config.username + ":****@" + config.host + "/" + config.sharepath);

  CFileItemList items;
  if (!XFILE::CDirectory::GetDirectory(smbUrl, items, "", XFILE::DIR_FLAG_DEFAULTS))
  {
    D("FAILED: GetDirectory - cannot list SMB share");
    {
      XFILE::CFile f;
      if (f.OpenForWrite("special://home/restore_diag.txt", true))
      {
        f.Write(diag.c_str(), diag.size());
        f.Close();
      }
    }
    HELPERS::ShowOKDialogText(CVariant{"Restore"}, CVariant{"Cannot list SMB share"});
    return;
  }

  D("GetDirectory OK, items=" + std::to_string(items.Size()));

  for (int i = 0; i < items.Size(); i++)
  {
    const auto& item = items[i];
    D("  item[" + std::to_string(i) + "]: path=" + item->GetPath() + " isFolder=" +
      (item->IsFolder() ? "true" : "false"));
  }

  // Flush all open Kodi config files before swapping
  D("flushing settings before swap");
  CServiceBroker::GetSettingsComponent()->GetSettings()->Save();
  CServiceBroker::GetSettingsComponent()->GetProfileManager()->Save();
  CMediaSourceSettings::GetInstance().Save();
  D("settings flushed");

  // Open progress dialog with cycling bar
  CGUIDialogProgress* progress = dynamic_cast<CGUIDialogProgress*>(
      CServiceBroker::GetGUI()->GetWindowManager().GetWindow(WINDOW_DIALOG_PROGRESS));
  int progressAnim = 0;
  if (progress)
  {
    progress->SetHeading(CVariant{"Restoring from backup..."});
    progress->SetLine(0, CVariant{"Copying files..."});
    progress->SetPercentage(0);
    progress->Open();
  }

  // Copy all files to a temp directory (no conflicts with running Kodi)
  std::string tmpPath = "special://home/userdata_restore_tmp/";
  D("copying to " + tmpPath);
#if defined(TARGET_DARWIN_TVOS)
  // Drop any leftover xml keys from a previous restore attempt before copying
  CTVOSNSUserDefaults::DeleteKeysWithPrefix("/userdata_restore_tmp/", true);
#endif
  XFILE::CDirectory::RemoveRecursive(tmpPath);
  XFILE::CDirectory::Create(tmpPath);
  SMBConfig restoreConfig = config;
  restoreConfig.sharepath = fullPath;
  bool copyOk = CopyDirectory(restoreConfig, "", tmpPath, diag, progress, &progressAnim);
  D("CopyDirectory completed, copyOk=" + std::to_string(copyOk));

  // Copy guisettings.xml to temp (skipped during bulk copy) and fix it
  if (progress)
    progress->SetLine(0, CVariant{"Applying tvOS settings fix..."});

  {
    std::string guiFile = tmpPath + "guisettings.xml";
    std::string srcUrl = StringUtils::Format("smb://{}:{}@{}/{}/guisettings.xml",
                                              config.username, config.password, config.host,
                                              fullPath);

    D("Copying guisettings.xml from SMB");
    bool guiOk = XFILE::CFile::Copy(srcUrl, guiFile);
    D("  first attempt: " + std::to_string(guiOk));
    if (!guiOk)
    {
      guiOk = XFILE::CFile::Copy(srcUrl, guiFile);
      D("  retry attempt: " + std::to_string(guiOk));
    }

    if (guiOk)
    {
      XFILE::CFile file;
      if (file.Open(guiFile, XFILE::READ_NO_CACHE))
      {
        std::string xml;
        char buf[4096];
        ssize_t n;
        while ((n = file.Read(buf, sizeof(buf))) > 0)
          xml.append(buf, n);
        file.Close();

        auto removeLine = [&xml](const std::string& pattern)
        {
          size_t pos = xml.find(pattern);
          if (pos != std::string::npos)
          {
            size_t lineEnd = xml.find('\n', pos);
            if (lineEnd != std::string::npos)
              xml.erase(pos, lineEnd - pos + 1);
          }
        };
        removeLine("videoscreen.screenmode");
        removeLine("videoscreen.resolution");
        removeLine("videoscreen.screen");
        removeLine("videoscreen.fakefullscreen");
        removeLine("audiooutput.audiodevice");
        removeLine("audiooutput.passthroughdevice");
        removeLine("audiooutput.channels");
        removeLine("audiooutput.config");

        size_t pos = xml.find("<settings version=\"2\">");
        if (pos != std::string::npos)
        {
          pos = xml.find('>', pos) + 1;
          std::string expert = "\n    <setting id=\"settings.level\">Expert</setting>";
          xml.insert(pos, expert);
        }

        XFILE::CFile outFile;
        if (outFile.OpenForWrite(guiFile, true))
        {
          outFile.Write(xml.c_str(), xml.size());
          outFile.Close();
        }
      }
    }
  }

  if (progress)
    progress->Close();

  if (!copyOk)
  {
    D("FAILED: CopyDirectory reported errors");
    XFILE::CDirectory::RemoveRecursive(tmpPath);
    HELPERS::ShowOKDialogText(CVariant{"Restore"}, CVariant{"Some files failed to copy"});
    return;
  }

  // Validate copied data has required userdata markers before destructive swap
  D("validating copied data...");
  {
    bool hasDb = XFILE::CDirectory::Exists(URIUtils::AddFileToFolder(tmpPath, "Database"),
                                           false);
    // On tvOS guisettings.xml is vectored into NSUserDefaults (not a physical
    // file), so CFile::Exists (which checks the key via CTVOSFile) is the
    // reliable probe. It falls back to a real file on other platforms.
    bool hasGui = XFILE::CFile::Exists(URIUtils::AddFileToFolder(tmpPath, "guisettings.xml"),
                                       false);
    D("validation: Database=" + std::to_string(hasDb) + " guisettings=" + std::to_string(hasGui));
    if (!hasDb || !hasGui)
    {
      D("FAILED: copied data missing required userdata markers");
      XFILE::CDirectory::RemoveRecursive(tmpPath);
      HELPERS::ShowOKDialogText(
          CVariant{"Restore"},
          CVariant{"The copied data is missing required userdata files "
                   "(Database/, guisettings.xml).\n\n"
                   "The selected folder may not be a valid backup."});
      return;
    }
  }

  // Capture files marked "keep" from the current userdata before the swap so a
  // failed restore never loses them. Mirrors adblink's restore-protect list.
  D("capturing protected files (" + std::to_string(config.protect.size()) + " checked)");
  auto captured = CaptureProtectedFiles(config.protect, diag);

  // Rollback-safe atomic swap
  D("swapping temp -> userdata");
  std::string realUserdata = CSpecialProtocol::TranslatePath("special://home/userdata/");
  std::string realTmp = CSpecialProtocol::TranslatePath("special://home/userdata_restore_tmp/");
  D("  real userdata: " + realUserdata);
  D("  real tmp: " + realTmp);

  // Move current userdata aside before replacing (don't delete until new one is in place)
  std::string backupUserdata = realUserdata;
  URIUtils::RemoveSlashAtEnd(backupUserdata);
  backupUserdata += ".restorebak";

  XFILE::CDirectory::RemoveRecursive(backupUserdata); // clean previous .restorebak if any
  bool moved = (std::rename(realUserdata.c_str(), backupUserdata.c_str()) == 0);
  if (!moved)
  {
    D("FAILED: could not rename userdata to .restorebak (directory may be in use)");
    HELPERS::ShowOKDialogText(
        CVariant{"Restore"},
        CVariant{"Could not replace userdata — the directory is in use.\n\n"
                 "Please close any file operations and try again."});
    return;
  }
  D("  renamed userdata -> userdata.restorebak");

  bool renamed = (std::rename(realTmp.c_str(), realUserdata.c_str()) == 0);
  if (!renamed)
  {
    D("FAILED: rename tmp -> userdata failed, rolling back");
    std::rename(backupUserdata.c_str(), realUserdata.c_str());
    HELPERS::ShowOKDialogText(CVariant{"Restore"},
                              CVariant{"Could not replace userdata directory."});
    return;
  }
  D("  renamed tmp -> userdata");

#if defined(TARGET_DARWIN_TVOS)
  // tvOS vectors *.xml into NSUserDefaults keyed by their userdata path, while
  // everything else lives as real files in Caches. The physical renames above
  // leave the keyed xml untouched, so:
  //  1. drop the settings we are replacing (/userdata/*),
  //  2. move xml written into the temp dir keys to their final /userdata/* keys
  //     (e.g. guisettings.xml, which was copied via XFILE::CFile -> CTVOSFile),
  //  3. vector any restored *.xml that only exists as a physical file into
  //     NSUserDefaults so the restore survives a Caches purge.
  D("syncing tvOS xml persistence");
  CTVOSNSUserDefaults::DeleteKeysWithPrefix("/userdata/", false);
  CTVOSNSUserDefaults::MoveKeysWithPrefix("/userdata_restore_tmp/", "/userdata/", true);
  SyncTVOSXmlPersistence(realUserdata, diag);
  D("tvOS xml persistence synced");
#endif

  // Restore the captured "keep" files over the restored data. On tvOS these
  // writes route through CTVOSFile into NSUserDefaults; on other platforms they
  // are plain file writes. Done after the swap + tvOS persistence sync so the
  // protected content wins over whatever the backup contained.
  if (!captured.empty())
  {
    D("restoring protected files (" + std::to_string(captured.size()) + ")");
    RestoreProtectedFiles(captured, diag);
  }

  // Clean up the old backup
  XFILE::CDirectory::RemoveRecursive(backupUserdata);
  D("  removed .restorebak");

  // Write full diag
  {
    XFILE::CFile f;
    if (f.OpenForWrite("special://home/restore_diag.txt", true))
    {
      f.Write(diag.c_str(), diag.size());
      f.Close();
    }
  }

  D("=== Restore completed successfully, restarting ===");
  CServiceBroker::GetAppMessenger()->PostMsg(TMSG_QUIT);
}

bool CGUIDialogRestore::CopyDirectory(const SMBConfig& config, const std::string& subPath,
                                       const std::string& destPath, std::string& diag,
                                       CGUIDialogProgress* progress,
                                       int* progressAnim)
{
  auto D = [&diag](const std::string& msg) { diag += msg + "\n"; };

  std::string url = StringUtils::Format("smb://{}/{}/{}", config.host, config.sharepath, subPath);
  CFileItemList items;
  if (!XFILE::CDirectory::GetDirectory(url, items, "", XFILE::DIR_FLAG_DEFAULTS))
  {
    std::string urlWithCreds = StringUtils::Format("smb://{}:{}@{}/{}/{}", config.username,
                                                     config.password, config.host, config.sharepath,
                                                     subPath);
    if (!XFILE::CDirectory::GetDirectory(urlWithCreds, items, "", XFILE::DIR_FLAG_DEFAULTS))
    {
      D("  FAILED to list: " + subPath);
      return false;
    }
  }

  D("  CopyDirectory: " + subPath + " (" + std::to_string(items.Size()) + " items)");
  XFILE::CDirectory::Create(destPath);

  for (int i = 0; i < items.Size(); i++)
  {
    const auto& item = items[i];
    std::string itemPath = item->GetPath();
    URIUtils::RemoveSlashAtEnd(itemPath);
    std::string name = URIUtils::GetFileName(itemPath);
    if (name == "." || name == ".." || name == ".DS_Store")
      continue;

    if (item->IsFolder())
    {
      if (!CopyDirectory(config, subPath + "/" + name,
                         URIUtils::AddFileToFolder(destPath, name), diag, progress, progressAnim))
        return false;
    }
    else
    {
      if (name == "guisettings.xml")
        continue;

      if (progress)
      {
        progress->SetLine(0, CVariant{"Copying " + name});
        if (progressAnim)
        {
          *progressAnim = (*progressAnim + 3) % 100;
          progress->SetPercentage(*progressAnim);
        }
        progress->Progress();
      }

      std::string smbPath = subPath;
      if (!smbPath.empty() && smbPath.front() == '/')
        smbPath = smbPath.substr(1);
      std::string smbFullPath = smbPath.empty() ? name : smbPath + "/" + name;
      std::string fileUrl = StringUtils::Format("smb://{}:{}@{}/{}/{}", config.username,
                                                  config.password, config.host, config.sharepath,
                                                  smbFullPath);
      std::string destFile = URIUtils::AddFileToFolder(destPath, name);
      std::string realDest = CSpecialProtocol::TranslatePath(destFile);
      D("  copy: " + name + " src=" + fileUrl);

      bool manualOk = false;
      XFILE::CFile srcFile;
      if (srcFile.Open(fileUrl, XFILE::READ_NO_CACHE))
      {
        FILE* dstFile = fopen(realDest.c_str(), "wb");
        if (dstFile)
        {
          char buf[65536];
          ssize_t n;
          while ((n = srcFile.Read(buf, sizeof(buf))) > 0)
          {
            if (fwrite(buf, 1, n, dstFile) != static_cast<size_t>(n))
              break;
          }
          manualOk = !ferror(dstFile);
          fclose(dstFile);
        }
        srcFile.Close();
      }
      if (!manualOk)
        D("  FAILED: " + name);
    }
  }
  return true;
}

bool CGUIDialogRestore::UploadDirectory(const std::string& localPath, const std::string& smbBase,
                                         const SMBConfig& config, std::string& diag,
                                         CGUIDialogProgress* progress, int* progressAnim)
{
  auto D = [&diag](const std::string& msg) { diag += msg + "\n"; };
  D("  UploadDirectory: " + localPath);

  CFileItemList items;
  if (!XFILE::CDirectory::GetDirectory(localPath, items, "", XFILE::DIR_FLAG_DEFAULTS))
  {
    D("  FAILED to list: " + localPath);
    return false;
  }

  XFILE::CDirectory::Create(smbBase);

  for (int i = 0; i < items.Size(); i++)
  {
    const auto& item = items[i];
    std::string itemPath = item->GetPath();
    URIUtils::RemoveSlashAtEnd(itemPath);
    std::string name = URIUtils::GetFileName(itemPath);
    if (name == "." || name == ".." || name == ".DS_Store")
      continue;

    if (item->IsFolder())
    {
      std::string subSmb = URIUtils::AddFileToFolder(smbBase, name);
      if (!UploadDirectory(itemPath, subSmb, config, diag, progress, progressAnim))
        return false;
    }
    else
    {
      if (progress)
      {
        progress->SetLine(0, CVariant{"Uploading " + name});
        if (progressAnim)
        {
          *progressAnim = (*progressAnim + 3) % 100;
          progress->SetPercentage(*progressAnim);
        }
        progress->Progress();
      }

      std::string destUrl = URIUtils::AddFileToFolder(smbBase, name);
      if (!XFILE::CFile::Copy(itemPath, destUrl))
        D("  failed to upload " + name);
    }
  }
  return true;
}

#if defined(TARGET_DARWIN_TVOS)
void CGUIDialogRestore::SyncTVOSXmlPersistence(const std::string& realDir, std::string& diag)
{
  auto D = [&diag](const std::string& msg) { diag += msg + "\n"; };

  CFileItemList items;
  if (!XFILE::CDirectory::GetDirectory(realDir, items, "", XFILE::DIR_FLAG_DEFAULTS))
    return;

  for (int i = 0; i < items.Size(); i++)
  {
    const auto& item = items[i];
    std::string itemPath = item->GetPath();
    URIUtils::RemoveSlashAtEnd(itemPath);
    std::string name = URIUtils::GetFileName(itemPath);

    if (item->IsFolder())
    {
      if (name != "." && name != ".." && name != "Database" && name != "Thumbnails")
        SyncTVOSXmlPersistence(itemPath, diag);
      continue;
    }

    if (!StringUtils::EqualsNoCase(URIUtils::GetExtension(itemPath), ".xml"))
      continue;

    // Skip xml that already has an NSUserDefaults key (e.g. guisettings.xml
    // moved over from the temp dir above).
    if (CTVOSNSUserDefaults::KeyFromPathExists(itemPath))
      continue;

    FILE* f = fopen(itemPath.c_str(), "rb");
    if (!f)
      continue;

    std::string data;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
      data.append(buf, n);
    fclose(f);

    if (data.empty())
      continue;

    if (CTVOSNSUserDefaults::SetKeyDataFromPath(itemPath, data.data(), data.size(), true))
    {
      // Remove the physical copy so CTVOSDirectory doesn't list it twice.
      std::remove(itemPath.c_str());
      D("  vectored " + name + " -> NSUserDefaults");
    }
    else
    {
      D("  FAILED to vector " + name + " -> NSUserDefaults");
    }
  }
}
#endif
