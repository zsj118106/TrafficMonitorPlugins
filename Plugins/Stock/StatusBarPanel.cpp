#include "pch.h"
#include "StatusBarPanel.h"
#include "ChartColors.h"
#include "Common.h"
#include "DataManager.h"
#include <Stock.h>
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

void CStatusBarPanel::DrawRelatedStockBar(CDC& memDC, int w, int topBarY, int singleBarHeight, const std::wstring& stockId, int viewMode)
{
	const int GAP = 2;

	std::vector<std::wstring> relatedCodes = g_data.GetRelatedStocks(stockId);
	bool isRelatedMode = !relatedCodes.empty();
	if (relatedCodes.empty())
	{
		// 没有关联股票时使用默认指数：上证指数、中证银行、恒生科技
		relatedCodes = { L"sh000001", L"sz399986", L"rt_hkHSTECH" };
	}
	const int relatedCount = static_cast<int>(relatedCodes.size());

	// 从已计算好的均幅数据中获取（由UpdateRelatedStocksAvgDiff在行情更新时计算）
	bool showAvgDiff = isRelatedMode && relatedCount >= 1;
	double avgDiffPercent = 0.0;
	double minAvgDiff = 0.0;
	double maxAvgDiff = 0.0;
	CString minAvgValueStr, avgValueStr, maxAvgValueStr, trendArrowStr;
	if (showAvgDiff)
	{
		auto avgData = g_data.GetAvgDiffData(stockId);
		minAvgDiff = avgData.minVal;
		maxAvgDiff = avgData.maxVal;
		avgDiffPercent = avgData.currentVal;
		// 均幅数据全为0说明尚未计算，不显示均幅区域
		if (minAvgDiff == 0.0 && maxAvgDiff == 0.0 && avgDiffPercent == 0.0)
			showAvgDiff = false;
	}
	if (showAvgDiff)
	{
		if (minAvgDiff >= 0)
			minAvgValueStr.Format(_T("+%.2f"), minAvgDiff);
		else
			minAvgValueStr.Format(_T("%.2f"), minAvgDiff);

		if (avgDiffPercent >= 0)
			avgValueStr.Format(_T("+%.2f"), avgDiffPercent);
		else
			avgValueStr.Format(_T("%.2f"), avgDiffPercent);

		if (maxAvgDiff >= 0)
			maxAvgValueStr.Format(_T("+%.2f"), maxAvgDiff);
		else
			maxAvgValueStr.Format(_T("%.2f"), maxAvgDiff);

		// 计算趋势箭头：分时界面用1分钟趋势，5分钟界面用5分钟趋势
		RegResult trend = (viewMode < UI_VIEW_MIN5_KLINE)
			? g_data.Get1MinAvgTrend(stockId)
			: g_data.Get5MinAvgTrend(stockId);
		trendArrowStr = _T("|"); // 默认竖线
		if (trend.valid)
		{
			if (trend.r2 >= 0.1 || std::abs(trend.slope) >= 0.001)
			{
				// 箭头强度：低1个、中2个、高3个
				int arrowCount = 1;
				if (trend.r2 >= 0.7)
					arrowCount = 3;
				else if (trend.r2 >= 0.55)
					arrowCount = 2;
				if (trend.slope > 0)
					trendArrowStr = CString(_T('↑'), arrowCount);
				else
					trendArrowStr = CString(_T('↓'), arrowCount);
			}
		}
	}

	// 动态计算字体大小：先测量实际文字宽度，再逐步缩小直到适合
	CFont* pOldFont = nullptr;
	CFont dynFont;
	CFont avgFont; // 右侧均幅区域固定字体
	{
		const int minFont = 8;
		const int maxFont = 14;

		// 右侧均幅区域始终使用最大字号
		if (showAvgDiff)
		{
			avgFont.CreateFont(-g_data.RDPI(maxFont), 0, 0, 0, FW_NORMAL, 0, 0, 0,
				DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
				DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("微软雅黑"));
		}

		if (!isRelatedMode)
		{
			// 非关联模式直接用最大字号
			dynFont.CreateFont(-g_data.RDPI(maxFont), 0, 0, 0, FW_NORMAL, 0, 0, 0,
				DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
				DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("微软雅黑"));
			pOldFont = memDC.SelectObject(&dynFont);
		}
		else
		{
			// 关联模式：用实际测量法，从最大字号开始尝试，逐步缩小直到文字总宽度适合
			// 右侧预留120像素不参与均分，仅左侧区域用于字体计算
			// 趋势箭头在120像素区域左侧，也需要预留空间
			int trendArrowWidth = 0;
			if (showAvgDiff && !trendArrowStr.IsEmpty())
			{
				// 用最大字号测量趋势箭头宽度（趋势箭头使用avgFont固定字号）
				CFont tmpAvgFont;
				tmpAvgFont.CreateFont(-g_data.RDPI(maxFont), 0, 0, 0, FW_NORMAL, 0, 0, 0,
					DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
					DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("微软雅黑"));
				CFont* pOldTmpFont = memDC.SelectObject(&tmpAvgFont);
				trendArrowWidth = memDC.GetTextExtent(trendArrowStr).cx + GAP;
				memDC.SelectObject(pOldTmpFont);
				tmpAvgFont.DeleteObject();
			}
			int availableWidth = w - (showAvgDiff ? g_data.RDPI(120) : 0) - trendArrowWidth;

			std::lock_guard<std::mutex> lockMeasure(Stock::Instance().m_stockDataMutex);

			// 预先构建所有文本字符串
			struct StockText { CString nameStr; CString changeStr; bool valid; };
			std::vector<StockText> stockTexts(relatedCount);
			for (int i = 0; i < relatedCount; i++)
			{
				auto stockData = g_data.GetStockData(relatedCodes[i]);
				if (stockData && stockData->info.is_ok)
				{
					const auto& info = stockData->info;
					double displayPrice = info.currentPrice > 0 ? info.currentPrice : info.prevClosePrice;
					double diff = displayPrice - info.prevClosePrice;
					double diffPercent = info.prevClosePrice != 0 ? (diff / info.prevClosePrice) * 100 : 0;
					stockTexts[i].nameStr = info.GetStockListName() + _T(":");
					if (diff >= 0)
						stockTexts[i].changeStr.Format(_T("+%.2f%%"), diffPercent);
					else
						stockTexts[i].changeStr.Format(_T("%.2f%%"), diffPercent);
					stockTexts[i].valid = true;
				}
				else
				{
					stockTexts[i].nameStr = CString(relatedCodes[i].c_str()) + _T(" --");
					stockTexts[i].valid = false;
				}
			}

			int fontSize = maxFont;

			while (fontSize >= minFont)
			{
				// 创建临时字体测量
				CFont tmpFont;
				tmpFont.CreateFont(-g_data.RDPI(fontSize), 0, 0, 0, FW_NORMAL, 0, 0, 0,
					DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
					DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("微软雅黑"));
				CFont* pOldTmp = memDC.SelectObject(&tmpFont);

				// 计算所有股票文字总宽度（已包含每个元素后的GAP间隔）
				int totalWidth = GAP; // 左侧起始边距
				for (int i = 0; i < relatedCount; i++)
				{
					totalWidth += memDC.GetTextExtent(stockTexts[i].nameStr).cx + GAP;
					if (stockTexts[i].valid)
						totalWidth += memDC.GetTextExtent(stockTexts[i].changeStr).cx + GAP;
				}

				memDC.SelectObject(pOldTmp);
				tmpFont.DeleteObject();

				// 检查是否适合左侧可用区域（去掉预留120像素）
				if (totalWidth <= availableWidth)
					break; // 当前字号适合

				fontSize--;
			}
			if (fontSize < minFont) fontSize = minFont;

			dynFont.CreateFont(-g_data.RDPI(fontSize), 0, 0, 0, FW_NORMAL, 0, 0, 0,
				DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
				DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("微软雅黑"));
			pOldFont = memDC.SelectObject(&dynFont);
		}
	}

	std::lock_guard<std::mutex> lock(Stock::Instance().m_stockDataMutex);
	if (isRelatedMode)
	{
		// 关联模式：流式布局，每只股票紧凑排列
		int textX = GAP;
		for (int i = 0; i < relatedCount; i++)
		{
			auto stockData = g_data.GetStockData(relatedCodes[i]);

			if (stockData && stockData->info.is_ok)
			{
				const auto& info = stockData->info;
				double displayPrice = info.currentPrice > 0 ? info.currentPrice : info.prevClosePrice;
				double diff = displayPrice - info.prevClosePrice;
				double diffPercent = info.prevClosePrice != 0 ? (diff / info.prevClosePrice) * 100 : 0;

				CString nameStr = info.GetStockListName() + _T(":");
				CString changeStr;
				if (diff >= 0)
					changeStr.Format(_T("+%.2f%%"), diffPercent);
				else
					changeStr.Format(_T("%.2f%%"), diffPercent);

				memDC.SetTextColor(COLOR_BLACK);
				memDC.TextOut(textX, topBarY + g_data.RDPI(2), nameStr);
				textX += memDC.GetTextExtent(nameStr).cx + GAP;

				memDC.SetTextColor(CCommon::GetProfitLossColor(diffPercent));
				memDC.TextOut(textX, topBarY + g_data.RDPI(2), changeStr);
				textX += memDC.GetTextExtent(changeStr).cx + GAP;
			}
			else
			{
				CString nameStr = CString(relatedCodes[i].c_str()) + _T(" --");
				memDC.SetTextColor(COLOR_GRAY_PURPLE);
				memDC.TextOut(textX, topBarY + g_data.RDPI(2), nameStr);
				textX += memDC.GetTextExtent(nameStr).cx + GAP;
			}
		}

		// 右侧预留120像素显示：最小值 均值 最大值（固定字体大小，红绿背景）
		if (showAvgDiff)
		{
			int avgAreaWidth = g_data.RDPI(120);
			int avgAreaX = w - avgAreaWidth - GAP;
			int avgAreaH = singleBarHeight;
			int avgAreaY = topBarY;

			// 红绿颜色分3档，由浅到深
			static const COLORREF AVG_RED_COLORS[] = {
				RGB(240, 40, 40),   // 浅红
				RGB(180, 50, 50),   // 中红
				RGB(130, 20, 40)    // 深红
			};
			static const COLORREF AVG_GREEN_COLORS[] = {
				RGB(40, 240, 40),   // 浅绿
				RGB(50, 180, 50),   // 中绿
				RGB(20, 130, 40)    // 深绿
			};

			// 红绿颜色深度由均值在区间中的位置决定
			double range = maxAvgDiff - minAvgDiff;
			int redIdx, greenIdx;
			if (range == 0)
			{
				redIdx = 0;
				greenIdx = 2;
			}
			else
			{
				double posRatio = (avgDiffPercent - minAvgDiff) / range;
				posRatio = max(0.0, min(1.0, posRatio));
				redIdx = static_cast<int>(posRatio * 2 + 0.5);
				greenIdx = 2 - redIdx;
				redIdx = max(0, min(2, redIdx));
				greenIdx = max(0, min(2, greenIdx));
			}
			COLORREF redColor = AVG_RED_COLORS[redIdx];
			COLORREF greenColor = AVG_GREEN_COLORS[greenIdx];

			// 绘制红绿背景
			if (range == 0 || avgDiffPercent <= minAvgDiff)
			{
				memDC.FillSolidRect(avgAreaX, avgAreaY, avgAreaWidth, avgAreaH, greenColor);
			}
			else if (avgDiffPercent >= maxAvgDiff)
			{
				memDC.FillSolidRect(avgAreaX, avgAreaY, avgAreaWidth, avgAreaH, redColor);
			}
			else
			{
				double ratio = (avgDiffPercent - minAvgDiff) / range;
				int redWidth = static_cast<int>(ratio * avgAreaWidth);
				redWidth = max(0, min(redWidth, avgAreaWidth));
				int greenWidth = avgAreaWidth - redWidth;
				if (redWidth >= greenWidth)
				{
					memDC.FillSolidRect(avgAreaX, avgAreaY, redWidth, avgAreaH, redColor);
					memDC.FillSolidRect(avgAreaX + redWidth, avgAreaY, greenWidth, avgAreaH, greenColor);
				}
				else
				{
					memDC.FillSolidRect(avgAreaX, avgAreaY, greenWidth, avgAreaH, greenColor);
					memDC.FillSolidRect(avgAreaX + greenWidth, avgAreaY, redWidth, avgAreaH, redColor);
				}
			}

			// 切换到固定字体绘制均幅区域
			CFont* pPrevFont = memDC.SelectObject(&avgFont);

			// 120像素区域三等分
			int thirdWidth = avgAreaWidth / 3;

			// 趋势指示器（120像素左侧，间隔2像素，无背景）
			if (!trendArrowStr.IsEmpty())
			{
				TCHAR firstChar = trendArrowStr.GetAt(0);
				if (firstChar == _T('↑'))
					memDC.SetTextColor(COLOR_RED_UP);
				else if (firstChar == _T('↓'))
					memDC.SetTextColor(COLOR_GREEN_DOWN);
				else
					memDC.SetTextColor(RGB(0, 0, 0));
				int trendX = avgAreaX - GAP - memDC.GetTextExtent(trendArrowStr).cx;
				memDC.TextOut(trendX, avgAreaY + g_data.RDPI(2), trendArrowStr);
			}

			// 最小值（白色文字）
			memDC.SetTextColor(RGB(255, 255, 255));
			int minX = avgAreaX + (thirdWidth - memDC.GetTextExtent(minAvgValueStr).cx) / 2;
			memDC.TextOut(minX, avgAreaY + g_data.RDPI(2), minAvgValueStr);

			// 均值（白色文字）
			int avgX = avgAreaX + thirdWidth + (thirdWidth - memDC.GetTextExtent(avgValueStr).cx) / 2;
			memDC.TextOut(avgX, avgAreaY + g_data.RDPI(2), avgValueStr);

			// 最大值（白色文字）
			int maxX = avgAreaX + thirdWidth * 2 + (thirdWidth - memDC.GetTextExtent(maxAvgValueStr).cx) / 2;
			memDC.TextOut(maxX, avgAreaY + g_data.RDPI(2), maxAvgValueStr);

			// 恢复之前的字体
			memDC.SelectObject(pPrevFont);
		}
	}
	else
	{
		// 非关联模式（默认指数）：等分列宽布局
		const int colWidth = w / max(relatedCount, 1);
		for (int i = 0; i < relatedCount; i++)
		{
			auto stockData = g_data.GetStockData(relatedCodes[i]);
			int colX = i * colWidth;
			int textX = colX + GAP;

			if (stockData && stockData->info.is_ok)
			{
				const auto& info = stockData->info;
				double displayPrice = info.currentPrice > 0 ? info.currentPrice : info.prevClosePrice;
				double diff = displayPrice - info.prevClosePrice;
				double diffPercent = info.prevClosePrice != 0 ? (diff / info.prevClosePrice) * 100 : 0;

				CString nameStr = info.GetStockListName();
				CString priceStr;
				priceStr.Format(_T("%.2f"), displayPrice);
				CString changeStr;
				if (diff >= 0)
					changeStr.Format(_T("+%.2f%%"), diffPercent);
				else
					changeStr.Format(_T("%.2f%%"), diffPercent);

				memDC.SetTextColor(COLOR_BLACK);
				memDC.TextOut(textX, topBarY + g_data.RDPI(2), nameStr);
				textX += memDC.GetTextExtent(nameStr).cx + GAP;

				memDC.SetTextColor(CCommon::GetProfitLossColor(diffPercent));
				memDC.TextOut(textX, topBarY + g_data.RDPI(2), priceStr);
				textX += memDC.GetTextExtent(priceStr).cx + GAP;

				memDC.SetTextColor(CCommon::GetProfitLossColor(diffPercent));
				memDC.TextOut(textX, topBarY + g_data.RDPI(2), changeStr);
			}
			else
			{
				CString nameStr = CString(relatedCodes[i].c_str());
				memDC.SetTextColor(COLOR_GRAY_PURPLE);
				memDC.TextOut(textX, topBarY + g_data.RDPI(2), nameStr + _T(" --"));
			}
		}
	}

	// 恢复原始字体
	if (pOldFont != nullptr)
	{
		memDC.SelectObject(pOldFont);
		dynFont.DeleteObject();
	}
}

