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

#include <chrono>
#include <cstdio>

using namespace KODI::MESSAGING;

static const std::string CONFIG_PATH = "special://home/smb_restore.json";

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
      if (!LoadConfig(config))
      {
        config.host = "192.168.1.39";
        config.username = "jeff";
        config.password = "xky91234";
        config.sharepath = "storage/kodi";
      }
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
  return true;
}

bool CGUIDialogRestore::SaveConfig(const SMBConfig& config)
{
  CVariant variant(CVariant::VariantTypeObject);
  variant["host"] = config.host;
  variant["username"] = config.username;
  variant["password"] = config.password;
  variant["sharepath"] = config.sharepath;

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

  // List temp directory contents for diagnostics
  D("listing temp dir contents:");
  {
    CFileItemList tmpItems;
    XFILE::CDirectory::GetDirectory(tmpPath, tmpItems, "", XFILE::DIR_FLAG_DEFAULTS);
    for (int i = 0; i < tmpItems.Size(); i++)
    {
      std::string p = tmpItems[i]->GetPath();
      URIUtils::RemoveSlashAtEnd(p);
      D("  " + URIUtils::GetFileName(p));
    }
  }

  // Atomic swap: delete userdata, rename temp to userdata
  D("swapping temp -> userdata");
  std::string realHome = CSpecialProtocol::TranslatePath("special://home/");
  std::string realUserdata = CSpecialProtocol::TranslatePath("special://home/userdata/");
  std::string realTmp = CSpecialProtocol::TranslatePath("special://home/userdata_restore_tmp/");
  D("  real home: " + realHome);
  D("  real userdata: " + realUserdata);
  D("  real tmp: " + realTmp);

  XFILE::CDirectory::RemoveRecursive(realUserdata);
  bool renamed = (std::rename(realTmp.c_str(), realUserdata.c_str()) == 0);
  D("  rename result: " + std::to_string(renamed));

  if (progress)
    progress->Close();

  if (!copyOk)
  {
    D("FAILED: CopyDirectory reported errors");
    HELPERS::ShowOKDialogText(CVariant{"Restore"}, CVariant{"Some files failed to copy"});
    return;
  }

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
