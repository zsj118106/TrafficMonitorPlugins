#pragma once
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <string>
#include <deque>

// 股票数据获取专用工作线程
// 将所有网络数据获取与 UI 交互彻底分离：
//  - 持有两个长期运行的后台线程，避免每次请求都创建/销毁线程
//  - 实时行情线程：自主定时获取所有股票价格+五档（盘中2秒，午休60秒）
//  - 图表线程：处理即时任务/集合竞价/后台任务/图表定时获取（分时/K线/IOPV）
//  - 两个线程独立运行，互不阻塞
//  - 切换股票时调用 SetFocusStockId，图表线程自动重置计时器并立即获取新股数据
class CStockFetchThread
{
public:
	using Task = std::function<void()>;

	static CStockFetchThread& Instance();

	// 启动工作线程（实时行情线程 + 图表线程，幂等，重复调用安全）
	void Start();
	// 通知所有工作线程退出并等待其结束（在主线程退出前调用）
	void Stop();

	// 启动外部共享内存程序（getPrice.exe）
	void StartExternalProcess();
	// 关闭外部共享内存程序
	void StopExternalProcess();

	// 投递一个常规任务到工作线程（用于日K线等一次性请求）
	// 线程未启动、正在退出或当前正忙于执行同类任务时，忽略本次请求
	void PostTask(Task task);
	// 投递一个集合竞价数据获取任务到工作线程
	void PostCallAuctionTask(Task task);
	// 投递一个后台任务到工作线程（排队执行，不丢弃）
	// 用于预加载、筹码峰等不可丢弃的任务
	void PostBackgroundTask(Task task);

	// 工作线程是否正忙于执行常规任务
	bool IsBusy() const { return m_busy.load(); }
	// 工作线程是否正忙于执行集合竞价任务
	bool IsCallAuctionBusy() const { return m_callAuction_busy.load(); }

	// 设置当前关注的股票ID（图表数据将针对此股票定时获取）
	// 切换股票时自动重置图表计时器，使线程立即获取新股数据
	void SetFocusStockId(const std::wstring& stockId);
	// 获取当前关注的股票ID
	std::wstring GetFocusStockId() const;

private:
	CStockFetchThread();
	~CStockFetchThread();
	CStockFetchThread(const CStockFetchThread&) = delete;
	CStockFetchThread& operator=(const CStockFetchThread&) = delete;

	static UINT ThreadProc(LPVOID pParam);
	void Run();

	// 实时行情独立线程
	static UINT RealtimeThreadProc(LPVOID pParam);
	void RunRealtime();

	// 各图表数据类型的获取间隔（秒）
	static time_t GetChartInterval(int type);
	// 各图表数据类型的上次获取时间
	time_t m_chart_last_fetch[4] = {};  // IOPV, Timeline, Min5KLine, Min30KLine

	// 实时行情定时获取
	time_t m_realtime_last_fetch = 0;	// 上次获取实时行情的时间
	static const time_t REALTIME_INTERVAL_TRADING = 2;	// 盘中实时行情间隔（秒）
	static const time_t REALTIME_INTERVAL_LUNCH = 60;	// 午休期间间隔（秒）

	mutable std::mutex m_mutex;
	std::condition_variable m_cv;
	std::atomic<bool> m_started{ false };
	std::atomic<bool> m_stopping{ false };

	// 实时行情线程独立的同步设施（与图表线程互不干扰）
	std::mutex m_realtime_mutex;
	std::condition_variable m_realtime_cv;

	// 常规任务（实时行情）
	std::atomic<bool> m_busy{ false };
	Task m_task;
	bool m_has_task{ false };

	// 集合竞价任务
	std::atomic<bool> m_callAuction_busy{ false };
	Task m_callAuction_task;
	bool m_has_callAuction_task{ false };

	// 后台任务队列（预加载、筹码峰等，排队执行不丢弃）
	std::deque<Task> m_background_tasks;

	// 当前关注的股票ID（图表数据获取目标）
	std::wstring m_focus_stock_id;

	HANDLE m_thread_handle{ nullptr };
	class CWinThread* m_pThread{ nullptr };

	// 实时行情线程
	HANDLE m_realtime_thread_handle{ nullptr };
	class CWinThread* m_realtime_pThread{ nullptr };

	// 外部共享内存进程
	HANDLE m_hExternalProcess{ nullptr };
	DWORD m_dwExternalPid{ 0 };
	HANDLE m_hJob{ nullptr };		// Job Object，确保子进程随主进程一起终止
	HANDLE m_hStartMutex{ nullptr };	// 跨进程互斥体，防止多个TM进程重复启动getPrice.exe
};
