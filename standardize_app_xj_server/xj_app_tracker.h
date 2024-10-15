#ifndef XJ_APP_TRACKER_H
#define XJ_APP_TRACKER_H

#include "board.h"
#include "tracker.h"

// using board as a tracking object, you can use view or others
class AppTrackingObject : public BaseTrackingObject
{
public:
	AppTrackingObject() : BaseTrackingObject(), m_pBoard(nullptr) {}
	virtual ~AppTrackingObject() {}

	void setBoard(const std::shared_ptr<Board> &pBoard) { m_pBoard = pBoard; }

	ClassificationResult getResult() { return m_pBoard->getResult(); }
	bool getBatchedResult(std::vector<std::vector<ClassificationResult>> &result);
private:
	std::shared_ptr<Board> m_pBoard;
};

class AppTrackingHistory : public BaseTrackingHistory
{
public:
	AppTrackingHistory(const int plcDistance) : BaseTrackingHistory(plcDistance) {}
	virtual ~AppTrackingHistory() {}

	virtual bool calibrate() { return true; };
};

class AppTracker : public BaseTracker
{
public:
	AppTracker(const int boards) : BaseTracker(boards) {}
	virtual ~AppTracker() {}
};

#endif  // XJ_APP_TRACKER_H
