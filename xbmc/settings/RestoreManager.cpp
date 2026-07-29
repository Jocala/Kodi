#include "RestoreManager.h"

#include "FileItem.h"
#include "FileItemList.h"
#include "ServiceBroker.h"
#include "Util.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "dialogs/GUIDialogYesNo.h"
#include "filesystem/Directory.h"
#include "filesystem/File.h"
#include "guilib/GUIKeyboardFactory.h"
#include "messaging/ApplicationMessenger.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "utils/JSONVariantParser.h"
#include "utils/JSONVariantWriter.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "utils/Variant.h"
#include "utils/log.h"

using namespace KODI::MESSAGING;

bool CRestoreManager::Restore()
{
  RestoreConfig config;

  if (!PromptForConfig(config))
    return false;

  if (!SaveConfig(config))
  {
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Error, "Restore",
                                          "Failed to save config");
    return false;
  }

  CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Info, "Restore",
                                        "Copying files...");

  if (!CopyFromSMB(config))
  {
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Error, "Restore",
                                          "Failed to copy files");
    return false;
  }

  CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Info, "Restore",
                                        "Done. Please restart Kodi.");

  CServiceBroker::GetAppMessenger()->PostMsg(TMSG_QUIT);
  return true;
}

bool CRestoreManager::PromptForConfig(RestoreConfig& config)
{
  LoadConfig(config);

  if (!CGUIKeyboardFactory::ShowAndGetInput(config.host, CVariant{"SMB host"}, false))
    return false;
  if (!CGUIKeyboardFactory::ShowAndGetInput(config.share, CVariant{"Share path"}, false))
    return false;
  if (!CGUIKeyboardFactory::ShowAndGetInput(config.username, CVariant{"Username"}, false))
    return false;
  if (!CGUIKeyboardFactory::ShowAndGetInput(config.password, CVariant{"Password"}, false, true))
    return false;

  return CGUIDialogYesNo::ShowAndGetInput(CVariant{"Restore"},
                                          CVariant{"Overwrite data and restart?"});
}

bool CRestoreManager::LoadConfig(RestoreConfig& config)
{
  XFILE::CFile file;
  if (!file.Open("special://masterprofile/smb_restore.json", XFILE::READ_NO_CACHE))
    return false;

  std::string json;
  char buf[4096];
  ssize_t n;
  while ((n = file.Read(buf, sizeof(buf))) > 0)
    json.append(buf, n);
  file.Close();

  CVariant variant;
  CJSONVariantParser::Parse(json, variant);

  if (variant.isObject())
  {
    if (variant["host"].isString())
      config.host = variant["host"].asString();
    if (variant["share"].isString())
      config.share = variant["share"].asString();
    if (variant["username"].isString())
      config.username = variant["username"].asString();
    if (variant["password"].isString())
      config.password = variant["password"].asString();
  }
  return true;
}

bool CRestoreManager::SaveConfig(const RestoreConfig& config)
{
  CVariant variant(CVariant::VariantTypeObject);
  variant["host"] = config.host;
  variant["share"] = config.share;
  variant["username"] = config.username;
  variant["password"] = config.password;

  std::string json;
  if (!CJSONVariantWriter::Write(variant, json, true))
    return false;

  XFILE::CFile file;
  if (!file.OpenForWrite("special://masterprofile/smb_restore.json", true))
    return false;

  ssize_t written = file.Write(json.c_str(), json.size());
  file.Close();
  return written == static_cast<ssize_t>(json.size());
}

bool CRestoreManager::CopyFromSMB(const RestoreConfig& config)
{
  std::string smbUrl = StringUtils::Format("smb://{}:{}@{}/{}", config.username, config.password,
                                            config.host, config.share);
  std::string destPath = "special://home/userdata/";

  // Clear destination first
  XFILE::CDirectory::RemoveRecursive(destPath);
  XFILE::CDirectory::Create(destPath);

  return CopyDirectory(smbUrl, destPath);
}

bool CRestoreManager::CopyDirectory(const std::string& sourcePath, const std::string& destPath)
{
  CFileItemList items;
  if (!XFILE::CDirectory::GetDirectory(sourcePath, items, "", XFILE::DIR_FLAG_DEFAULTS))
  {
    CLog::Log(LOGERROR, "RestoreManager: failed to list: {}", sourcePath);
    return false;
  }

  CLog::Log(LOGINFO, "RestoreManager: {} items in {}", items.Size(), sourcePath);

  XFILE::CDirectory::Create(destPath);

  bool allOk = true;
  for (int i = 0; i < items.Size(); i++)
  {
    const auto& item = items[i];
    std::string name = URIUtils::GetFileName(item->GetPath());

    if (name == "." || name == ".." || name == ".DS_Store")
      continue;

    if (item->IsFolder())
    {
      if (!CopyDirectory(URIUtils::AddFileToFolder(sourcePath, name),
                         URIUtils::AddFileToFolder(destPath, name)))
        allOk = false;
    }
    else
    {
      std::string destFile = URIUtils::AddFileToFolder(destPath, name);
      CLog::Log(LOGINFO, "RestoreManager: copying {}", name);
      if (!XFILE::CFile::Copy(item->GetPath(), destFile))
      {
        CLog::Log(LOGERROR, "RestoreManager: failed to copy {}", item->GetPath());
        allOk = false;
      }
    }
  }

  return allOk;
}
