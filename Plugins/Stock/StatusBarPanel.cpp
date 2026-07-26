#include "pch.h"
#include "StatusBarPanel.h"
#include "ChartColors.h"
#include "Common.h"
#include "DataManager.h"
#include <algorithm>
#include <map>
#include <ctime>
#include <limits>

void CStatusBarPanel::DrawHeader(CDC& memDC, const STOCK::StockInfo& realtimeData, int windowWidth, int headerHeight, const CString& macdTrendSignal)
{
	double diff = realtimeData.GetChangeAmount();
	double diffPercent = realtimeData.GetChangePercent();
	COLORREF diffColor = diff >= 0 ? COLOR_RED_UP : COLOR_GREEN_DOWN;

	// 标题格式：(股票代码)股票名称：
	CString prefixTxt;
	prefixTxt.Format(_T("%s:"), realtimeData.displayName.c_str());

	CString currentTxt = realtimeData.IsETF() ? CCommon::FormatETFPrice(realtimeData.currentPrice) : CCommon::FormatFloat(realtimeData.currentPrice);
	CString diffTxt;
	if (diff >= 0)
		diffTxt.Format(_T(" +%.2f%%"), diffPercent);
	else
		diffTxt.Format(_T(" %.2f%%"), diffPercent);

	// MACD趋势信号标签
	CString macdTxt;
	if (!macdTrendSignal.IsEmpty())
		macdTxt.Format(_T(" [%s]"), macdTrendSignal.GetString());

	// 计算总宽度，在整个标题栏水平居中
	CSize prefixSize = memDC.GetTextExtent(prefixTxt);
	CSize currentSize = memDC.GetTextExtent(currentTxt);
	CSize diffSize = memDC.GetTextExtent(diffTxt);
	CSize macdSize = memDC.GetTextExtent(macdTxt);
	int totalWidth = prefixSize.cx + currentSize.cx + diffSize.cx + macdSize.cx;

	int startX = (windowWidth - totalWidth) / 2;
	int centerY = headerHeight / 2;

	memDC.SetTextColor(COLOR_GRAY_TEXT);
	memDC.TextOut(startX, centerY - prefixSize.cy / 2, prefixTxt);

	int curX = startX + prefixSize.cx;
	memDC.SetTextColor(diffColor);
	memDC.TextOut(curX, centerY - currentSize.cy / 2, currentTxt);

	curX += currentSize.cx;
	memDC.TextOut(curX, centerY - diffSize.cy / 2, diffTxt);

	// 绘制MACD趋势信号
	if (!macdTxt.IsEmpty())
	{
		curX += diffSize.cx;
		// 信号颜色：正T=红色，反T=绿色，持有=橙色，观望=灰色
		COLORREF macdColor;
		if (macdTrendSignal == _T("正T"))
			macdColor = RGB(255, 50, 50);
		else if (macdTrendSignal == _T("反T"))
			macdColor = RGB(0, 180, 0);
		else if (macdTrendSignal == _T("持有"))
			macdColor = RGB(255, 165, 0);
		else
			macdColor = RGB(128, 128, 128);
		memDC.SetTextColor(macdColor);
		memDC.TextOut(curX, centerY - macdSize.cy / 2, macdTxt);
	}
}

