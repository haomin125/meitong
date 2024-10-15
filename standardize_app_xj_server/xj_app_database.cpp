#include <sstream>
#include <ostream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <string>
#include "xj_app_database.h"
#include "logger.h"
#include "shared_utils.h"

using namespace std;

enum class DefectCols
{
	TIME = 0,
	PROD,
	BOARD,
	LOT,
	PROD_NO,
	DEFECT_START
};

enum class MesCols
{
	TIME = 0,
	PROD,
	BOARD,
	LOT,
	PROD_NO,
	TOTAL,
	TOTAL_DEFECT,
	PLC_CODE,
	SHIFT_TYPE,
	DEFECT_START
};

AppDatabase::AppDatabase() : Database()
{

}

AppDatabase::~AppDatabase()
{

}

int AppDatabase::defectDeleteDaysAgo(const int days)
{
  LogINFO << "Defect delete " << days << " days ago.";
  string startTime = shared_utils::getTime(0);
  string endTime = shared_utils::getTime(-24 * days);
  return defectDelete(endTime);
}

int AppDatabase::defectDelete(const string& endTime)
{
  if (!DbOpened())
  {
    LogERROR << "Database is not opened.";
    return 1;
  }

  LogINFO << "Defect delete to " << endTime;

  unique_lock<mutex> lock(m_databaseMutex);

  sqlite3_stmt *stmt;
  string sql = "delete from defect where time <=?1";
  sqlite3_prepare_v2(GetDB(), sql.c_str(), -1, &stmt, NULL);

  sqlite3_bind_text(stmt, 1, endTime.c_str(), -1, SQLITE_STATIC);

  if (sqlite3_step(stmt) != SQLITE_DONE)
  {
    LogERROR << "ERROR deleting defect: " << sqlite3_errmsg(GetDB());
  }

  return sqlite3_finalize(stmt);
}

string AppDatabase::reportInsertColumns(const map<string, int> &value)
{
	string output = "";
	for (const auto &itr : value)
	{
		output += itr.first + ", ";
	}

	return value.empty() ? "" : output.substr(0, output.length() - 2);
}

string AppDatabase::reportSelectColumns()
{
	string output = "";
	for (int idx = 2; idx < ClassifierResult::getNameSize() - 1; idx++)
	{
		//output += "IFNULL(sum(defect" + to_string(idx - 1) + "), 0)";
		output += "defect" + to_string(idx - 1);
		if (idx < ClassifierResult::getNameSize() - 2)
		{
			output += ", ";
		}
	}

	return output;
}

void AppDatabase::bindEachDefectToReport(sqlite3_stmt *stmt, const int start, const map<string, int> &value)
{
	if (start <= 0 || value.empty())
	{
		return;
	}

	int idx = start;
	for (const auto &itr : value)
	{
		LogINFO << "column: " << itr.first << ", bind in " << idx << ", with value: " << itr.second;
		sqlite3_bind_int(stmt, idx++, itr.second);
	}
}

string AppDatabase::getBindedValueSequence(const int start, const int size)
{
	if (start <= 0 || size <= 0)
	{
		return "";
	}

	// should have output ?3, ?4, .. ?(n-2), if start is 3, and size is n
	string output = "";
	for (int idx = start; idx < size + start - 1; idx++)
	{
		output += "?" + to_string(idx) + ", ";
	}

	// the last one
	output += "?" + to_string(start + size - 1);

	return output;
}

