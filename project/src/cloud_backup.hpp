#include <sys/types.h>
#include <sys/stat.h>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>
#include <fstream>
#include <unordered_map>
#include <zlib.h>
#include <pthread.h>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include "httplib.h"


#define NONHOT_TIME 10 //最后一次访问时间在10秒以类为热点文件
#define INTERVAL_TIME 30 //非热点的检测每30秒一次
#define BACKUP_DIR "./backup/" //文件的备份路径
#define GZFILE_DIR "./gzfile/" //压缩后的文件存放的路径
#define DATA_FILE "./list.backup" //数据管理模块的数据备份文件名称

namespace _cloud_sys
{
	class FileUtil
	{
		public:
			//从文件中读取所有内容
			static bool Read(const std::string &name, std::string *body)
			{
				//一定要注意以二进制方式打开文件
				//输入文件流
				std::ifstream fs(name, std::ios::binary);
				if(fs.is_open() == false)
				{
					std::cout<<"open file"<< name <<" failed\n";
					return false;
				}
				//boost::filesystem::file_size()获取文件大小
				int64_t fsize = boost::filesystem::file_size(name);
				//给body申请空间接收文件数据
				body -> resize(fsize);
				//ifstream.read(char *, int);
				//body是个指针，需要先解引用
				fs.read(&(*body)[0],fsize);
				//判断上一次操作是否正确
				if(fs.good() == false)
				{
					std::cout<< "file" << name << "read data failed!\n";
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
				if(ofs.is_open() == false)
				{
					std::cout<< "open file" << name << " failed\n";
				}
				ofs.write(&body[0], body.size());
				if(ofs.good() == false)
				{
					std::cout<< "file" << name << " write data failed!\n";
				}
				ofs.close();
				return true;
			}
	};
	class CompressUtil
	{
		public:
			//文件压缩-原文件名称-压缩包名称
			static bool Compress(const std::string &src, const std::string &dst)
			{
				std::string body;
				FileUtil::Read(src, &body);
				//打开压缩包
				gzFile gf = gzopen(dst.c_str(), "wb");
				if(gf  == NULL)
				{
					std::cout<< "open file" << dst << "failed!\n";
					return false;
				}
				int wlen = 0;
				while(wlen < body.size())
				{
					//防止body中的数据没有一次性压缩成功i
					//若一次没有将全部数据压缩，则从未压缩的数据开始继续压缩
					int ret = gzwrite(gf, &body[wlen], body.size() - wlen);
					if(ret == 0)
					{
						std::cout<< "file" << dst << " write compress data failed!\n";
						return false;
					}
					wlen += ret;
				}
				gzclose(gf);
				return true;
			}
			//文件解压缩-压缩包名称-原文件名称
			static bool UnCompress(const std::string &src, const std::string &dst)
			{
				std::ofstream ofs(dst, std::ios::binary);
				if(ofs.is_open() == false)
				{
					std::cout<< "open file" << dst << "failed!\n";
					return false;
				}
				gzFile gf = gzopen(src.c_str(), "rb");
				if(gf == NULL)
				{
					std::cout<< "open file"<< src << " failed!\n";
					ofs.close();
					return false;
				}
				int ret;
				char tmp[4096] = {0};
				//gzread(句柄， 缓冲区， 缓冲区大小)
				//返回实际读取到的解压后的数据大小
				while((ret = gzread(gf, tmp, 4096)) > 0)
				{
					ofs.write(tmp, ret);
				}
				ofs.close();
				gzclose(gf);
				return true;
			}
	};
	class DataManager
	{
		public:
			DataManager(const std::string &path):_back_file(path)
			{
				pthread_rwlock_init(&_rwlock, NULL);
			}
			~DataManager()
			{
				pthread_rwlock_destroy(&_rwlock);
			}
			//判断文件是否存在
			bool Exists(const std::string &name)
			{
				//看是否能够从_file_list找到这个信息
				pthread_rwlock_rdlock(&_rwlock);
				auto it = _file_list.find(name);
				if(it == _file_list.end())
				{
					pthread_rwlock_unlock(&_rwlock);
					return false;
				}
				return true;
			}	
			//判断文件是否已经压缩
			bool IsCompress(const std::string &name)
			{
				//管理的数据：源文件名称-压缩包名称
				//文件上传后，源文件名称和压缩包名称一致
				//文件压缩后，将压缩包名称更新为具体的包名
				pthread_rwlock_rdlock(&_rwlock);
				auto it = _file_list.find(name);
				if(it == _file_list.end())
				{
					pthread_rwlock_unlock(&_rwlock);
					return false;
				}
				if(it -> first == it -> second)
				{
					pthread_rwlock_unlock(&_rwlock);
					//两个名称一致，则表示未压缩
					return false;
				}
				pthread_rwlock_unlock(&_rwlock);
				return true;
			}
			//获取未压缩文件列表
			bool NonCompressList(std::vector<std::string>*list)
			{
				//遍历_file_list；将没有压缩的文件名称添加到list中
				pthread_rwlock_rdlock(&_rwlock);
				auto it = _file_list.begin();
				for( ; it != _file_list.end();++it)
				{
					if(it -> first == it -> second)
					{
						list -> push_back(it -> first);
					}
				}
				pthread_rwlock_unlock(&_rwlock);
				return true;
			}
			//插入/更新数据
			bool Insert(const std::string &src, const std::string &dst)
			{
				//更新修改需要加写锁
				pthread_rwlock_wrlock(&_rwlock);
				_file_list[src] = dst;
				pthread_rwlock_unlock(&_rwlock);
				//更新修改之后，重新备份
				Storage();
				return true;
			}
			//获取所有文件名称
			//向外展示文件列表使用
			bool GetAllName(std::vector<std::string>*list)
			{
				pthread_rwlock_rdlock(&_rwlock);
				auto it = _file_list.begin();
				for( ; it != _file_list.end(); ++it)
				{
					//获取的是源文件名称
					list -> push_back(it -> first);
				}
				pthread_rwlock_unlock(&_rwlock);
				return true;
			}
			//根据源文件名称，获取压缩包名称
			bool GetGzName(const std::string &src, std::string *dst)
			{
				auto it = _file_list.find(src);
				if(it == _file_list.end())
				{
					return false;
				}
				*dst = it -> second;
				return true;
			}
			//数据改变后持久化存储--存储的是管理的文件名数据
			bool Storage()
			{
				//将_file_list中的数据进行持久化存储
				//数据对象进行持久化存储--序列化
				//src dst\r\n
				//实例化一个string流对象
				std::stringstream tmp;
				pthread_rwlock_rdlock(&_rwlock);
				auto it = _file_list.begin();
				for( ; it != _file_list.end(); ++it)
				{
					//序列化--按照指定个数组织数据
					tmp << it -> first <<" "<< it -> second << "\r\n";
				}
				pthread_rwlock_unlock(&_rwlock);
				//每次都是清空写入
				//将数据备份到文件中
				FileUtil::Write(_back_file,tmp.str());
				return true;
			}
			//启动时初始化加载原有数据
			//filename gzfilename\r\n filename gzfilename\r\n...
			bool InitLoad()
			{
				//从数据的持久化存储文件中加载数据
				//1.将这个备份文件的数据读取出来
				std::string body;
				if(FileUtil::Read(_back_file, &body) == false)
				{
					return false;
				}
				//2.进行字符串处理，按照\r\n进行分割
				//boost::split(vector, src, sep, flag)
				std::vector<std::string> list;
				boost::split(list, body, boost::is_any_of("\r\n"), boost::token_compress_off);
				//3.每一行按照空格进行分割，前边是key，后边是val
				for(auto i : list)
				{
					size_t pos = i.find(" ");
					if(pos == std::string::npos)
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
			std::string _back_file;//持久化数据存储文件名称
			std::unordered_map<std::string,std::string> _file_list;//数据管理容器
			pthread_rwlock_t _rwlock;
	};
	_cloud_sys::DataManager data_manage(DATA_FILE);
	class NonHotCompress
	{
		public:
			NonHotCompress(const std::string gz_dir, const std::string bu_dir):
				_gz_dir(gz_dir), _bu_dir(bu_dir)
			{
				
			}
			//总体向外提供的功能接口，开始压缩模块
			bool Start()
			{
			//是一个循环的，持续的过程，每隔一段时间，判断有没有非热点文件，然后进行压缩
			//文件是非热点文件--当前时间减去最后一次访问时间 > n秒(基准值)
				while(1)
				{
				//1.获取一下所有的未压缩的文件列表
				std::vector<std::string> list;
				data_manage.NonCompressList(&list);
				//2.逐个判断这个文件是否是热点文件
				for(int i = 0; i < list.size(); i++)
				{
					//判断是否是热点文件
					bool ret = FileIsHot(list[i]);
					if(ret == false)
					{
						std::cout << "non hot file" << list[i] << "\n";
						//非热点文件则组织源文件的路径名称以及压缩包的路径名称
						//进行压缩存储
						//纯源文件名称
						std::string s_filename = list[i];
						//纯压缩包名称
						std::string d_filename = list[i] + ".gz";
						//源文件路径名称
						std::string src_name = _bu_dir + s_filename;
						std::string dst_name = _gz_dir + d_filename;
						//3.如果是非热点文件，则压缩这个文件，删除源文件
						if(CompressUtil::Compress(src_name, dst_name) == true)
						{
							//更新数据信息
							data_manage.Insert(s_filename, d_filename);
							//删除源文件
							unlink(src_name.c_str());
						}
					}
				}
				//4.休眠一会
				sleep(INTERVAL_TIME);

				}

			}
		private:
			//判断一个文件是否是一个热点文件
			//非热点文件--当前时间减去最后一次访问时间 > n(基准值)
			bool FileIsHot(const std::string &name)
			{
				//获取当前时间
				time_t cur_t = time(NULL);
				struct stat st;
				if(stat(name.c_str(), &st) < 0)
				{
					std::cout<< "get file" << name << "stat failed!\n";
					return false;
				}
				if((cur_t - st.st_atime) > NONHOT_TIME)
				{
					//非热点返回false
					return false;
				}
				//NONHOT_TIME以内都是热点文件
				return true;
			}
		private:
			std::string _bu_dir;//压缩前文件的所在路径
			std::string _gz_dir;//压缩后的文件存储路径
	};
	class Server
	{
		public:
			Server(){
			}
			~Server(){
			}
			bool Start()//启动网络通信模块接口
			{
				_server.Put("/(.*)",Upload);
				_server.Get("/list",List);
				//正则表达式 .*代表任意字符串  ()表示捕捉这个字符串
				//为了避免有文件名交list与list请求混淆
				_server.Get("/download/(.*)",Download);
				//搭建tcp服务器，进行http数据接收处理
				_server.listen("0.0.0.0",9000);
				return true;
			}
		private:
			//文件上传处理回调函数
			static void Upload(const httplib::Request &req, httplib::Response &rsp)
			{
				//req.method-解析出的请求方法
				//req.path--解析出的请求的资源路径
				//req.headers--这是一个头部信息键值对
				//req.body--存放请求数据的正文
				//纯文件名称
				std::string filename = req.matches[1];
				//组织文件路径名，文件备份在指定路径
				std::string pathname = BACKUP_DIR + filename;
				//向文件写入数据，文件不存在会创建
				FileUtil::Write(pathname, req.body);
				//添加文件信息到数据管理模块
				data_manage.Insert(filename, filename);
				rsp.status = 200;
				return;
			}
			//文件列表处理回调函数
			static void List(const httplib::Request &req, httplib::Response &rsp)
			{

				std::vector<std::string> list;
				data_manage.GetAllName(&list);
				std::stringstream tmp;
				tmp << "<html><body><hr />";
				for(int i = 0; i < list.size();i++)
				{
					tmp << "<a href = '/download/" << list[i] << "'>" << list[i] << "</a>";
					tmp << "<hr />";
					//tmp << "<a href = '/download/a.txt'> a.txt </a>";
				}
				tmp << "<hr /></body></html>";

				//http响应格式：首行(协议版本，状态码，描述)，头部，空行，正文
				//set_content(正文数据，数据大小，正文类型)
				rsp.set_content(tmp.str().c_str(), tmp.str().size(), "text/html");
				rsp.status = 200;
				return;
			}
			//文件下载处理回调函数
			static void Download(const httplib::Request &req, httplib::Response &rsp)
			{
				//1.从数据模块中判断文件是否存在
				//这就是前边路由注册时捕捉的(.*)
				std::string filename = req.matches[1];
				if (data_manage.Exists(filename) == false)
				{
					//文件不存在则page not found
					rsp.status = 404;
					return;
				}
				//2.判断文件是否已经压缩，压缩了则先要解压缩，然后再读取文件数据
				//源文件的备份路径名	
				std::string pathname = BACKUP_DIR + filename;
				if (data_manage.IsCompress(filename) == true)
				{
					//文件被压缩，先将文件解压出来
					std::string gzfile;
					//获取压缩包名称
					data_manage.GetGzName(filename, &gzfile);
					//组织一个压缩包的路径名
					std::string gzpathname = GZFILE_DIR + gzfile;
					//将压缩包解压
					CompressUtil::UnCompress(gzpathname, pathname);
					//删除压缩包
					unlink(gzpathname.c_str());
					//更新数据信息
					data_manage.Insert(filename, filename);
				}
				//从文件中读取数据，响应客户端
				//直接将文件数据读取到rsp的body中
				FileUtil::Read(pathname, &rsp.body);
				//二进制流文件下载
				rsp.set_header("Content_Type","application/octet-stream");
				rsp.status = 200;
				return;
			}
		private:
			std::string _file_dir;//文件上传备份路径
			httplib::Server _server;
	};
}
