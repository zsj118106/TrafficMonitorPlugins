#include "pch.h"
#include "TdxTcpServer.h"
#include <cstring>

CTdxTcpServer::CTdxTcpServer()
	: m_listenSock(INVALID_SOCKET)
	, m_clientSock(INVALID_SOCKET)
	, m_hRecvThread(NULL)
	, m_bThreadExit(false)
	, m_connected(false)
	, m_last_seq(0)
{
	ZeroMemory(&m_cacheData, sizeof(m_cacheData));
}

CTdxTcpServer::~CTdxTcpServer()
{
	Disconnect();
}

bool CTdxTcpServer::Connect(const char* /*ip*/, uint16_t /*port*/)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_connected)
		return true;

	// 初始化Winsock
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		return false;

	// 创建监听socket
	m_listenSock = socket(AF_INET, SOCK_STREAM, 0);
	if (m_listenSock == INVALID_SOCKET)
	{
		WSACleanup();
		return false;
	}

	// 端口复用
	BOOL opt = TRUE;
	setsockopt(m_listenSock, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

	sockaddr_in addr;
	addr.sin_family = AF_INET;
	InetPtonA(AF_INET, "127.0.0.1", &addr.sin_addr);
	addr.sin_port = htons(TCP_LISTEN_PORT);

	if (bind(m_listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
	{
		closesocket(m_listenSock);
		m_listenSock = INVALID_SOCKET;
		WSACleanup();
		return false;
	}

	if (listen(m_listenSock, SOMAXCONN) == SOCKET_ERROR)
	{
		closesocket(m_listenSock);
		m_listenSock = INVALID_SOCKET;
		WSACleanup();
		return false;
	}

	// 启动接收线程
	m_bThreadExit = false;
	m_hRecvThread = CreateThread(NULL, 0, RecvThreadProc, this, 0, NULL);
	if (m_hRecvThread == NULL)
	{
		closesocket(m_listenSock);
		m_listenSock = INVALID_SOCKET;
		WSACleanup();
		return false;
	}

	m_connected = true;
	return true;
}

void CTdxTcpServer::Disconnect()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_bThreadExit = true;

	if (m_clientSock != INVALID_SOCKET)
	{
		closesocket(m_clientSock);
		m_clientSock = INVALID_SOCKET;
	}
	if (m_listenSock != INVALID_SOCKET)
	{
		closesocket(m_listenSock);
		m_listenSock = INVALID_SOCKET;
	}
	if (m_hRecvThread != NULL)
	{
		WaitForSingleObject(m_hRecvThread, INFINITE);
		CloseHandle(m_hRecvThread);
		m_hRecvThread = NULL;
	}
	WSACleanup();
	m_connected = false;
	m_last_seq = 0;
	ZeroMemory(&m_cacheData, sizeof(m_cacheData));
}

bool CTdxTcpServer::IsConnected() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_connected;
}

DWORD WINAPI CTdxTcpServer::RecvThreadProc(LPVOID lpParam)
{
	CTdxTcpServer* pThis = (CTdxTcpServer*)lpParam;
	pThis->RecvLoop();
	return 0;
}

void CTdxTcpServer::RecvLoop()
{
	char recvBuf[4096];
	const int HEADER_LEN = 4;
	const int DATA_LEN = sizeof(ShareMemHeader);

	while (!m_bThreadExit)
	{
		// 等待Python客户端连接
		if (m_clientSock == INVALID_SOCKET)
		{
			sockaddr_in cliAddr;
			int addrLen = sizeof(cliAddr);
			m_clientSock = accept(m_listenSock, (sockaddr*)&cliAddr, &addrLen);
			if (m_clientSock == INVALID_SOCKET)
			{
				if (m_bThreadExit) break;
				Sleep(100);
				continue;
			}
			// 关闭Nagle，本地低延迟
			BOOL nodelay = TRUE;
			setsockopt(m_clientSock, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));
		}

		// 先读4字节长度头
		DWORD pkgLen = 0;
		int ret = recv(m_clientSock, (char*)&pkgLen, HEADER_LEN, MSG_WAITALL);
		if (ret != HEADER_LEN)
		{
			closesocket(m_clientSock);
			m_clientSock = INVALID_SOCKET;
			Sleep(200);
			continue;
		}

		// 读取完整行情结构体
		ret = recv(m_clientSock, recvBuf, DATA_LEN, MSG_WAITALL);
		if (ret != DATA_LEN)
		{
			closesocket(m_clientSock);
			m_clientSock = INVALID_SOCKET;
			Sleep(200);
			continue;
		}

		// 更新缓存，加锁
		std::lock_guard<std::mutex> lock(m_mutex);
		memcpy(&m_cacheData, recvBuf, DATA_LEN);
	}
}

unsigned int CTdxTcpServer::ReadQuotes(std::vector<QuoteItem>& outItems)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!m_connected)
		return 0;

	outItems.clear();
	int cnt = m_cacheData.item_count;
	if (cnt > MAX_WATCH_NUM) cnt = MAX_WATCH_NUM;
	outItems.reserve(cnt);
	for (int i = 0; i < cnt; i++)
	{
		outItems.push_back(m_cacheData.items[i]);
	}
	m_last_seq = m_cacheData.seq;
	return m_cacheData.seq;
}

bool CTdxTcpServer::GetQuoteByCode(const std::string& pureCode, QuoteItem& out)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!m_connected)
		return false;

	int cnt = m_cacheData.item_count;
	if (cnt > MAX_WATCH_NUM) cnt = MAX_WATCH_NUM;
	bool found = false;
	for (int i = 0; i < cnt; i++)
	{
		if (strncmp(m_cacheData.items[i].code, pureCode.c_str(), 6) == 0)
		{
			out = m_cacheData.items[i];
			found = true;
			break;
		}
	}
	return found;
}

int CTdxTcpServer::GetMarketByCode(const std::string& pureCode)
{
	if (pureCode.length() < 1)
		return -1;
	char first = pureCode[0];
	if (first == '6' || first == '5')
		return 1;
	if (first == '0' || first == '3')
		return 0;
	if (first == '8' || first == '4')
		return 0;
	return -1;
}

int CTdxTcpServer::GetMarketByCode(const std::wstring& pureCode)
{
	if (pureCode.length() < 1)
		return -1;
	wchar_t first = pureCode[0];
	if (first == L'6' || first == L'5')
		return 1;
	if (first == L'0' || first == L'3')
		return 0;
	if (first == L'8' || first == L'4')
		return 0;
	return -1;
}