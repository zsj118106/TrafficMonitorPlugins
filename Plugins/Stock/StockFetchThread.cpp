#include "pch.h"
#include "StockFetchThread.h"
#include "DataManager.h"
#include "Common.h"
#include "Stock.h"
#include "FloatingWnd.h"
#include <afxwin.h>
#include <afxinet.h>

// 图表数据类型索引（与 m_chart_last_fetch 数组对应）
// 按间隔从短到长排列，遍历时从前往后即可保证短间隔任务优先
// 日K线不在定时队列中，SetFocusStockId 时获取一次即可
enum ChartType
{
	CHART_IOPV = 0,			// ETF基金IOPV（3秒）
	CHART_TIMELINE,			// 分时图（10秒）
	CHART_MIN5_KLINE,		// 5分钟K线（60秒）
	CHART_MIN30_KLINE,		// 30分钟K线（600秒）
	CHART_COUNT
};

CStockFetchThread& CStockFetchThread::Instance()
{
	static CStockFetchThread instance;
	return instance;
}

CStockFetchThread::CStockFetchThread()
{
}

CStockFetchThread::~CStockFetchThread()
{
	Stop();
}

void CStockFetchThread::Start()
{
	if (m_started.exchange(true))
		return;

	AFX_MANAGE_STATE(AfxGetStaticModuleState());

	m_stopping = false;

	// 启动图表线程（即时任务/集合竞价/后台任务/图表定时获取）
	CWinThread* pThread = AfxBeginThread(ThreadProc, this, THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED);
	if (pThread == nullptr)
	{
		m_started = false;
		return;
	}
	pThread->m_bAutoDelete = FALSE;
	m_thread_handle = pThread->m_hThread;
	m_pThread = pThread;
	ResumeThread(m_thread_handle);

	// 启动实时行情线程（所有股票价格+五档，独立运行不阻塞图表线程）
	CWinThread* pRtThread = AfxBeginThread(RealtimeThreadProc, this, THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED);
	if (pRtThread == nullptr)
	{
		m_started = false;
		return;
	}
	pRtThread->m_bAutoDelete = FALSE;
	m_realtime_thread_handle = pRtThread->m_hThread;
	m_realtime_pThread = pRtThread;
	ResumeThread(m_realtime_thread_handle);
}

void CStockFetchThread::Stop()
{
	if (!m_started.exchange(false))
		return;

	m_stopping = true;

	// 唤醒图表线程
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_has_task = false;
		m_task = nullptr;
		m_has_callAuction_task = false;
		m_callAuction_task = nullptr;
		m_background_tasks.clear();
	}
	m_cv.notify_all();

	// 唤醒实时行情线程
	m_realtime_cv.notify_all();

	// 等待图表线程退出（最多2秒，超时则强制终止）
	if (m_thread_handle != nullptr)
	{
		DWORD waitResult = WaitForSingleObject(m_thread_handle, 2000);
		if (waitResult != WAIT_OBJECT_0)
			TerminateThread(m_thread_handle, 0);
		CloseHandle(m_thread_handle);
		m_thread_handle = nullptr;
	}
	delete m_pThread;
	m_pThread = nullptr;
	m_busy = false;
	m_callAuction_busy = false;

	// 等待实时行情线程退出（最多2秒，超时则强制终止）
	if (m_realtime_thread_handle != nullptr)
	{
		DWORD waitResult = WaitForSingleObject(m_realtime_thread_handle, 2000);
		if (waitResult != WAIT_OBJECT_0)
			TerminateThread(m_realtime_thread_handle, 0);
		CloseHandle(m_realtime_thread_handle);
		m_realtime_thread_handle = nullptr;
	}
	delete m_realtime_pThread;
	m_realtime_pThread = nullptr;
	m_realtime_last_fetch = 0;
}

void CStockFetchThread::PostTask(Task task)
{
	if (!m_started.load() || m_stopping.load())
		return;
	if (m_busy.load())
		return;

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_busy.load() || m_has_task || m_stopping.load())
			return;
		m_task = std::move(task);
		m_has_task = true;
	}
	m_cv.notify_one();
}

