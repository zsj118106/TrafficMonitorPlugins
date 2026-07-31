#include "pch.h"
#include "TdxTcpClient.h"
#include <cstring>

CTdxTcpClient::CTdxTcpClient()
	: m_hMap(nullptr)
	, m_hMutex(nullptr)
	, m_pShare(nullptr)
	, m_last_seq(0)
	, m_connected(false)
{
}

CTdxTcpClient::~CTdxTcpClient()
{
	Disconnect();
}

bool CTdxTcpClient::Connect(const char* /*ip*/, uint16_t /*port*/)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if (m_connected)
		return true;

	// 打开共享内存
	m_hMap = OpenFileMappingA(FILE_MAP_READ, FALSE, SHARE_NAME);
	if (!m_hMap)
		return false;

	// 映射共享内存
	m_pShare = (ShareMemHeader*)MapViewOfFile(m_hMap, FILE_MAP_READ, 0, 0, 0);
	if (!m_pShare)
	{
		CloseHandle(m_hMap);
		m_hMap = nullptr;
		return false;
	}

	// 打开命名互斥体
	m_hMutex = OpenMutexA(MUTEX_ALL_ACCESS, FALSE, MUTEX_NAME);
	if (!m_hMutex)
	{
		UnmapViewOfFile(m_pShare);
		m_pShare = nullptr;
		CloseHandle(m_hMap);
		m_hMap = nullptr;
		return false;
	}

	m_last_seq = 0;
	m_connected = true;
	return true;
}

void CTdxTcpClient::Disconnect()
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if (m_pShare)
	{
		UnmapViewOfFile(m_pShare);
		m_pShare = nullptr;
	}
	if (m_hMap)
	{
		CloseHandle(m_hMap);
		m_hMap = nullptr;
	}
	if (m_hMutex)
	{
		CloseHandle(m_hMutex);
		m_hMutex = nullptr;
	}
	m_connected = false;
	m_last_seq = 0;
}

bool CTdxTcpClient::IsConnected() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_connected;
}

unsigned int CTdxTcpClient::ReadQuotes(std::vector<QuoteItem>& outItems)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if (!m_connected || !m_pShare || !m_hMutex)
		return 0;

	// 等待互斥体，最多200ms
	DWORD ret = WaitForSingleObject(m_hMutex, 200);
	if (ret != WAIT_OBJECT_0)
		return 0;

	unsigned int cur_seq = m_pShare->seq;

	// 读取所有行情数据（每次都返回，不管seq是否变化）
	int cnt = m_pShare->item_count;
	if (cnt > MAX_WATCH_NUM)
		cnt = MAX_WATCH_NUM;

	outItems.clear();
	outItems.reserve(cnt);
	for (int i = 0; i < cnt; i++)
	{
		outItems.push_back(m_pShare->items[i]);
	}

	m_last_seq = cur_seq;
	ReleaseMutex(m_hMutex);
	return cur_seq;
}

bool CTdxTcpClient::GetQuoteByCode(const std::string& pureCode, QuoteItem& out)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if (!m_connected || !m_pShare || !m_hMutex)
		return false;

	// 等待互斥体，最多200ms
	DWORD ret = WaitForSingleObject(m_hMutex, 200);
	if (ret != WAIT_OBJECT_0)
		return false;

	int cnt = m_pShare->item_count;
	if (cnt > MAX_WATCH_NUM)
		cnt = MAX_WATCH_NUM;

	bool found = false;
	for (int i = 0; i < cnt; i++)
	{
		if (strncmp(m_pShare->items[i].code, pureCode.c_str(), 6) == 0)
		{
			out = m_pShare->items[i];
			found = true;
			break;
		}
	}
	ReleaseMutex(m_hMutex);
	return found;
}

int CTdxTcpClient::GetMarketByCode(const std::string& pureCode)
{
	if (pureCode.length() < 1)
		return -1;
	char first = pureCode[0];
	// 沪市：6/5开头
	if (first == '6' || first == '5')
		return 1;
	// 深市：0/3开头
	if (first == '0' || first == '3')
		return 0;
	// 北交所：8/4开头
	if (first == '8' || first == '4')
		return 0;
	return -1;
}

int CTdxTcpClient::GetMarketByCode(const std::wstring& pureCode)
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