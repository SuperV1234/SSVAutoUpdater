// Copyright (c) 2013 Vittorio Romeo
// License: Academic Free License ("AFL") v. 3.0
// AFL License page: http://opensource.org/licenses/AFL-3.0

#include <string>
#include <vector>
#include <fstream>
#include <SFML/Network.hpp>
#include <SSVUtils/SSVUtils.hpp>
#include <SSVUtilsJson/SSVUtilsJson.hpp>

using namespace std;
using namespace sf;
using namespace ssvu;
using namespace ssvu::Encryption;
using namespace ssvu::FileSystem;
using namespace ssvuj;

struct Target
{
	ssvu::FileSystem::Path remoteFolder, localFolder;
	inline Target(const ssvu::FileSystem::Path& mRemoteFolder, const ssvu::FileSystem::Path& mLocalFolder)
		: remoteFolder{mRemoteFolder}, localFolder{mLocalFolder} { }
};
struct DownloadData { bool localExists; ssvu::FileSystem::Path remotePath, localPath; std::string remoteMD5, localMD5; };

std::string backupFolder{"_BACKUP/"}, host, hostMainFolder, hostMainConfig, hostMainScript, remoteDataFolder;
std::vector<ssvu::FileSystem::Path> remoteExcludedFiles, remoteExcludedFolders, remoteOnlyNewFiles;
std::vector<Target> targets;
std::vector<DownloadData> downloadDatas;

std::vector<ssvu::FileSystem::Path> getFolderNames(const ssvu::FileSystem::Path& mPath)
{
	return ssvu::FileSystem::getScan<ssvu::FileSystem::Mode::Recurse, ssvu::FileSystem::Type::Folder>(mPath);
}

std::string downloadFileContents(const ssvu::FileSystem::Path& mRemotePath)
{
	ssvu::lo("downloadFileContents") << "Getting <" + mRemotePath.getStr() + "> from server..." << std::endl;
	sf::Http::Response response{sf::Http(host).sendRequest(mRemotePath.getStr())};
	if(response.getStatus() == sf::Http::Response::Ok)
	{
		ssvu::lo("downloadFileContents") << "<" + mRemotePath.getStr() + "> got successfully" << std::endl;
		return response.getBody();
	}
	ssvu::lo("downloadFileContents") << "Get <" + mRemotePath.getStr() + "> error" << std::endl;
	return "";
}

void download(const DownloadData& mDownloadData)
{
	ssvu::lo("download") << "Processing <" + mDownloadData.localPath + ">";

	if(mDownloadData.localExists)
	{
		ssvu::lo("download") << "Backing up <" + mDownloadData.localPath + "> to <" + backupFolder + mDownloadData.localPath + ">" << std::endl;
		if(!ssvu::FileSystem::exists(backupFolder)) ssvu::FileSystem::createFolder(backupFolder);
		for(const auto& f : getFolderNames(mDownloadData.localPath)) if(!ssvu::FileSystem::exists(backupFolder + f)) ssvu::FileSystem::createFolder(backupFolder + f);
		std::ofstream ofs{backupFolder + mDownloadData.localPath, std::ofstream::binary};
		ofs << ssvu::FileSystem::getFileContents(mDownloadData.localPath); ofs.flush(); ofs.close();
	}

	for(auto& f : getFolderNames(mDownloadData.localPath)) if(!ssvu::FileSystem::exists(f)) { ssvu::lo("download") << "Creating folder <" + f + ">"; ssvu::FileSystem::createFolder(f); }

	std::ofstream ofs{mDownloadData.localPath, std::ofstream::binary};
	ofs << downloadFileContents(hostMainFolder + mDownloadData.remotePath); ofs.flush(); ofs.close();
	ssvu::lo("download") << "Finished processing <" + mDownloadData.localPath + ">"<< std::endl;
}