void CStockFetchThread::PostCallAuctionTask(Task task)
{
	if (!m_started.load() || m_stopping.load())
		return;
	if (m_callAuction_busy.load())
		return;

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_callAuction_busy.load() || m_has_callAuction_task || m_stopping.load())
			return;
		m_callAuction_task = std::move(task);
		m_has_callAuction_task = true;
	}
	m_cv.notify_one();
}

void CStockFetchThread::PostBackgroundTask(Task task)
{
	if (!m_started.load() || m_stopping.load())
		return;

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_stopping.load())
			return;
		m_background_tasks.push_back(std::move(task));
	}
	m_cv.notify_one();
}

void CStockFetchThread::SetFocusStockId(const std::wstring& stockId)
{
	bool changed = false;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_focus_stock_id == stockId)
			return;
		m_focus_stock_id = stockId;
		// 重置图表计时器，使线程立即获取新股数据
		memset(m_chart_last_fetch, 0, sizeof(m_chart_last_fetch));
		changed = true;
	}
	if (changed)
	{
		m_cv.notify_one();
		// 日K线数据每天只变化一次，切换股票时获取一次即可，无需定时轮询
		// 通过 PostTask 投递到工作线程执行，避免在主线程做网络请求
		PostTask([stockId]() {
			g_data.RequestKLineData(stockId, 750);
			});
	}
}

std::wstring CStockFetchThread::GetFocusStockId() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_focus_stock_id;
}

time_t CStockFetchThread::GetChartInterval(int type)
{
	switch (type)
	{
	case CHART_TIMELINE:		return 10;		// 分时图：盘中实时变化，10秒
	case CHART_MIN5_KLINE:		return 60;		// 5分钟K线：60秒
	case CHART_MIN30_KLINE:		return 600;		// 30分钟K线：10分钟
	case CHART_IOPV:			return 3;		// ETF基金IOPV：实时估值，3秒
	default: return 10;
	}
}

UINT CStockFetchThread::ThreadProc(LPVOID pParam)
{
	CStockFetchThread* pThis = static_cast<CStockFetchThread*>(pParam);
	pThis->Run();
	return 0;
}