void CStatusBarPanel::DrawSystemStatusBar(CDC& memDC, int w, int bottomBarY, int singleBarHeight)
{
	const int GAP = 2;

	std::vector<std::wstring> statusBarCodes = g_data.GetStatusBarStockCodes();
	if (statusBarCodes.empty())
	{
		// 没有配置时使用默认指数：上证指数、中证银行、恒生科技
		statusBarCodes = { L"sh000001", L"sz399986", L"rt_hkHSTECH" };
	}
	const int sbCount = static_cast<int>(statusBarCodes.size());
	// 下行状态栏使用等分列宽布局
	const int colWidth = w / max(sbCount, 1);

	std::lock_guard<std::mutex> lock(Stock::Instance().m_stockDataMutex);
	for (int i = 0; i < sbCount; i++)
	{
		auto stockData = g_data.GetStockData(statusBarCodes[i]);
		int colX = i * colWidth;
		int textX = colX + GAP;

		if (stockData && stockData->info.is_ok)
		{
			const auto& info = stockData->info;
			double displayPrice = info.currentPrice > 0 ? info.currentPrice : info.prevClosePrice;
			double diff = displayPrice - info.prevClosePrice;
			double diffPercent = info.prevClosePrice != 0 ? (diff / info.prevClosePrice) * 100 : 0;

			CString nameStr = info.GetStockListName();
			CString priceStr;
			priceStr.Format(_T("%.2f"), displayPrice);
			CString changeStr;
			if (diff >= 0)
				changeStr.Format(_T("+%.2f%%"), diffPercent);
			else
				changeStr.Format(_T("%.2f%%"), diffPercent);

			memDC.SetTextColor(COLOR_BLACK);
			memDC.TextOut(textX, bottomBarY + g_data.RDPI(2), nameStr);
			textX += memDC.GetTextExtent(nameStr).cx + GAP;

			memDC.SetTextColor(CCommon::GetProfitLossColor(diffPercent));
			memDC.TextOut(textX, bottomBarY + g_data.RDPI(2), priceStr);
			textX += memDC.GetTextExtent(priceStr).cx + GAP;

			memDC.SetTextColor(CCommon::GetProfitLossColor(diffPercent));
			memDC.TextOut(textX, bottomBarY + g_data.RDPI(2), changeStr);
		}
		else
		{
			CString nameStr = CString(statusBarCodes[i].c_str());
			memDC.SetTextColor(COLOR_GRAY_PURPLE);
			memDC.TextOut(textX, bottomBarY + g_data.RDPI(2), nameStr + _T(" --"));
		}
	}
}

