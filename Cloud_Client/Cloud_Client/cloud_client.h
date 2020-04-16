#pragma once
#define  _SCL_SECURE_NO_WARNINGS
#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>//splitͷ�ļ�
#include "httplib.h"

#define STORE_FILE "./list.backup"
#define LISTEN_DIR "./backup/"

class FileUtil
{
public:
	//���ļ��ж�ȡ��������
	static bool Read(const std::string &name, std::string *body)
	{
		//һ��Ҫע���Զ����Ʒ�ʽ���ļ�
		//�����ļ���
		std::ifstream fs(name, std::ios::binary);
		if (fs.is_open() == false)
		{
			std::cout << "open file" << name << " failed\n";
			return false;
		}
		//boost::filesystem::file_size()��ȡ�ļ���С
		int64_t fsize = boost::filesystem::file_size(name);
		//��body����ռ�����ļ�����
		body->resize(fsize);
		//ifstream.read(char *, int);
		//body�Ǹ�ָ�룬��Ҫ�Ƚ�����
		fs.read(&(*body)[0], fsize);
		//�ж���һ�β����Ƿ���ȷ
		if (fs.good() == false)
		{
			std::cout << "file" << name << "read data failed!\n";
			return false;
		}
		fs.close();
		return true;
	}

	//���ļ���д������ 
	static bool Write(const std::string &name, const std::string &body)
	{
		//�����--ofstreamĬ�ϴ��ļ���ʱ������ԭ������
		//��ǰ�����Ǹ���д��
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

//���ݹ���ģ��
class DataManager
{
public:
	DataManager(const std::string &filename) :_store_file(filename)
	{}

	//����/��������
	bool Insert(const std::string &key, const std::string &val)
	{
		_backup_list[key] = val;
		return true;
	}

	//ͨ���ļ�����ȡԭ��etag��Ϣ
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

	//�־û��洢
	bool Storage()
	{
		//��_backup_list�е����ݽ��г־û��洢
		//���ݶ�����г־û��洢--���л�
		//filename etag\r\n
		//ʵ����һ��string������
		std::stringstream tmp;
		auto it = _backup_list.begin();
		for (; it != _backup_list.end(); ++it)
		{
			//���л�--����ָ��������֯����
			tmp << it->first << " " << it->second << "\r\n";
		}
		//�����ݱ��ݵ��ļ���
		FileUtil::Write(_store_file, tmp.str());
		return true;
	}

	//��ʼ������ԭ������
	bool InitLoad()
	{
		//�����ݵĳ־û��洢�ļ��м�������
		//1.����������ļ������ݶ�ȡ����
		std::string body;
		if (FileUtil::Read(_store_file, &body) == false)
		{
			return false;
		}
		//2.�����ַ�����������\r\n���зָ�
		//boost::split(vector, src, sep, flag)
		std::vector<std::string> list;
		boost::split(list, body, boost::is_any_of("\r\n"), boost::token_compress_off);
		//3.ÿһ�а��տո���зָǰ����key�������val
		for (auto i : list)
		{
			size_t pos = i.find(" ");
			if (pos == std::string::npos)
			{
				continue;
			}
			std::string key = i.substr(0, pos);
			std::string val = i.substr(pos + 1);
			//4.��key/val��ӵ�_file_list��
			Insert(key, val);
		}
		return true;
	}
private:
	//�־û��洢�ļ�����
	std::string _store_file;
	//���ݵ��ļ���Ϣ
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
		//������ǰ���ݵ���Ϣ
		data_manage.InitLoad();
		while (1)
		{
			std::vector<std::string>list;
			//��ȡ�����е���Ҫ���ݵ��ļ�����
			GetBackupFileList(&list);
			for (int i = 0; i < list.size(); i++)
			{
				std::string name = list[i];
				std::string pathname = _listen_dir + name;
				std::cout << pathname << "is need to backup\n";
				//��ȡ�ļ����ݣ���Ϊ��������
				std::string body;
				FileUtil::Read(pathname, &body);
				//ʵ����Client����׼������HTTP�ϴ��ļ�����
				httplib::Client client(_srv_ip.c_str(), _srv_port);
				std::string req_path = "/" + name;
				auto rsp = client.Put(req_path.c_str(), body, "application/octet-stream");
				if (rsp == NULL || (rsp != NULL && rsp->status != 200))
				{
					//����ļ��ϴ�����ʧ����
					std::cout << pathname << "backup failed\n";
					continue;
				}
				std::string etag;
				GetEtag(pathname, &etag);
				//���ݳɹ������/������Ϣ
				data_manage.Insert(name, etag);
				std::cout << pathname << "backup success\n";
			}
			//����1000��������¼��
			Sleep(1000);
		}
		return true;
	}

	//��ȡ��Ҫ���ݵ��ļ��б�
	bool GetBackupFileList(std::vector<std::string> *list)
	{
		if (boost::filesystem::exists(_listen_dir) == false)
		{
			//��Ŀ¼�����ڣ��򴴽�
			boost::filesystem::create_directory(_listen_dir);
		}
		//1.����Ŀ¼��أ���ȡָ��Ŀ¼�������ļ�����
		boost::filesystem::directory_iterator begin(_listen_dir);
		boost::filesystem::directory_iterator end;
		for (; begin != end; ++begin)
		{
			if (boost::filesystem::is_directory(begin->status()))
			{
				//Ŀ¼�ǲ���Ҫ���б��ݵ�
				//��ǰ���ǲ�������㼶Ŀ¼����,����Ŀ¼ֱ��Խ��
				continue;
			}
			std::string pathname = begin->path().string();
			std::string name = begin->path().filename().string();
			//2.����ļ���������ǰetag
			std::string cur_etag;
			GetEtag(pathname, &cur_etag);
			//3.��ȡ�Ѿ����ݹ���etag��Ϣ
			std::string old_etag;
			data_manage.GetEtag(name, &old_etag);
			//4.��data_manage�б����ԭ��etag���бȶ�
			//  1.û���ҵ�ԭ��etag--���ļ���Ҫ����
			//  2.�ҵ�ԭ��etag�����ǵ�ǰetag��ԭ��etag����ȣ���Ҫ����
			//  3.�ҵ�ԭ��etag�������뵱ǰetag��ȣ�����Ҫ����
			if (cur_etag != old_etag)
			{
				//��ǰetag��ԭ��etag��ͬ������Ҫ����
				list->push_back(name);
			}
		}

		return true;
	}

	//�����ļ���etag��Ϣ
	bool GetEtag(const std::string &pathname, std::string *etag)
	{
		//����һ��etag:�ļ���С-�ļ����һ���޸�ʱ��
		int64_t fsize = boost::filesystem::file_size(pathname);//�ļ���С
		time_t mtime = boost::filesystem::last_write_time(pathname);//���һ���޸�ʱ��
		*etag = std::to_string(fsize) + "-" + std::to_string(mtime);
		return true;
	}
private:
	std::string _srv_ip;
	uint16_t _srv_port;
	//��ص�Ŀ¼����
	std::string _listen_dir;
	DataManager data_manage;
};