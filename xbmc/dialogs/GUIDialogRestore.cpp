#include "GUIDialogRestore.h"

#include "FileItem.h"
#include "FileItemList.h"
#include "PasswordManager.h"
#include "ServiceBroker.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "filesystem/Directory.h"
#include "filesystem/File.h"
#include "guilib/GUIComponent.h"
#include "guilib/GUIEditControl.h"
#include "guilib/GUIMessage.h"
#include "guilib/GUIWindowManager.h"
#include "messaging/ApplicationMessenger.h"
#include "utils/JSONVariantParser.h"
#include "utils/JSONVariantWriter.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "utils/Variant.h"
#include "utils/log.h"

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
        config.sharepath = "storage/userdata";
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
          readEdit(CONTROL_EDIT_PASSWORD), readEdit(CONTROL_EDIT_SHAREPATH)};
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

void CGUIDialogRestore::OnRestore()
{
  SMBConfig config = ReadFields();

  if (config.host.empty() || config.username.empty() || config.password.empty() ||
      config.sharepath.empty())
  {
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Error, "Restore",
                                          "All fields required");
    return;
  }

  SaveConfig(config);
  Close();

  CLog::Log(LOGINFO, "=== RestoreManager: Starting restore ===");
  CLog::Log(LOGINFO, "RestoreManager: host={} user={} share={}", config.host, config.username,
            config.sharepath);

  // Save credentials to Kodi's password manager
  {
    CURL pwUrl("smb://" + config.host + "/" + config.sharepath);
    pwUrl.SetUserName(config.username);
    pwUrl.SetPassword(config.password);
    CPasswordManager::GetInstance().SaveAuthenticatedURL(pwUrl, true);
    CLog::Log(LOGINFO, "RestoreManager: saved credentials to password manager");
  }

  // Test connection
  std::string smbUrlBase =
      StringUtils::Format("smb://{}/{}", config.host, config.sharepath);
  CLog::Log(LOGINFO, "RestoreManager: listing directory: {}", smbUrlBase);

  CFileItemList items;
  bool listOk = XFILE::CDirectory::GetDirectory(smbUrlBase, items, "", XFILE::DIR_FLAG_DEFAULTS);
  CLog::Log(LOGINFO, "RestoreManager: GetDirectory returned {} with {} items", listOk,
            items.Size());

  if (!listOk)
  {
    CLog::Log(LOGERROR, "RestoreManager: GetDirectory FAILED for: {}", smbUrlBase);
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Error, "Restore",
                                          "Cannot list SMB share");
    return;
  }

  if (items.Size() == 0)
  {
    CLog::Log(LOGWARNING, "RestoreManager: SMB share has 0 items");
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Warning, "Restore",
                                          "SMB share is empty");
    return;
  }

  for (int i = 0; i < items.Size(); i++)
  {
    const auto& item = items[i];
    CLog::Log(LOGINFO, "RestoreManager:   item[{}]: path={} isFolder={}", i, item->GetPath(),
              item->IsFolder());
  }

  // Clear destination
  std::string destPath = "special://home/userdata/";
  CLog::Log(LOGINFO, "RestoreManager: clearing destination: {}", destPath);
  XFILE::CDirectory::RemoveRecursive(destPath);
  XFILE::CDirectory::Create(destPath);

  // Do the full copy
  CLog::Log(LOGINFO, "=== RestoreManager: Starting file copy ===");
  if (!CopyDirectory(config, "", destPath))
  {
    CLog::Log(LOGERROR, "RestoreManager: CopyDirectory failed");
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Error, "Restore",
                                          "Failed to copy files");
    return;
  }

  // Fix guisettings.xml - strip tvOS-incompatible video settings
  {
    CLog::Log(LOGINFO, "RestoreManager: fixing video settings in guisettings.xml");
    std::string guiFile = destPath + "guisettings.xml";
    XFILE::CFile file;
    if (file.Open(guiFile, XFILE::READ_NO_CACHE))
    {
      std::string xml;
      char buf[4096];
      ssize_t n;
      while ((n = file.Read(buf, sizeof(buf))) > 0)
        xml.append(buf, n);
      file.Close();

      // Remove WINDOW screenmode, resolution, and screen index
      // These are desktop settings that break fullscreen on tvOS
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
      // Remove macOS audio device settings incompatible with tvOS
      removeLine("audiooutput.audiodevice");
      removeLine("audiooutput.passthroughdevice");
      removeLine("audiooutput.channels");
      removeLine("audiooutput.config");

      // Set Expert settings level so advanced audio/video options are visible
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
        CLog::Log(LOGINFO, "RestoreManager: guisettings.xml video settings fixed");
      }
    }
  }

  CLog::Log(LOGINFO, "=== RestoreManager: Copy complete, quitting ===");
  CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Info, "Restore",
                                        "Done. Quitting.");
  CServiceBroker::GetAppMessenger()->PostMsg(TMSG_QUIT);
}

bool CGUIDialogRestore::CopyDirectory(const SMBConfig& config, const std::string& subPath,
                                       const std::string& destPath)
{
  std::string url = StringUtils::Format("smb://{}/{}/{}", config.host, config.sharepath, subPath);
  CLog::Log(LOGINFO, "RestoreManager: CopyDirectory listing: {}", url);

  CFileItemList items;
  if (!XFILE::CDirectory::GetDirectory(url, items, "", XFILE::DIR_FLAG_DEFAULTS))
  {
    CLog::Log(LOGERROR, "RestoreManager: CopyDirectory GetDirectory FAILED: {}", url);
    return false;
  }

  CLog::Log(LOGINFO, "RestoreManager: CopyDirectory found {} items in {}", items.Size(), subPath);
  XFILE::CDirectory::Create(destPath);

  for (int i = 0; i < items.Size(); i++)
  {
    const auto& item = items[i];
    // GetFileName returns empty for paths ending with /, so strip it first
    std::string itemPath = item->GetPath();
    URIUtils::RemoveSlashAtEnd(itemPath);
    std::string name = URIUtils::GetFileName(itemPath);
    if (name == "." || name == ".." || name == ".DS_Store")
      continue;

    if (item->IsFolder())
    {
      CLog::Log(LOGINFO, "RestoreManager:   entering folder: {}", name);
      if (!CopyDirectory(config, subPath + "/" + name,
                         URIUtils::AddFileToFolder(destPath, name)))
        return false;
    }
    else
    {
      std::string fileUrl = StringUtils::Format("smb://{}:{}@{}/{}/{}", config.username,
                                                 config.password, config.host, config.sharepath,
                                                 subPath + "/" + name);
      std::string destFile = URIUtils::AddFileToFolder(destPath, name);
      CLog::Log(LOGINFO, "RestoreManager:   copying file: {}", name);
      CLog::Log(LOGINFO, "RestoreManager:     from: {}", fileUrl);
      CLog::Log(LOGINFO, "RestoreManager:     to:   {}", destFile);

      bool ok = XFILE::CFile::Copy(fileUrl, destFile);
      CLog::Log(LOGINFO, "RestoreManager:     result: {}", ok);

      if (!ok)
      {
        CLog::Log(LOGERROR, "RestoreManager:   FAILED to copy: {}", name);
        return false;
      }
    }
  }
  return true;
}