int AppDatabase::defectAdd(const string& time, const string& prod, const string& board, const string& lot, const int prod_no, const map<string, int> &defects)
{
	if (!DbOpened())
	{
		LogERROR << "Database is not opened.";
		return 1;
	}

	LogINFO << "Detective product add: " << prod << ", " << board << ", " << lot << ", " << prod_no;

	// make column name and value sequence, create column "defect2, defect6, ..." and number "?7, ?8, ..."
	string insertColumns = reportInsertColumns(defects);
	string defectColumns = getBindedValueSequence(int(DefectCols::DEFECT_START) + 1, (int)defects.size());

	// insert and defect should be matched, which means they are all empty or all not empty
	if ((insertColumns.empty() && !defectColumns.empty()) || (!insertColumns.empty() && defectColumns.empty()))
	{
		LogERROR << "Defect columns are invalid.";
		return 1;
	}

	string hhmmss = ("" == time) ? shared_utils::getTime(0) : time;

	unique_lock<mutex> lock(m_databaseMutex);

	sqlite3_stmt *stmt;
	string sql;

	sql = "insert into defect (time, prod, board, lot, prod_no" + (insertColumns.empty() ? ")" : ", " + insertColumns + ")");
	sql += " values (?1, ?2, ?3, ?4, ?5";
	sql += (defectColumns.empty() ? ")" : ", " + defectColumns + ")");

	sqlite3_prepare_v2(GetDB(), sql.c_str(), -1, &stmt, NULL);

	sqlite3_bind_text(stmt, 1, hhmmss.c_str(), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, prod.c_str(), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 3, board.c_str(), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 4, lot.c_str(), -1, SQLITE_STATIC);
	sqlite3_bind_int(stmt, 5, prod_no);
	if (!insertColumns.empty())
	{
		bindEachDefectToReport(stmt, int(DefectCols::DEFECT_START) + 1, defects);
	}

	if (sqlite3_step(stmt) != SQLITE_DONE)
	{
		LogERROR << "ERROR adding defect: " << sqlite3_errmsg(GetDB());
	}

	return sqlite3_finalize(stmt);
}

int AppDatabase::defectAdd(const DefectData& defectData)
{
	const string& sTime = ("" == defectData.hhmmss) ? shared_utils::getTime(0) : defectData.hhmmss;
	return defectAdd(sTime, defectData.prod, defectData.board, defectData.lot, stoi(defectData.prod_no), defectData.defects);
}

int AppDatabase::defectRead(vector<DefectData>& rpv, const string& startTime, const string& endTime, const string& prod, const string& board, const string& lot)
{
	rpv.clear();
	if (!DbOpened())
	{
		LogERROR << "Database is not opened.";
		return 1;
	}

	// we allow prod is empty, which will only select by start time, end time, and board
	if (prod.empty())
	{
		LogINFO << "Defect read with lot: from " << startTime << " to " << endTime << ", board: " << board;
	}
	else
	{
		LogINFO << "Defect read with lot: from " << startTime << " to " << endTime << ", prod: " << prod << ", board: " << board;
	}

	string defectColumns = reportSelectColumns();
	if (defectColumns.empty())
	{
		LogERROR << "Invalid classfication result defined, reported defect columns are empty";
		return 1;
	}

	string now = shared_utils::getTime(0);
	double hours = shared_utils::getTimeDiff(startTime, endTime);

	if (hours < 0)
	{
		LogERROR << "Invalid time difference.";
		return 1;
	}

	unique_lock<mutex> lock(m_databaseMutex);

	sqlite3_stmt *stmt;
	int iBoardBindID = prod.empty() ? 3 : 4;
	int iLotBindID;
	if (prod.empty() && board == "-1")
	{
		iLotBindID = 3;
	}
	else if (!prod.empty() && board != "-1")
	{
		iLotBindID = 5;
	}
	else
	{
		iLotBindID = 4;
	}
	string sql;
	sql = "select time, prod, board, lot, prod_no, " + defectColumns
		  + " from defect where time >= ?1 and time < ?2" + (!prod.empty() ? " and prod = ?3" : "") + (board != "-1" ? " and board = ?" + to_string(iBoardBindID) : "") 
		  + (lot != "-1" ? " and lot = ?" + to_string(iLotBindID) : "");

	sqlite3_prepare_v2(GetDB(), sql.c_str(), -1, &stmt, NULL);

	sqlite3_bind_text(stmt, 1, startTime.c_str(), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, endTime.c_str(), -1, SQLITE_STATIC);
	if (!prod.empty())
	{
		sqlite3_bind_text(stmt, 3, prod.c_str(), -1, SQLITE_STATIC);
	}
	if (board != "-1")
	{
		sqlite3_bind_text(stmt, iBoardBindID, board.c_str(), -1, SQLITE_STATIC);
	}
	if (lot != "-1")
	{
		sqlite3_bind_text(stmt, iLotBindID, lot.c_str(), -1, SQLITE_STATIC);
	}

	bool done(false);
	while (!done)
	{
		DefectData rp;
		switch (sqlite3_step(stmt))
		{
			case SQLITE_ROW:
			{
				rp.hhmmss = shared_utils::convertTextToString(sqlite3_column_text(stmt, int(DefectCols::TIME)));
				rp.prod = shared_utils::convertTextToString(sqlite3_column_text(stmt, int(DefectCols::PROD)));
				rp.board = shared_utils::convertTextToString(sqlite3_column_text(stmt, int(DefectCols::BOARD)));
				rp.lot = shared_utils::convertTextToString(sqlite3_column_text(stmt, int(DefectCols::LOT)));
				rp.prod_no = shared_utils::convertTextToString(sqlite3_column_text(stmt, int(DefectCols::PROD_NO)));
				rp.defects.clear();
				const int iDefectStartIndex = 2;
				for (int idx = iDefectStartIndex; idx < ClassifierResult::getNameSize() - 1; idx++)
				{
					string column = ClassifierResult::getName(idx);
					if (column.empty())
					{
						LogERROR << "ERROR getting column name";
						return 1;
					}
					int iColID = (int)int(DefectCols::DEFECT_START) + idx - iDefectStartIndex;
					string sDefectCount = shared_utils::convertTextToString(sqlite3_column_text(stmt, iColID));
					rp.defects.emplace(column, stoi(sDefectCount));
				}
				rpv.push_back(rp);
			}
				break;
			case SQLITE_DONE:
			{
				done = true;
			}
				break;
			default:
				LogERROR << "ERROR reading defect: " << sqlite3_errmsg(GetDB());
				return sqlite3_finalize(stmt);
		}
	}
	
	return sqlite3_finalize(stmt);
}