void CStatusBarPanel::DrawBottomStatusBar(CDC& memDC, int w, int h, int indexBarHeight, const std::wstring& stockId, int viewMode)
{
	const int singleBarHeight = g_data.RDPI(20);  // 单行状态栏高度
	const int topBarY = h - indexBarHeight;       // 上行状态栏Y坐标
	const int bottomBarY = topBarY + singleBarHeight;  // 下行状态栏Y坐标
	memDC.FillSolidRect(0, topBarY, w, indexBarHeight, RGB(240, 240, 240));
	memDC.SetBkMode(TRANSPARENT);

	// 绘制上行关联股票状态栏
	DrawRelatedStockBar(memDC, w, topBarY, singleBarHeight, stockId, viewMode);

	// 在关联股票状态栏右侧预留120像素均值区域中间绘制竖线分隔
	std::vector<std::wstring> relatedCodes = g_data.GetRelatedStocks(stockId);
	bool isRelatedMode = !relatedCodes.empty();
	if (isRelatedMode)
	{
		auto avgData = g_data.GetAvgDiffData(stockId);
		bool showAvgDiff = !(avgData.minVal == 0.0 && avgData.maxVal == 0.0 && avgData.currentVal == 0.0);
		if (showAvgDiff)
		{
			int avgAreaWidth = g_data.RDPI(120);
			int avgAreaX = w - avgAreaWidth - 2;
			int midX = avgAreaX + avgAreaWidth / 2;
			CPen pen(PS_SOLID, 1, RGB(180, 180, 180));
			CPen* pOldPen = memDC.SelectObject(&pen);
			memDC.MoveTo(midX, topBarY);
			memDC.LineTo(midX, topBarY + singleBarHeight);
			memDC.SelectObject(pOldPen);
			pen.DeleteObject();
		}
	}

	// 绘制下行系统状态栏
	DrawSystemStatusBar(memDC, w, bottomBarY, singleBarHeight);
}