void CStatusBarPanel::DrawTimelinePositionInfo(CDC& memDC, const TimelineDrawContext& ctx, const std::wstring& stockId)
{
	double costPrice = g_data.GetCostPrice(stockId);
	double holdingCount = g_data.GetHoldingCount(stockId);

	CString countValue;
	if (holdingCount > 0)
		countValue = CCommon::FormatVolumeInt(holdingCount);
	else
		countValue = _T("--");

	STOCK::StockData::PositionInfo posInfo = {};
	auto stockDataPtr = g_data.GetStockData(stockId);
	if (stockDataPtr)
		posInfo = stockDataPtr->CalculatePositionInfo(costPrice, holdingCount);

	CString totalCostValue;
	if (posInfo.totalCost > 0)
		totalCostValue = CCommon::FormatAmount(posInfo.totalCost);
	else
		totalCostValue = _T("--");

	CString marketValueValue;
	if (posInfo.marketValue > 0)
		marketValueValue = CCommon::FormatAmount(posInfo.marketValue);
	else
		marketValueValue = _T("--");

	CString profitLossValue;
	COLORREF profitLossColor = COLOR_BLACK;
	if (posInfo.totalCost > 0)
	{
		profitLossColor = posInfo.profitLoss >= 0 ? COLOR_RED_UP : COLOR_GREEN_DOWN;
		profitLossValue = CCommon::FormatProfitLoss(posInfo.profitLossPercent, posInfo.profitLoss, true);
	}
	else
		profitLossValue = _T("--");

	CString todayProfitLossValue;
	COLORREF todayProfitLossColor = COLOR_BLACK;
	if (holdingCount > 0 && ctx.realtimeData.prevClosePrice != 0)
	{
		double todayProfitLoss = (ctx.realtimeData.currentPrice - ctx.realtimeData.prevClosePrice) * holdingCount;
		double todayProfitLossPercent = (ctx.realtimeData.currentPrice - ctx.realtimeData.prevClosePrice) / ctx.realtimeData.prevClosePrice * 100;
		todayProfitLossColor = todayProfitLoss >= 0 ? COLOR_RED_UP : COLOR_GREEN_DOWN;
		todayProfitLossValue = CCommon::FormatProfitLoss(todayProfitLossPercent, todayProfitLoss, true);
	}
	else
		todayProfitLossValue = _T("--");

	struct InfoItem {
		CString label;
		CString value;
		COLORREF labelColor;
		COLORREF valueColor;
	};

	InfoItem items[] = {
		{ _T("持仓:"), countValue, COLOR_BLACK, COLOR_BLACK },
		{ _T(" 成本:"), totalCostValue, COLOR_BLACK, COLOR_BLACK },
		{ _T(" 市值:"), marketValueValue, COLOR_BLACK, COLOR_BLACK },
		{ _T(" 盈亏:"), profitLossValue, COLOR_BLACK, profitLossColor },
		{ _T(" 当日盈亏:"), todayProfitLossValue, COLOR_BLACK, todayProfitLossColor }
	};

	int totalWidth = 0;
	for (const auto& item : items)
		totalWidth += memDC.GetTextExtent(item.label).cx + memDC.GetTextExtent(item.value).cx;

	int startX = max(g_data.RDPI(5), (ctx.chartWidth - totalWidth) / 2);
	int currentX = startX;

	for (const auto& item : items)
	{
		memDC.SetTextColor(item.labelColor);
		memDC.TextOut(currentX, ctx.positionY, item.label);
		currentX += memDC.GetTextExtent(item.label).cx;

		memDC.SetTextColor(item.valueColor);
		memDC.TextOut(currentX, ctx.positionY, item.value);
		currentX += memDC.GetTextExtent(item.value).cx;
	}
}