void CStockFetchThread::Run()
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());

	while (true)
	{
		// 1. 先检查是否有投递的即时任务（日K线/集合竞价/后台任务）
		Task task;
		bool isCallAuctionTask = false;
		bool isBackgroundTask = false;
		{
			std::lock_guard<std::mutex> lock(m_mutex);

			// 优先级：常规任务 > 集合竞价 > 后台任务 > 图表定时任务（实时行情在独立线程）
			if (m_has_task)
			{
				task = std::move(m_task);
				m_task = nullptr;
				m_has_task = false;
				m_busy = true;
			}
			else if (m_has_callAuction_task)
			{
				task = std::move(m_callAuction_task);
				m_callAuction_task = nullptr;
				m_has_callAuction_task = false;
				m_callAuction_busy = true;
				isCallAuctionTask = true;
			}
			else if (!m_background_tasks.empty())
			{
				task = std::move(m_background_tasks.front());
				m_background_tasks.pop_front();
				isBackgroundTask = true;
			}
		}

		// 执行即时任务
		if (task)
		{
			if (m_stopping.load())
				return;
			try
			{
				task();
			}
			catch (CInternetException* e)
			{
				e->Delete();
			}
			catch (...)
			{
			}
			if (isCallAuctionTask)
				m_callAuction_busy = false;
			else if (!isBackgroundTask)
				m_busy = false;
			// UI刷新由实时行情线程统一驱动，此处仅更新数据
			continue;
		}

		// 2. 检查图表定时任务
		std::wstring stockId;
		time_t now = time(nullptr);
		int chartType = -1;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_stopping.load())
				return;
			stockId = m_focus_stock_id;

			if (!stockId.empty())
			{
				// 查找间隔已到的图表任务
				// enum 按间隔从短到长排列，从前往后遍历保证短间隔任务优先执行
				for (int i = 0; i < CHART_COUNT; i++)
				{
					// IOPV 仅对基金代码获取，且仅在交易时段获取
					if (i == CHART_IOPV && (!CCommon::IsFundCode(stockId) || !CDataManager::IsMarketOpen()))
						continue;

					time_t interval = GetChartInterval(i);
					time_t elapsed = now - m_chart_last_fetch[i];
					if (elapsed >= interval)
					{
						chartType = i;
						break;
					}
				}
			}
		}

		// 执行图表任务
		if (chartType >= 0)
		{
			if (m_stopping.load())
				return;
			try
			{
				switch (chartType)
				{
				case CHART_TIMELINE:
					g_data.RequestTimelineData(stockId);
					break;
				case CHART_MIN5_KLINE:
					g_data.RequestMin5KLineData(stockId, 250);
					break;
				case CHART_MIN30_KLINE:
					g_data.RequestMin30KLineData(stockId, 250);
					break;
				case CHART_IOPV:
					g_data.RequestFundIOPV(stockId);
					break;
				}
			}
			catch (CInternetException* e)
			{
				e->Delete();
			}
			catch (...)
			{
			}

			// 更新该类型的上次获取时间
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_chart_last_fetch[chartType] = time(nullptr);
			}
			// UI刷新由实时行情线程统一驱动，此处仅更新数据
			continue;  // 执行完一个图表任务后立即检查下一个
		}

		// 3. 没有即时任务也没有到时的图表任务，计算等待时间
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			if (m_stopping.load())
				return;

			// 计算最近的图表任务还需要等多久
			time_t minWaitSec = 1;  // 默认1秒轮询一次（确保能及时响应新投递的任务）
			if (!m_focus_stock_id.empty())
			{
				now = time(nullptr);
				for (int i = 0; i < CHART_COUNT; i++)
				{
					if (i == CHART_IOPV && !CCommon::IsFundCode(m_focus_stock_id))
						continue;
					time_t interval = GetChartInterval(i);
					time_t elapsed = now - m_chart_last_fetch[i];
					time_t remaining = interval - elapsed;
					if (remaining > 0 && remaining < minWaitSec)
						minWaitSec = remaining;
				}
			}

			// 等待：可被 PostTask/PostBackgroundTask/SetFocusStockId/Stop 提前唤醒
			m_cv.wait_for(lock, std::chrono::seconds(minWaitSec));
		}
	}
}

UINT CStockFetchThread::RealtimeThreadProc(LPVOID pParam)
{
	CStockFetchThread* pThis = static_cast<CStockFetchThread*>(pParam);
	pThis->RunRealtime();
	return 0;
}

void CStockFetchThread::RunRealtime()
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());

	while (true)
	{
		if (m_stopping.load())
			return;

		// 检查是否需要获取实时行情
		bool bTradingSession = CDataManager::IsTradingDaySession();
		bool bFullDay = g_data.m_setting_data.m_full_day == 1;
		bool needFetch = false;
		time_t waitSec = 1;  // 默认1秒后重新检查

		if (bTradingSession || bFullDay)
		{
			if (g_data.m_setting_data.m_stock_codes.empty())
			{
				// 无股票代码时重置文本，并标记已获取避免重复
				if (m_realtime_last_fetch != 0)
				{
					g_data.ResetText();
					m_realtime_last_fetch = time(nullptr);
				}
			}
			else
			{
				time_t interval = CDataManager::IsMarketOpen()
					? REALTIME_INTERVAL_TRADING : REALTIME_INTERVAL_LUNCH;
				time_t elapsed = time(nullptr) - m_realtime_last_fetch;
				if (elapsed >= interval)
					needFetch = true;
				else
					waitSec = interval - elapsed;
			}
		}

		if (needFetch)
		{
			if (m_stopping.load())
				return;
			try
			{
				g_data.RequestRealtimeData();
			}
			catch (CInternetException* e)
			{
				e->Delete();
			}
			catch (...)
			{
			}
			m_realtime_last_fetch = time(nullptr);

			// 数据获取完成后通知浮动窗口刷新
			Stock::Instance().NotifyFloatingWndUpdate();

			continue;  // 获取完成后立即检查是否需要下一次
		}

		// 等待：可被 Stop 提前唤醒
		std::unique_lock<std::mutex> lock(m_realtime_mutex);
		if (m_stopping.load())
			return;
		m_realtime_cv.wait_for(lock, std::chrono::seconds(waitSec));
	}
}