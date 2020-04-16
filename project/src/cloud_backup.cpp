#include <thread>
#include "cloud_backup.hpp"

_cloud_sys::DataManager data_manage(DATA_FILE);

void compress_test(char *argv[])
{	//argv[1] = 源文件名称
	//argv[2] = 压缩包名称
	
	_cloud_sys::CompressUtil::Compress(argv[1],argv[2]);
	std::string file = argv[2];
	file += ".txt";
	_cloud_sys::CompressUtil::UnCompress(argv[2], file.c_str());
}

void data_test()
{
	_cloud_sys::DataManager data_manage("./test.txt");
	data_manage.InitLoad();
	data_manage.Insert("c.txt", "c.txt.gz");
	std::vector<std::string> list;
	//获取所有信息测试
	data_manage.GetAllName(&list);
	for(auto i : list)
	{
		printf("%s\n", i.c_str());
	}
	printf("------------------\n");
	//获取未压缩的文件名称测试
	list.clear();
	data_manage.NonCompressList(&list);
	for(auto i : list)
	{
		printf("%s\n", i.c_str());	
	}
	/*
	data_manage.Insert("a.txt","a.txt");
	data_manage.Insert("b.txt","b.txt.gz");
	data_manage.Insert("c.txt","c.txt");
	data_manage.Insert("d.txt","d.txt.gz");
	data_manage.Storage();//持久化存储
	*/
}

void m_non_compress()
{
	_cloud_sys::NonHotCompress ncom(GZFILE_DIR, BACKUP_DIR);
	ncom.Start();
	return;
}

void thr_http_server()
{
	_cloud_sys::Server srv;
	srv.Start();
	return;
}

int main(int argc,char *argv[])
{
	//文件备份路径不存在，则创建
	if(boost::filesystem::exists(GZFILE_DIR) == false)
	{
		boost::filesystem::create_directory(GZFILE_DIR);
	}
	
	//压缩包存放路径不存在，则创建
	if(boost::filesystem::exists(BACKUP_DIR) == false)
	{
		boost::filesystem::create_directory(BACKUP_DIR);
	}
	 //_cloud_sys::data_manage.Insert("my.txt.gz.txt","my.txt.gz.txt");
	std::thread thr_compress(m_non_compress);//c++11中的线程--启动非热点文件压缩模块
	std::thread thr_server(thr_http_server);//网络通信服务端模块启动
	thr_compress.join();//等待线程退出
	thr_server.join();
	return 0;
}