void CStatusBarPanel::DrawKLinePositionInfo(CDC& memDC, int x, int y, int chartWidth, const STOCK::StockInfo& realtimeData, const std::wstring& stockId)
{
	double costPrice = g_data.GetCostPrice(stockId);
	double holdingCount = g_data.GetHoldingCount(stockId);

	CString countLabel = _T("持仓:");
	CString countValue;
	if (holdingCount > 0)
		countValue = CCommon::FormatVolumeInt(holdingCount);
	else
		countValue = _T("--");

	STOCK::StockData::PositionInfo posInfo = {};
	{
		auto stockDataPtr = g_data.GetStockData(stockId);
		if (stockDataPtr)
		{
			posInfo = stockDataPtr->CalculatePositionInfo(costPrice, holdingCount);
		}
	}

	CString totalCostLabel = _T(" 成本:");
	CString totalCostValue;
	if (posInfo.totalCost > 0)
		totalCostValue = CCommon::FormatAmount(posInfo.totalCost);
	else
		totalCostValue = _T("--");

	CString marketValueLabel = _T(" 市值:");
	CString marketValueValue;
	if (posInfo.marketValue > 0)
		marketValueValue = CCommon::FormatAmount(posInfo.marketValue);
	else
		marketValueValue = _T("--");

	CString profitLossLabel = _T(" 盈亏:");
	CString profitLossValue;
	COLORREF profitLossColor = COLOR_BLACK;
	if (posInfo.totalCost > 0)
	{
		profitLossColor = posInfo.profitLoss >= 0 ? COLOR_RED_UP : COLOR_GREEN_DOWN;
		profitLossValue = CCommon::FormatProfitLoss(posInfo.profitLossPercent, posInfo.profitLoss, true);
	}
	else
		profitLossValue = _T("--");

	CString todayProfitLossLabel = _T(" 当日盈亏:");
	CString todayProfitLossValue;
	COLORREF todayProfitLossColor = COLOR_BLACK;
	if (holdingCount > 0 && realtimeData.prevClosePrice != 0 && realtimeData.currentPrice > 0)
	{
		double todayProfitLoss = (realtimeData.currentPrice - realtimeData.prevClosePrice) * holdingCount;
		double todayProfitLossPercent = (realtimeData.currentPrice - realtimeData.prevClosePrice) / realtimeData.prevClosePrice * 100;
		todayProfitLossColor = todayProfitLoss >= 0 ? COLOR_RED_UP : COLOR_GREEN_DOWN;
		todayProfitLossValue = CCommon::FormatProfitLoss(todayProfitLossPercent, todayProfitLoss, false);
	}
	else
		todayProfitLossValue = _T("--");

	CString labelsTxt = countLabel + totalCostLabel + marketValueLabel + profitLossLabel + todayProfitLossLabel;
	CString valuesTxt = countValue + totalCostValue + marketValueValue + profitLossValue + todayProfitLossValue;
	CSize labelsSize = memDC.GetTextExtent(labelsTxt);
	CSize valuesSize = memDC.GetTextExtent(valuesTxt);
	int totalWidth = labelsSize.cx + valuesSize.cx;

	int startX = (chartWidth - totalWidth) / 2;
	startX = max(g_data.RDPI(5), startX);

	memDC.SetTextColor(COLOR_BLACK);
	memDC.TextOut(startX, y, countLabel);
	memDC.TextOut(startX + memDC.GetTextExtent(countLabel).cx, y, countValue);

	int posX = startX + memDC.GetTextExtent(countLabel).cx + memDC.GetTextExtent(countValue).cx;
	memDC.TextOut(posX, y, totalCostLabel);
	memDC.TextOut(posX + memDC.GetTextExtent(totalCostLabel).cx, y, totalCostValue);

	posX += memDC.GetTextExtent(totalCostLabel).cx + memDC.GetTextExtent(totalCostValue).cx;
	memDC.TextOut(posX, y, marketValueLabel);
	memDC.TextOut(posX + memDC.GetTextExtent(marketValueLabel).cx, y, marketValueValue);

	posX += memDC.GetTextExtent(marketValueLabel).cx + memDC.GetTextExtent(marketValueValue).cx;
	memDC.TextOut(posX, y, profitLossLabel);
	memDC.SetTextColor(profitLossColor);
	memDC.TextOut(posX + memDC.GetTextExtent(profitLossLabel).cx, y, profitLossValue);

	posX += memDC.GetTextExtent(profitLossLabel).cx + memDC.GetTextExtent(profitLossValue).cx;
	memDC.SetTextColor(COLOR_BLACK);
	memDC.TextOut(posX, y, todayProfitLossLabel);
	memDC.SetTextColor(todayProfitLossColor);
	memDC.TextOut(posX + memDC.GetTextExtent(todayProfitLossLabel).cx, y, todayProfitLossValue);
}

