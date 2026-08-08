#pragma once
#include <vector>
#include <mutex>
#include <memory>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#define MAX_WATCH_NUM 32
#define TCP_LISTEN_PORT 10188

// 和Python完全对齐的行情结构体
#pragma pack(push, 1)
struct QuoteItem
{
	char code[16];
	char name[32];
	double price;
	double open;
	double high;
	double low;
	double pre_close;

	double bid1;
	long long bid_vol1;
	double bid2;
	long long bid_vol2;
	double bid3;
	long long bid_vol3;
	double bid4;
	long long bid_vol4;
	double bid5;
	long long bid_vol5;

	double ask1;
	long long ask_vol1;
	double ask2;
	long long ask_vol2;
	double ask3;
	long long ask_vol3;
	double ask4;
	long long ask_vol4;
	double ask5;
	long long ask_vol5;

	long long vol;
	double amount;
	long long inner_vol;
	long long outer_vol;
	long long cur_vol;
};

struct ShareMemHeader
{
	unsigned int seq;
	int item_count;
	QuoteItem items[MAX_WATCH_NUM];
};
#pragma pack(pop)

class CTdxTcpServer
{
public:
	CTdxTcpServer();
	~CTdxTcpServer();

	// 对外接口完全兼容旧代码，上层无需改动
	bool Connect(const char* ip, uint16_t port);
	void Disconnect();
	bool IsConnected() const;

	unsigned int ReadQuotes(std::vector<QuoteItem>& outItems);
	bool GetQuoteByCode(const std::string& pureCode, QuoteItem& out);

	int GetMarketByCode(const std::string& pureCode);
	int GetMarketByCode(const std::wstring& pureCode);

private:
	// 后台接收线程
	static DWORD WINAPI RecvThreadProc(LPVOID lpParam);
	void RecvLoop();

	// 套接字
	SOCKET m_listenSock;
	SOCKET m_clientSock;
	HANDLE m_hRecvThread;
	bool m_bThreadExit;

	// 行情缓存
	mutable std::mutex m_mutex;
	bool m_connected;
	unsigned int m_last_seq;
	ShareMemHeader m_cacheData;
};