/*************** MES *******************/
int AppDatabase::mesAdd(const string& time, const string& prod, const string& board, const string& lot, 
		int prod_no, int total, int total_defect, const string& plc_code, const string& shift_type, const map<string, int> &defects)
{
	if (!DbOpened())
	{
		LogERROR << "Database is not opened.";
		return 1;
	}

	// make column name and value sequence, create column "defect2, defect6, ..." and number "?7, ?8, ..."
	string insertColumns = reportInsertColumns(defects);
	string defectColumns = getBindedValueSequence(int(MesCols::DEFECT_START) + 1, (int)defects.size());

	// insert and defect should be matched, which means they are all empty or all not empty
	if ((insertColumns.empty() && !defectColumns.empty()) || (!insertColumns.empty() && defectColumns.empty()))
	{
		LogERROR << "Defect columns are invalid.";
		cout << "Defect columns are invalid." << endl;
		return 1;
	}

	const string& hhmmss = ("" == time) ? shared_utils::getTime(0) : time;

	unique_lock<mutex> lock(m_databaseMutex);

	sqlite3_stmt *stmt;
	string sql;

	sql = "insert into mes (time, prod, board, lot, prod_no, total, total_defect, plc_code, shift_type" + (insertColumns.empty() ? ")" : ", " + insertColumns + ")");
	sql += " values (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9";
	sql += (defectColumns.empty() ? ")" : ", " + defectColumns + ")");

	sqlite3_prepare_v2(GetDB(), sql.c_str(), -1, &stmt, NULL);

	sqlite3_bind_text(stmt, 1, hhmmss.c_str(), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, prod.c_str(), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 3, board.c_str(), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 4, lot.c_str(), -1, SQLITE_STATIC);
	sqlite3_bind_int(stmt, 5, prod_no);
	sqlite3_bind_int(stmt, 6, total);
	sqlite3_bind_int(stmt, 7, total_defect);
	sqlite3_bind_text(stmt, 8, plc_code.c_str(), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 9, shift_type.c_str(), -1, SQLITE_STATIC);
	if (!insertColumns.empty())
	{
		bindEachDefectToReport(stmt, int(MesCols::DEFECT_START) + 1, defects);
	}

	if (sqlite3_step(stmt) != SQLITE_DONE)
	{
		LogERROR << "ERROR adding defect: " << sqlite3_errmsg(GetDB());
	}

	return sqlite3_finalize(stmt);
}

int AppDatabase::mesAdd(MesData& mesData)
{
	if ("" == mesData.hhmmss)
		mesData.hhmmss = shared_utils::getTime(0);
	if ("" == mesData.shift_type)
		mesData.getShiftTypeFromDate();
	return mesAdd(mesData.hhmmss, mesData.prod, mesData.board, mesData.lot, stoi(mesData.prod_no), 
			stoi(mesData.total), stoi(mesData.total_defect), mesData.plc_code, mesData.shift_type, mesData.defects);
}