void CStatusBarPanel::DrawKLineInfoPanel(CDC& memDC, int left, int right, int bottomY, const STOCK::StockInfo& stockInfo, const std::vector<STOCK::KLinePoint>& klineData, const std::wstring& stockId)
{
	int textX = left + g_data.RDPI(5) + 3;
	int topY = g_data.RDPI(24);
	// bottomY 由调用方传入，即持仓信息栏的起始位置
	int availableHeight = bottomY - topY;
	if (availableHeight <= 0) return;

	memDC.SetBkMode(TRANSPARENT);

	std::wstring buyDate = g_data.GetBuyDate(stockId);
	double costPrice = g_data.GetCostPrice(stockId);
	double holdingCount = g_data.GetHoldingCount(stockId);

	const int DAYS_PER_YEAR = 250;
	auto stockDataPtr = g_data.GetStockData(stockId);
	auto* klineObj = stockDataPtr ? stockDataPtr->getKLineData() : nullptr;

	struct PeriodStats
	{
		STOCK::Price maxPrice;
		STOCK::Price minPrice;
		std::string maxDate;
		std::string minDate;
	};

	// 分段计算最高价和最低价：1年=最近250天，2年=251-500天，3年=501-750天
	std::map<int, PeriodStats> periodStatsMap;
	for (int year : {1, 2, 3})
	{
		PeriodStats stats = { 0, 0, "", "" };
		int endIdx = 0, startIdx = 0;

		if (klineObj && !klineObj->data.empty())
		{
			int dataSize = static_cast<int>(klineObj->data.size());
			// 1年: [dataSize-250, dataSize-1], 2年: [dataSize-500, dataSize-251], 3年: [dataSize-750, dataSize-501]
			endIdx = dataSize - (year - 1) * DAYS_PER_YEAR - 1;
			startIdx = max(0, dataSize - year * DAYS_PER_YEAR);

			if (startIdx <= endIdx && endIdx >= 0)
			{
				double maxPrice = 0;
				double minPrice = (std::numeric_limits<double>::max)();
				for (int i = startIdx; i <= endIdx && i < dataSize; i++)
				{
					if (klineObj->data[i].high > maxPrice)
					{
						maxPrice = klineObj->data[i].high;
						stats.maxDate = klineObj->data[i].day;
					}
					if (klineObj->data[i].low < minPrice)
					{
						minPrice = klineObj->data[i].low;
						stats.minDate = klineObj->data[i].day;
					}
				}
				stats.maxPrice = maxPrice;
				stats.minPrice = (minPrice == (std::numeric_limits<double>::max)()) ? 0 : minPrice;
			}
		}
		else if (!klineData.empty())
		{
			int dataSize = static_cast<int>(klineData.size());
			endIdx = dataSize - (year - 1) * DAYS_PER_YEAR - 1;
			startIdx = max(0, dataSize - year * DAYS_PER_YEAR);

			if (startIdx <= endIdx && endIdx >= 0)
			{
				double maxPrice = 0;
				double minPrice = (std::numeric_limits<double>::max)();
				for (int i = startIdx; i <= endIdx && i < dataSize; i++)
				{
					if (klineData[i].high > maxPrice)
					{
						maxPrice = klineData[i].high;
						stats.maxDate = klineData[i].day;
					}
					if (klineData[i].low < minPrice)
					{
						minPrice = klineData[i].low;
						stats.minDate = klineData[i].day;
					}
				}
				stats.maxPrice = maxPrice;
				stats.minPrice = (minPrice == (std::numeric_limits<double>::max)()) ? 0 : minPrice;
			}
		}
		periodStatsMap[year] = stats;
	}

	// 计算每个周期的平均价格
	auto getAvgPrice = [&](int days) -> double {
		if (klineObj)
			return klineObj->CalculateMAPeriod(days, 1);
		else if (!klineData.empty())
		{
			int startIdx = max(0, static_cast<int>(klineData.size()) - days * DAYS_PER_YEAR);
			int endIdx = static_cast<int>(klineData.size()) - 1;
			double sumPrice = 0;
			int count = 0;
			for (int i = startIdx; i <= endIdx; i++) { sumPrice += klineData[i].close; count++; }
			return count > 0 ? sumPrice / count : 0;
		}
		return 0;
		};

	// 收集所有要绘制的项目（文本行或分隔线），用于精确计算行高
	enum ItemType { TEXT_ROW, SEPARATOR };
	struct DrawItem {
		ItemType type;
		CString text;
		COLORREF color;
	};

	std::vector<DrawItem> items;

	// 基本信息区（4行）
	CString buyDateStr(buyDate.c_str());
	buyDateStr.Replace(_T("-"), _T("/"));
	items.push_back({ TEXT_ROW, !buyDate.empty() ? CString(_T("买入:")) + buyDateStr : CString(_T("买入:--")), COLOR_BLACK });

	if (!buyDate.empty())
	{
		int year = 0, month = 0, day = 0;
		if (swscanf_s(buyDate.c_str(), L"%d-%d-%d", &year, &month, &day) == 3)
		{
			std::tm buyTm = { 0 };
			buyTm.tm_year = year - 1900;
			buyTm.tm_mon = month - 1;
			buyTm.tm_mday = day;
			std::time_t buyTime = std::mktime(&buyTm);
			std::time_t now = std::time(nullptr);
			double diffDays = std::difftime(now, buyTime) / (60 * 60 * 24);
			CString holdingTimeStr;
			if (diffDays < 365)
				holdingTimeStr.Format(_T("持有:%d天"), static_cast<int>(diffDays));
			else
			{
				int years = static_cast<int>(diffDays / 365);
				int days = static_cast<int>(diffDays) % 365;
				holdingTimeStr.Format(_T("持有:%d年%d天"), years, days);
			}
			items.push_back({ TEXT_ROW, holdingTimeStr, COLOR_BLACK });
		}
		else
			items.push_back({ TEXT_ROW, _T("持有:--"), COLOR_BLACK });
	}
	else
		items.push_back({ TEXT_ROW, _T("持有:--"), COLOR_BLACK });

	if (costPrice > 0 && holdingCount > 0 && stockInfo.currentPrice > 0)
	{
		STOCK::StockData::PositionInfo posInfo = {};
		auto stockDataPtr2 = g_data.GetStockData(stockId);
		if (stockDataPtr2)
			posInfo = stockDataPtr2->CalculatePositionInfo(costPrice, holdingCount);
		COLORREF profitLossColor = posInfo.profitLoss >= 0 ? COLOR_RED_UP : COLOR_GREEN_DOWN;
		CString profitLossStr;
		if (posInfo.profitLossPercent >= 0)
			profitLossStr.Format(_T("盈亏:+%.2f%%(+%g)"), posInfo.profitLossPercent, posInfo.profitLoss);
		else
			profitLossStr.Format(_T("盈亏:%.2f%%(%g)"), posInfo.profitLossPercent, posInfo.profitLoss);
		items.push_back({ TEXT_ROW, profitLossStr, profitLossColor });
	}
	else
		items.push_back({ TEXT_ROW, _T("盈亏:--"), COLOR_BLACK });

	if (costPrice > 0 && holdingCount > 0 && stockInfo.currentPrice > 0 && !buyDate.empty())
	{
		double annualizedReturn = 0;
		auto stockDataPtr2 = g_data.GetStockData(stockId);
		if (stockDataPtr2)
			annualizedReturn = stockDataPtr2->CalculateAnnualizedReturn(costPrice, holdingCount, buyDate);
		COLORREF annualColor = annualizedReturn >= 0 ? COLOR_RED_UP : COLOR_GREEN_DOWN;
		CString annualStr;
		if (annualizedReturn >= 0)
			annualStr.Format(_T("年化: +%.2f%%"), annualizedReturn);
		else
			annualStr.Format(_T("年化: %.2f%%"), annualizedReturn);
		items.push_back({ TEXT_ROW, annualStr, annualColor });
	}
	else
		items.push_back({ TEXT_ROW, _T("年化:--"), COLOR_BLACK });

	// 周期指标 - 按类型分组：最高价、最低价、平均价
	struct PeriodInfo { int days; COLORREF color; };
	std::vector<PeriodInfo> periods = { {1, COLOR_BLUE_AVG1}, {2, COLOR_GREEN_AVG2}, {3, COLOR_GREEN_AVG3} };

	// 最高价分组
	items.push_back({ SEPARATOR, _T(""), COLOR_GRAY_GRID });
	for (const auto& p : periods)
	{
		auto it = periodStatsMap.find(p.days);
		if (it == periodStatsMap.end()) continue;
		const PeriodStats& stats = it->second;
		if (!(stats.maxPrice > 0)) continue;

		CString maxTxt;
		if (stockInfo.currentPrice > 0)
		{
			double maxDiff = stats.maxPrice - stockInfo.currentPrice;
			double maxDiffPercent = (maxDiff / stats.maxPrice) * 100;
			if (maxDiff >= 0)
				maxTxt.Format(_T("[high%d]:%s +%.2f%%"), p.days, CCommon::FormatFloat(stats.maxPrice), maxDiffPercent);
			else
				maxTxt.Format(_T("[high%d]:%s %.2f%%"), p.days, CCommon::FormatFloat(stats.maxPrice), maxDiffPercent);
		}
		else
			maxTxt.Format(_T("[high%d]:%s"), p.days, CCommon::FormatFloat(stats.maxPrice));
		items.push_back({ TEXT_ROW, maxTxt, p.color });

		CString maxDateStr(stats.maxDate.c_str());
		maxDateStr.Replace(_T("-"), _T("/"));
		items.push_back({ TEXT_ROW, CString(_T("时间:")) + maxDateStr, p.color });
	}

	// 最低价分组
	items.push_back({ SEPARATOR, _T(""), COLOR_GRAY_GRID });
	for (const auto& p : periods)
	{
		auto it = periodStatsMap.find(p.days);
		if (it == periodStatsMap.end()) continue;
		const PeriodStats& stats = it->second;
		if (!(stats.minPrice > 0 && stats.minPrice < (std::numeric_limits<STOCK::Price>::max)() / 2)) continue;

		CString minTxt;
		if (stockInfo.currentPrice > 0)
		{
			double minDiff = stats.minPrice - stockInfo.currentPrice;
			double minDiffPercent = (minDiff / stats.minPrice) * 100;
			if (minDiff >= 0)
				minTxt.Format(_T("[low%d]:%s +%.2f%%"), p.days, CCommon::FormatFloat(stats.minPrice), minDiffPercent);
			else
				minTxt.Format(_T("[low%d]:%s %.2f%%"), p.days, CCommon::FormatFloat(stats.minPrice), minDiffPercent);
		}
		else
			minTxt.Format(_T("[low%d]:%s"), p.days, CCommon::FormatFloat(stats.minPrice));
		items.push_back({ TEXT_ROW, minTxt, p.color });

		CString minDateStr(stats.minDate.c_str());
		minDateStr.Replace(_T("-"), _T("/"));
		items.push_back({ TEXT_ROW, CString(_T("时间:")) + minDateStr, p.color });
	}

	// 平均价分组
	items.push_back({ SEPARATOR, _T(""), COLOR_GRAY_GRID });
	for (const auto& p : periods)
	{
		double avgPrice = getAvgPrice(p.days);
		CString avgTxt;
		if (stockInfo.currentPrice > 0)
		{
			double avgDiff = avgPrice - stockInfo.currentPrice;
			double avgDiffPercent = (avgDiff / avgPrice) * 100;
			if (avgDiff >= 0)
				avgTxt.Format(_T("[avg%d]:%s +%.2f%%"), p.days, CCommon::FormatFloat(avgPrice), avgDiffPercent);
			else
				avgTxt.Format(_T("[avg%d]:%s %.2f%%"), p.days, CCommon::FormatFloat(avgPrice), avgDiffPercent);
		}
		else
			avgTxt.Format(_T("[avg%d]:%s"), p.days, CCommon::FormatFloat(avgPrice));
		items.push_back({ TEXT_ROW, avgTxt, p.color });
	}

	// 计算行高：以字体高度为最小值，确保文字不被截断
	int separatorCount = 0;
	int textCount = 0;
	for (const auto& item : items) {
		if (item.type == SEPARATOR) separatorCount++;
		else textCount++;
	}
	TEXTMETRIC tm;
	memDC.GetTextMetrics(&tm);
	int fontHeight = tm.tmHeight;
	int separatorHeight = g_data.RDPI(4);
	int totalSeparatorHeight = separatorCount * separatorHeight;
	// 均分可用高度，但每行不低于字体高度
	int textRowHeight = textCount > 0 ? (availableHeight - totalSeparatorHeight) / textCount : fontHeight;
	textRowHeight = max(textRowHeight, fontHeight);

	// 绘制所有项目
	int currentY = topY;
	CPen pDashLine(PS_DASH, 1, COLOR_GRAY_GRID);
	CPen* pOldPen = memDC.SelectObject(&pDashLine);

	for (const auto& item : items)
	{
		if (item.type == SEPARATOR)
		{
			memDC.MoveTo(left, currentY);
			memDC.LineTo(right, currentY);
			currentY += separatorHeight;
		}
		else
		{
			memDC.SetTextColor(item.color);
			memDC.TextOut(textX, currentY, item.text);
			currentY += textRowHeight;
		}
	}

	memDC.SelectObject(pOldPen);
}