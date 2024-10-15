#ifndef XJ_APP_DATABASE_H
#define XJ_APP_DATABASE_H

#include "database.h"

using namespace std;

class AppDatabase : public Database
{
public:
	AppDatabase();
	~AppDatabase();
public:
	struct DefectData
	{
		 DefectData() : hhmmss(""), prod(""), prod_no(""), lot(""), board("") { defects.clear(); };
		 DefectData(const string& _hhmmss, const string& _prod, const string& _prod_no, const string& _lot, const string& _board) 
		 	: hhmmss(_hhmmss), prod(_prod), prod_no(_prod_no), lot(_lot), board(_board) { defects.clear(); };
		 std::string hhmmss;         //date
		 std::string prod;           //current product name
		 std::string lot;            //lot number
		 std::string board;          //board id
		 std::string prod_no;        //current product count
		 map<string, int> defects;   //defect list
		 friend std::ostream &operator<<(std::ostream &os, const struct DefectData &t)
		 {
			 os << std::endl << "hhmmss: " << t.hhmmss << ", prod: " << t.prod << ", prod_no: " << t.prod_no << ", lot: " << t.lot << ", board: " << t.board ;
			 for (const auto &itr : t.defects)
			 {
				 os << ", " << itr.first << ": " << itr.second;
			 }
			 return os;
		 }
	 };
	// 上传到mes的数据结构
	struct MesData
	{
		MesData() : hhmmss(""), prod(""), lot(""), board(""), prod_no(""), plc_code("")
			, total(""), total_defect(""),shift_type("") { defects.clear(); };
		MesData(const string& _hhmmss, const string& _prod, const string& _lot, const string& _board, const string& _prod_no, const string& _plc_code
			, const string& _total, const string& _total_defect)
			: hhmmss(_hhmmss), prod(_prod), lot(_lot), board(_board), prod_no(_prod_no), plc_code(_plc_code), total(_total), total_defect(_total_defect){ defects.clear();};
		string hhmmss;                 // date
		std::string prod;              // current product name
		std::string lot;               // lot number
		std::string board;             // board id
		std::string prod_no;           // current product count
		string plc_code = "F205-T";    // 编号(目前固定)
		map<string, int> defects;      // 瑕疵类别
		std::string total;             // 当前生产总数
		std::string total_defect;      // 当前NG数
		string shift_type;             // 班次

		void getShiftTypeFromDate()
		{
			try 
			{
				if (hhmmss.size() != 19)
					return;
				const int hour = stoi(hhmmss.substr(11, 2));
				shift_type = "day";
				if (hour < 7 || hour > 19)
				{
					shift_type = "night";
				}
			}
			catch(...)
			{
				return;
			}
		}
	};
private:
	/**
	* @brief make column name and value sequence, create column "defect2, defect6, ..." and number "?7, ?8, ..."
	* @param start <input> start index, eg. when start = 7, start from "?7"
	* @param size <input> sequence lenght
	* @param return gen sequence
	*/
	std::string getBindedValueSequence(const int start, const int size);
	std::string reportInsertColumns(const std::map<std::string, int> &value);
	std::string reportSelectColumns();
	void bindEachDefectToReport(sqlite3_stmt *stmt, const int start, const map<string, int> &value);
public:
/**
 * @brief add a defect record to the database
 * @param time date
 * @param prod current product name
 * @param board current board id
 * @param lot lot number
 * @param prod_no product count
 * @param defects key:defect name, value: defect number
 * @return database operation result (if ok return 0)
 */
int defectAdd(const string& time, const string& prod, const string& board, const string& lot, int prod_no, const map<string, int> &defects);
/**
 * @brief add a record to the database
 * @param defectData struct of defect infos
 * @return database operation result (if ok return 0)
 */
int defectAdd(const DefectData& defectData);
/**
 * @brief read all data between datetime range
 * @param rpv matching records
 * @param startTime start time of query
 * @param endTime end time of query
 * @param prod product name of query
 * @param board board index of query
 * @param lot lot number of query
 * @return database operation result (if ok return 0)
 */
int defectRead(vector<DefectData>& rpv, const string& startTime, const string& endTime, const string& prod, const string& board, const string& lot);
/**
 * @brief delete data before a specific number of days
 * @param days specific number of days
 * @return database operation result
 */
int defectDeleteDaysAgo(const int days);
/**
 * @brief delete data before endTime
 * @param endTime Data before endTime will be deleted
 * @return database operation result (if ok return 0)
 */
int defectDelete(const string& endTime);

/*************** MES *********************/
/**
 * @brief add a mes record to the database
 * @param time date
 * @param prod current product name
 * @param board current board id
 * @param lot lot number
 * @param prod_no product count
 * @param total total number
 * @param total_defect total defect number
 * @param plc_code plc code
 * @param shift_type day or night
 * @param defects key:defect name, value: defect number
 * @return database operation result (if ok return 0)
 */
int mesAdd(const string& time, const string& prod, const string& board, const string& lot, int prod_no, int total, 
		int total_defect, const string& plc_code, const string& shift_type, const map<string, int> &defects);
/**
 * @brief add a mes record to the database
 * @param mesData struct of mes infos
 * @return database operation result (if ok return 0)
 */
int mesAdd(MesData& mesData);
/**
 * @brief read all data between datetime range
 * @param rpv matching records
 * @param startTime start time of query
 * @param endTime end time of query
 * @param startProdNo start product index of query
 * @param endProdNo end product index of query
 * @param prod product name of query
 * @param board board index of query
 * @param lot lot number of query
 * @return database operation result (if ok return 0)
 */
int mesRead(vector<MesData>& rpv, const string& startTime, const string& endTime, 
		const string& startProdNo, const string& endProdNo, const string& prod, const string& board, const string& lot);
};

#endif
