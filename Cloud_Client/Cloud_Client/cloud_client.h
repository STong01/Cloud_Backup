#pragma once
#define  _SCL_SECURE_NO_WARNINGS
#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>//split头文件
#include "httplib.h"

#define STORE_FILE "./list.backup"
#define LISTEN_DIR "./backup/"

class FileUtil
{
public:
	//从文件中读取所有内容
	static bool Read(const std::string &name, std::string *body)
	{
		//一定要注意以二进制方式打开文件
		//输入文件流
		std::ifstream fs(name, std::ios::binary);
		if (fs.is_open() == false)
		{
			std::cout << "open file" << name << " failed\n";
			return false;
		}
		//boost::filesystem::file_size()获取文件大小
		int64_t fsize = boost::filesystem::file_size(name);
		//给body申请空间接收文件数据
		body->resize(fsize);
		//ifstream.read(char *, int);
		//body是个指针，需要先解引用
		fs.read(&(*body)[0], fsize);
		//判断上一次操作是否正确
		if (fs.good() == false)
		{
			std::cout << "file" << name << "read data failed!\n";
			return false;
		}
		fs.close();
		return true;
	}

	//向文件中写入数据 
	static bool Write(const std::string &name, const std::string &body)
	{
		//输出流--ofstream默认打开文件的时候会清空原有内容
		//当前策略是覆盖写入
		std::ofstream ofs(name, std::ios::binary);
		if (ofs.is_open() == false)
		{
			std::cout << "open file" << name << " failed\n";
		}
		ofs.write(&body[0], body.size());
		if (ofs.good() == false)
		{
			std::cout << "file" << name << " write data failed!\n";
		}
		ofs.close();
		return true;
	}
};

//数据管理模块
class DataManager
{
public:
	DataManager(const std::string &filename) :_store_file(filename)
	{}

	//插入/更新数据
	bool Insert(const std::string &key, const std::string &val)
	{
		_backup_list[key] = val;
		return true;
	}

	//通过文件名获取原有etag信息
	bool GetEtag(const std::string &key, std::string *val)
	{
		auto it = _backup_list.find(key);
		if (it == _backup_list.end())
		{
			return false;
		}
		*val = it->second;
		return true;
	}

	//持久化存储
	bool Storage()
	{
		//将_backup_list中的数据进行持久化存储
		//数据对象进行持久化存储--序列化
		//filename etag\r\n
		//实例化一个string流对象
		std::stringstream tmp;
		auto it = _backup_list.begin();
		for (; it != _backup_list.end(); ++it)
		{
			//序列化--按照指定个数组织数据
			tmp << it->first << " " << it->second << "\r\n";
		}
		//将数据备份到文件中
		FileUtil::Write(_store_file, tmp.str());
		return true;
	}

	//初始化加载原有数据
	bool InitLoad()
	{
		//从数据的持久化存储文件中加载数据
		//1.将这个备份文件的数据读取出来
		std::string body;
		if (FileUtil::Read(_store_file, &body) == false)
		{
			return false;
		}
		//2.进行字符串处理，按照\r\n进行分割
		//boost::split(vector, src, sep, flag)
		std::vector<std::string> list;
		boost::split(list, body, boost::is_any_of("\r\n"), boost::token_compress_off);
		//3.每一行按照空格进行分割，前边是key，后边是val
		for (auto i : list)
		{
			size_t pos = i.find(" ");
			if (pos == std::string::npos)
			{
				continue;
			}
			std::string key = i.substr(0, pos);
			std::string val = i.substr(pos + 1);
			//4.将key/val添加到_file_list中
			Insert(key, val);
		}
		return true;
	}
private:
	//持久化存储文件名称
	std::string _store_file;
	//备份的文件信息
	std::unordered_map<std::string, std::string> _backup_list;
};

//DataManager data_manage(STORE_FILE);

class CloudClient
{
public:
	CloudClient(const std::string &filename, const std::string &store_file, const std::string &srv_ip, uint16_t srv_port):
		_listen_dir(filename), data_manage(store_file), _srv_ip(srv_ip), _srv_port(srv_port)
	{}

	bool Start()
	{
		//加载以前备份的信息
		data_manage.InitLoad();
		while (1)
		{
			std::vector<std::string>list;
			//获取到所有的需要备份的文件名陈
			GetBackupFileList(&list);
			for (int i = 0; i < list.size(); i++)
			{
				std::string name = list[i];
				std::string pathname = _listen_dir + name;
				std::cout << pathname << "is need to backup\n";
				//读取文件数据，作为请求正文
				std::string body;
				FileUtil::Read(pathname, &body);
				//实例化Client对象准备发起HTTP上传文件请求
				httplib::Client client(_srv_ip.c_str(), _srv_port);
				std::string req_path = "/" + name;
				auto rsp = client.Put(req_path.c_str(), body, "application/octet-stream");
				if (rsp == NULL || (rsp != NULL && rsp->status != 200))
				{
					//这个文件上传备份失败了
					std::cout << pathname << "backup failed\n";
					continue;
				}
				std::string etag;
				GetEtag(pathname, &etag);
				//备份成功则插入/更新信息
				data_manage.Insert(name, etag);
				std::cout << pathname << "backup success\n";
			}
			//休眠1000毫秒后，重新检测
			Sleep(1000);
		}
		return true;
	}

	//获取需要备份的文件列表
	bool GetBackupFileList(std::vector<std::string> *list)
	{
		if (boost::filesystem::exists(_listen_dir) == false)
		{
			//若目录不存在，则创建
			boost::filesystem::create_directory(_listen_dir);
		}
		//1.进行目录监控，获取指定目录下所有文件名称
		boost::filesystem::directory_iterator begin(_listen_dir);
		boost::filesystem::directory_iterator end;
		for (; begin != end; ++begin)
		{
			if (boost::filesystem::is_directory(begin->status()))
			{
				//目录是不需要进行备份的
				//当前我们并不做多层级目录备份,遇到目录直接越过
				continue;
			}
			std::string pathname = begin->path().string();
			std::string name = begin->path().filename().string();
			//2.逐个文件计算自身当前etag
			std::string cur_etag;
			GetEtag(pathname, &cur_etag);
			//3.获取已经备份过的etag信息
			std::string old_etag;
			data_manage.GetEtag(name, &old_etag);
			//4.与data_manage中保存的原有etag进行比对
			//  1.没有找到原有etag--新文件需要备份
			//  2.找到原有etag，但是当前etag和原有etag不想等，需要备份
			//  3.找到原有etag，并且与当前etag相等，则不需要备份
			if (cur_etag != old_etag)
			{
				//当前etag与原有etag不同，则需要备份
				list->push_back(name);
			}
		}

		return true;
	}

	//计算文件的etag信息
	bool GetEtag(const std::string &pathname, std::string *etag)
	{
		//计算一个etag:文件大小-文件最后一次修改时间
		int64_t fsize = boost::filesystem::file_size(pathname);//文件大小
		time_t mtime = boost::filesystem::last_write_time(pathname);//最后一次修改时间
		*etag = std::to_string(fsize) + "-" + std::to_string(mtime);
		return true;
	}
private:
	std::string _srv_ip;
	uint16_t _srv_port;
	//监控的目录名称
	std::string _listen_dir;
	DataManager data_manage;
};