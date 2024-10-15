#include <mutex>
#include "xj_app_json_config.h"

AppJsonConfig* AppJsonConfig::m_instance = nullptr;
std::mutex AppJsonConfig::m_mutex;

AppJsonConfig &AppJsonConfig::instance()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if(m_instance == nullptr)
    {
        m_instance = new AppJsonConfig();
    }
    return *m_instance;
}

AppJsonConfig::AppJsonConfig():m_sJsonPath(""), m_bIsLoaded(false)
{

}

AppJsonConfig::~AppJsonConfig()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if(m_instance)
    {
        delete m_instance;
        m_instance = nullptr;
    }
}

void AppJsonConfig::loadJson()
{	
	try
	{
		if (m_sJsonPath.empty())
		{
			LogERROR << "The path name of the json file is empty!";
			return;
		}
		std::ifstream file(m_sJsonPath, std::ios_base::in);
		if (!file.is_open())
		{
			LogERROR << "Failed to open file with path name: " << m_sJsonPath <<  ", please check if the file exists!";
			return;
		}
		std::unique_lock<std::mutex> lock(m_mutex);
		read_json(file, m_root); // 将json文件读入根节点
		file.close();
		
		m_bIsLoaded = true;
	}
	catch (boost::property_tree::ptree_bad_path const &e)
	{
		LogERROR << e.what();
		throw;
	}
	catch (std::exception const &e)
	{
		LogERROR << "unexpected error, " << e.what();
		throw;
	}
}


void AppJsonConfig::saveJson()
{
	try
	{
		if (m_sJsonPath.empty())
		{
			LogERROR << "The path name of the json file is empty!";
			return;
		}
		
		std::unique_lock<std::mutex> lock(m_mutex);
		std::ofstream ofs(m_sJsonPath, std::ios_base::out);
		write_json(ofs, m_root);
		ofs.close();
	}
	catch (boost::property_tree::ptree_bad_path const &e)
	{
		LogERROR << e.what();
		throw;
	}
	catch (std::exception const &e)
	{
		LogERROR << "unexpected error, " << e.what();
		throw;
	}
}
