#include "xj_app_tracker.h"

using namespace std;

bool AppTrackingObject::getBatchedResult(std::vector<std::vector<ClassificationResult>> &result)
{
	result = m_pBoard->resultBatch();
	return true;
}