int AppDatabase::mesRead(vector<MesData>& rpv, const string& startTime, const string& endTime, 
		const string& startProdNo, const string& endProdNo, const string& prod, const string& board, const string& lot)
{
	rpv.clear();
	if (!DbOpened())
	{
		LogERROR << "Database is not opened.";
		return 1;
	}

	string defectColumns = reportSelectColumns();
	if (defectColumns.empty())
	{
		LogERROR << "Invalid classfication result defined, reported defect columns are empty";
		return 1;
	}

	if (startTime > endTime)
	{
		LogERROR << "Invalid time";
		return 1;
	}

	if (stoi(startProdNo) > stoi(endProdNo))
	{
		LogERROR << "Invalid prod no";
		return 1;
	}

	unique_lock<mutex> lock(m_databaseMutex);

	sqlite3_stmt *stmt;
	int iBoardBindID = prod.empty() ? 5 : 6;
	int iLotBindID;
	if (prod.empty() && board == "-1")
	{
		iLotBindID = 5;
	}
	else if (!prod.empty() && board != "-1")
	{
		iLotBindID = 7;
	}
	else
	{
		iLotBindID = 6;
	}
	string sql;
	sql = "select time, prod, board, lot, prod_no, total, total_defect, plc_code, shift_type, " + defectColumns
		  + " from mes where time >= ?1 and time < ?2 and prod_no >= ?3 and prod_no < ?4" + (!prod.empty() ? " and prod = ?5" : "") + (board != "-1" ? " and board = ?" + to_string(iBoardBindID) : "") 
		  + (lot != "-1" ? " and lot = ?" + to_string(iLotBindID) : "");
	sqlite3_prepare_v2(GetDB(), sql.c_str(), -1, &stmt, NULL);

	sqlite3_bind_text(stmt, 1, startTime.c_str(), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, endTime.c_str(), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 3, startProdNo.c_str(), -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 4, endProdNo.c_str(), -1, SQLITE_STATIC);
	if (!prod.empty())
	{
		sqlite3_bind_text(stmt, 5, prod.c_str(), -1, SQLITE_STATIC);
	}
	if (board != "-1")
	{
		sqlite3_bind_text(stmt, iBoardBindID, board.c_str(), -1, SQLITE_STATIC);
	}
	if (lot != "-1")
	{
		sqlite3_bind_text(stmt, iLotBindID, lot.c_str(), -1, SQLITE_STATIC);
	}

	bool done(false);
	while (!done)
	{
		MesData rp;
		switch (sqlite3_step(stmt))
		{
			case SQLITE_ROW:
			{
				rp.hhmmss = shared_utils::convertTextToString(sqlite3_column_text(stmt, int(MesCols::TIME)));
				rp.prod = shared_utils::convertTextToString(sqlite3_column_text(stmt, int(MesCols::PROD)));
				rp.board = shared_utils::convertTextToString(sqlite3_column_text(stmt, int(MesCols::BOARD)));
				rp.lot = shared_utils::convertTextToString(sqlite3_column_text(stmt, int(MesCols::LOT)));
				rp.prod_no = shared_utils::convertTextToString(sqlite3_column_text(stmt, int(MesCols::PROD_NO)));
				rp.total = shared_utils::convertTextToString(sqlite3_column_text(stmt, int(MesCols::TOTAL)));
				rp.total_defect = shared_utils::convertTextToString(sqlite3_column_text(stmt, int(MesCols::TOTAL_DEFECT)));
				rp.plc_code = shared_utils::convertTextToString(sqlite3_column_text(stmt, int(MesCols::PLC_CODE)));
				rp.shift_type = shared_utils::convertTextToString(sqlite3_column_text(stmt, int(MesCols::SHIFT_TYPE)));
				rp.defects.clear();
				const int iDefectStartIndex = 2;
				for (int idx = iDefectStartIndex; idx < ClassifierResult::getNameSize() - 1; idx++)
				{
					string column = ClassifierResult::getName(idx);
					if (column.empty())
					{
						LogERROR << "ERROR getting column name";
						return 1;
					}
					int iColID = (int)(MesCols::DEFECT_START) + idx - iDefectStartIndex;
					string sDefectCount = shared_utils::convertTextToString(sqlite3_column_text(stmt, iColID));
					rp.defects.emplace(column, stoi(sDefectCount));
				}
				rpv.push_back(rp);
			}
				break;
			case SQLITE_DONE:
			{
				done = true;
			}
				break;
			default:
				LogERROR << "ERROR reading defect: " << sqlite3_errmsg(GetDB());
				return sqlite3_finalize(stmt);
		}
	}

	
	return sqlite3_finalize(stmt);
}
