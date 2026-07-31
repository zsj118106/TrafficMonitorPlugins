#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <mutex>
#include <windows.h>

// 共享内存行情数据结构（与Python端一致）
const int MAX_WATCH_NUM = 32;
const char* const SHARE_NAME = "Local\\TdxQuoteShare";
const char* const MUTEX_NAME = "Local\\TdxQuoteMutex";

#pragma pack(push, 1)

typedef struct QuoteItem
{
	char code[16];       // 股票代码
	char name[32];    	 // 股票名称
	double price;        // 现价
	double open;         // 开盘
	double high;         // 最高
	double low;          // 最低
	double pre_close;    // 昨收

	double bid1;         // 买1价
	long long bid_vol1;  // 买1量
	double bid2;
	long long bid_vol2;
	double bid3;
	long long bid_vol3;
	double bid4;
	long long bid_vol4;
	double bid5;
	long long bid_vol5;

	double ask1;         // 卖1价
	long long ask_vol1;  // 卖1量
	double ask2;
	long long ask_vol2;
	double ask3;
	long long ask_vol3;
	double ask4;
	long long ask_vol4;
	double ask5;
	long long ask_vol5;

	long long vol;       // 总手
	double amount;       // 成交额
	long long inner_vol; // 内盘
	long long outer_vol; // 外盘
	long long cur_vol;   // 现手	
} QuoteItem;
#pragma pack(pop)
struct ShareMemHeader
{
	unsigned int seq;                    // 序列号（Python端每次写入递增）
	int item_count;                      // 行情条目数
	QuoteItem items[MAX_WATCH_NUM];      // 行情数据
};
// 通达信共享内存行情客户端
// 通过读取Python端写入的共享内存获取实时行情，零延迟无网络开销
class CTdxTcpClient
{
public:
	CTdxTcpClient();
	~CTdxTcpClient();

	// 打开共享内存和互斥体（线程安全）
	bool Connect(const char* ip = nullptr, uint16_t port = 0);
	// 关闭共享内存（线程安全）
	void Disconnect();
	// 是否已连接（共享内存已打开）
	bool IsConnected() const;

	// 读取共享内存中的行情数据（线程安全）
	// 返回当前seq，0表示读取失败
	unsigned int ReadQuotes(std::vector<QuoteItem>& outItems);

	// 根据股票代码查找行情（线程安全）
	// pureCode: 纯6位数字代码
	bool GetQuoteByCode(const std::string& pureCode, QuoteItem& out);

	// 根据股票代码判断市场（纯数字代码，不含前缀）
	// 返回: 1=沪市, 0=深市, -1=无法判断
	static int GetMarketByCode(const std::string& pureCode);
	static int GetMarketByCode(const std::wstring& pureCode);

private:
	mutable std::mutex m_mutex;
	HANDLE m_hMap;       // 共享内存句柄
	HANDLE m_hMutex;     // 互斥体句柄
	ShareMemHeader* m_pShare;  // 共享内存映射指针
	unsigned int m_last_seq;   // 上次读取的序列号
	bool m_connected;
};