void loadLocalConfig()
{
	ssvu::lo("loadLocalConfig") << "loading local config...\n" << std::endl;

	const Obj localConfig{ssvuj::getFromFile("updaterConfig.json")};
	host			= ssvuj::getAs<std::string>(localConfig, "host"); ssvu::lo("loadLocalConfig") << "host: <" + host + ">" << std::endl;
	hostMainFolder	= ssvuj::getAs<std::string>(localConfig, "hostMainFolder"); ssvu::lo("loadLocalConfig") << "hostMainFolder: <" + hostMainFolder + ">" << std::endl;
	hostMainConfig	= ssvuj::getAs<std::string>(localConfig, "hostMainConfig"); ssvu::lo("loadLocalConfig") << "hostMainConfig: <" + hostMainConfig + ">" << std::endl;
	hostMainScript	= ssvuj::getAs<std::string>(localConfig, "hostMainScript"); ssvu::lo("loadLocalConfig") << "hostMainScript: <" + hostMainScript + ">" << std::endl;

	for(const auto& t : localConfig["targets"])
	{
		auto remoteFolder(ssvuj::getAs<string>(t, "remoteFolder")), localFolder(ssvuj::getAs<string>(t, "localFolder"));
		targets.emplace_back(remoteFolder, localFolder);
		ssvu::lo("loadLocalConfig") << "target: <" + remoteFolder + "> -> <" + localFolder + ">" << std::endl;
	}
}

void loadRemoteConfig()
{
	ssvu::lo("loadRemoteConfig") << "loading remote config...\n" << std::endl;

	const Obj remoteConfig{ssvuj::getFromString(downloadFileContents(hostMainFolder + hostMainConfig))};
	remoteDataFolder = ssvuj::getAs<std::string>(remoteConfig, "dataFolder"); ssvu::lo("loadRemoteConfig") << "remoteDataFolder" + remoteDataFolder << std::endl;
	for(const auto& f : ssvuj::getAs<std::vector<std::string>>(remoteConfig, "excludedFiles"))
	{
		remoteExcludedFiles.push_back(f);
		ssvu::lo("loadRemoteConfig") << "remoteExcludedFile: <" + f + ">" << std::endl;
	}
	for(const auto& f : ssvuj::getAs<std::vector<std::string>>(remoteConfig, "excludedFolders"))
	{
		remoteExcludedFolders.push_back(f);
		ssvu::lo("loadRemoteConfig") << "remoteExcludedFolder: <" + f + ">" << std::endl;
	}
	for(const auto& f : ssvuj::getAs<std::vector<std::string>>(remoteConfig, "onlyNewFiles"))
	{
		remoteOnlyNewFiles.push_back(f);
		ssvu::lo("loadRemoteConfig") << "remoteOnlyNewFile: <" + f + ">" << std::endl;
	}
}

void loadRemoteScript()
{
	ssvu::lo("loadRemoteScript") << "loading remote script..." << std::endl;

	const Obj remoteScriptResult{ssvuj::getFromString(downloadFileContents(hostMainFolder + hostMainScript))};

	for(auto& f : remoteScriptResult)
	{
		auto remotePath(ssvuj::getAs<std::string>(f, "path")), remoteMD5(ssvuj::getAs<std::string>(f, "md5"));
		ssvu::lo("loadRemoteScript") << "remoteFiles: <" + remotePath + "> <" + remoteMD5 + ">" << std::endl;

		ssvu::FileSystem::Path localPath{ssvu::getReplaced(remotePath, remoteDataFolder, "")};
		std::string localMD5;

		for(const auto& t : targets) localPath = Path(ssvu::getReplaced(localPath.getStr(), t.remoteFolder.getStr(), t.localFolder.getStr()));
		bool localExists{ssvu::FileSystem::exists(localPath)};

		if(localPath.existsAsFolder()) continue;
		if(localExists)
		{
			localMD5 = ssvu::Encryption::encrypt<ssvu::Encryption::Type::MD5>(ssvu::FileSystem::getFileContents(localPath));
			ssvu::lo("loadRemoteScript") << "localFiles: <" + localPath + "> <" + localMD5 + ">" << std::endl;
		}
		else ssvu::lo("loadRemoteScript") << "localFiles: <" + localPath + "> <does not exist>" << std::endl;

		downloadDatas.push_back({localExists, remotePath, remoteMD5, localPath, localMD5});
	}
}

void processDownloads()
{
	ssvu::lo("processDownloads") << "processing downloads..." << std::endl;

	for(const auto& d : downloadDatas)
	{
		if(d.localExists && d.localMD5 == d.remoteMD5) ssvu::lo("processDownloads") << "no need to update: <" + d.localPath + ">" << std::endl;
		else if(d.localExists && ssvu::contains(remoteOnlyNewFiles, d.remotePath)) ssvu::lo("processDownloads") << "<" + d.localPath + "> doesn't match, but won't be downloaded because it exists" << std::endl;
		else download(d);
	}
}

int main() { loadLocalConfig(); loadRemoteConfig(); loadRemoteScript(); processDownloads(); saveLogToFile("updaterLog.txt"); return 0; }
