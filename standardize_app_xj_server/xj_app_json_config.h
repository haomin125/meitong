#ifndef XJ_APP_JSON_CONFIG_H
#define XJ_APP_JSON_CONFIG_H

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <vector>
#include <sstream>

#include "logger.h"

class AppJsonConfig
{
public:
	static AppJsonConfig &instance();

	template <typename T>
	T get(const std::string &nodeName)
	{
		// you need to try ... catch (const std::exception &e), if anything is wrong
		try
		{
			loadJson();
			return m_root.get<T>(nodeName);
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
	};

	template <typename T>
	std::vector<T> getVector(const std::string &nodeName)
	{
		// you need to try ... catch (const std::exception &e), if anything is wrong
		try
		{
			std::vector<T> value{};

			loadJson();
			for (const auto &itr : m_root.get_child(nodeName))
			{
				value.emplace_back(itr.second.get_value<T>());
			}
			return value;
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
	};

	template <typename T>
	void set(const std::string &nodeName, const T& value)
	{
		// you need to try ... catch (const std::exception &e), if anything is wrong
		try
		{
			loadJson();
			m_root.put(nodeName, value);
			saveJson();
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
	};

	// This method is used to load json configuration from use defined file path
	void setJsonPath(const std::string &path) { m_sJsonPath = path; };
	std::string setJsonPath(const std::string &path) const { return m_sJsonPath; };

private:
	AppJsonConfig();
	virtual ~AppJsonConfig();
	void loadJson();
	void saveJson();

	static std::mutex m_mutex;
	static AppJsonConfig *m_instance;
	boost::property_tree::ptree m_root;

	std::string m_sJsonPath;
	bool m_bIsLoaded;
};

#endif
