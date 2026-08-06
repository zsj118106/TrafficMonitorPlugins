#include "pch.h"
#include "OrderBookPanel.h"
#include "ChartColors.h"
#include "Common.h"
#include "DataManager.h"
#include "SignalAnalyzer.h"
#include <algorithm>
#include <cmath>
#include <map>

// 静态成员初始化
const COLORREF COrderBookPanel::NET_RATIO_RED_COLORS[] = {
	RGB(240, 40, 40),   // 0-30
	RGB(180, 50, 50),   // 30-60
	RGB(130, 20, 40)    // 60以上
};
const COLORREF COrderBookPanel::NET_RATIO_GREEN_COLORS[] = {
	RGB(40, 240, 40),  // 0~30 浅亮绿（弱多）
	RGB(50, 180, 50),  // 30~60 中草绿（中多）
	RGB(20, 130, 40)   // 60以上 深墨绿（强多）
};
std::map<std::wstring, double> COrderBookPanel::m_lastNetRatioMap;
std::map<std::wstring, CString> COrderBookPanel::m_lastNetRatioTrendMap;
std::map<std::wstring, std::map<int, double>> COrderBookPanel::m_lastPeriodRatioMap;
std::map<std::wstring, std::map<int, CString>> COrderBookPanel::m_lastPeriodRatioTrendMap;

void COrderBookPanel::Draw(CDC& memDC, int left, int right, int height, const STOCK::StockInfo& stockInfo,
	const std::vector<STOCK::KLinePoint>& klineData,
	ChartViewMode viewMode)
{
	const int MAX_LEVEL = STOCK::StockInfo::MAX_LEVEL;
	// 布局：0=委比, 1=趋势, 2=最高, 3=卖三, 4=卖二, 5=卖一, 6=净比00, 7=买一, 8=买二, 9=买三, 10=最低, 11-14=净比01/05/10/20, 15=净比99, 16=振幅, 17=换手率
	const int totalRows = 18;
	const int headerHeight = g_data.RDPI(26);  // 主标题栏高度
	const int obTitleH = g_data.RDPI(16);       // 盘口标题栏高度，与走势图标题栏一致
	const int topOffset = headerHeight + obTitleH;  // 内容从主标题栏+盘口标题栏下方开始
	const int panelW = right - left;
	// 绘制盘口标题栏背景（在主标题栏下方）
	memDC.FillSolidRect(left, headerHeight, panelW, obTitleH, RGB(245, 245, 245));
	const int rowHeight = (height - obTitleH) / totalRows;  // 每行基础高度
	const int contentH = height - obTitleH;  // 内容区域总高度
	const int rem = contentH % totalRows;    // 余数：前rem行多1px
	const int textX = left + g_data.RDPI(5) + 3;

	// 填充内容区域背景，避免底部空白
	memDC.FillSolidRect(left, topOffset, panelW, contentH, RGB(250, 250, 250));
	memDC.SetBkMode(TRANSPARENT);

	// 初始化挂单量累加缓存
	m_stockDataForAccum = g_data.GetStockData(stockInfo.code);

	// 构建布局上下文
	LayoutContext lc;
	lc.left = left;
	lc.right = right;
	lc.height = height;
	lc.headerHeight = headerHeight;
	lc.obTitleH = obTitleH;
	lc.topOffset = topOffset;
	lc.panelW = panelW;
	lc.totalRows = totalRows;
	lc.rowHeight = rowHeight;
	lc.contentH = contentH;
	lc.rem = rem;
	lc.textX = textX;

	DWORD tickCount = GetTickCount();
	bool blinkOn = (tickCount / 500) % 2 == 0;  // 每500ms切换

	// 依次绘制各区域
	DrawWeiBi(memDC, lc, stockInfo);
	DrawTrend(memDC, lc, stockInfo, viewMode);
	DrawAskRows(memDC, lc, stockInfo, blinkOn);
	DrawNetRatio00(memDC, lc, stockInfo);
	DrawBidRows(memDC, lc, stockInfo, blinkOn);
	DrawNetRatioPeriods(memDC, lc, stockInfo);
	DrawNetRatio99(memDC, lc, stockInfo);
	DrawAmplitude(memDC, lc, stockInfo, klineData);
	DrawTurnoverRate(memDC, lc, stockInfo);
}

// ============================================================================
// 绘制委比（行0）
// ============================================================================
void COrderBookPanel::DrawWeiBi(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo)
{
	const int MAX_LEVEL = STOCK::StockInfo::MAX_LEVEL;
	STOCK::Volume bidTotal = 0;
	STOCK::Volume askTotal = 0;
	for (int i = 0; i < MAX_LEVEL; i++)
	{
		bidTotal += stockInfo.bidLevels[i].volume / 100;
		askTotal += stockInfo.askLevels[i].volume / 100;
	}

	double wbRatio = 0.0;
	if (bidTotal + askTotal > 0)
	{
		wbRatio = (double)(bidTotal - askTotal) / (bidTotal + askTotal) * 100;
	}

	CString wbLabel = _T("委  比:");
	int wbBarY = lc.RowY(0);
	int wbBarH = lc.RowH(0);
	memDC.SetTextColor(wbRatio > 0 ? COLOR_RED_UP : (wbRatio < 0 ? COLOR_GREEN_DOWN : COLOR_BLACK));
	memDC.TextOut(lc.textX, wbBarY + max(0, (wbBarH - memDC.GetTextExtent(wbLabel).cy) / 2), wbLabel);
	int wbBarX = lc.textX + memDC.GetTextExtent(wbLabel).cx + g_data.RDPI(4);
	int wbBarW = lc.right - wbBarX - g_data.RDPI(4);
	DrawRatioBar(memDC, wbBarX, wbBarY, wbBarW, wbBarH, wbRatio, bidTotal - askTotal);
	CString wbTxt;
	wbTxt.Format(_T("%.2f"), std::abs(wbRatio));
	DrawNetRatioBarText(memDC, wbBarX, wbBarY, wbBarW, wbBarH, wbTxt, _T(""));
}

// ============================================================================
// 绘制趋势判定（行1）
// ============================================================================
void COrderBookPanel::DrawTrend(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo,
	ChartViewMode viewMode)
{
	auto stockDataForTrend = g_data.GetStockData(stockInfo.code);

	// 分别计算三个周期的趋势方向
	STOCK::TrendDir dir30 = STOCK::TrendDir::DIR_SIDE;
	STOCK::TrendDir dir5 = STOCK::TrendDir::DIR_SIDE;
	STOCK::TrendDir dirCur = STOCK::TrendDir::DIR_SIDE;
	bool curIsShortPullback = false;
	bool curIsShortRebound = false;
	STOCK::SideTag curSideTag = STOCK::SideTag::SIDE_MID;
	bool valid30 = false, valid5 = false, validCur = false;

	if (stockDataForTrend)
	{
		// 30分钟趋势
		auto* min30Obj = stockDataForTrend->getMin30KLineData();
		if (min30Obj && min30Obj->data.size() >= 25)
		{
			std::vector<STOCK::Bar> bars30;
			bars30.reserve(min30Obj->data.size());
			for (const auto& kp : min30Obj->data) bars30.push_back(STOCK::Bar::FromKLinePoint(kp));
			if (CSignalAnalyzer::Calc30UpStruct(bars30))
				dir30 = STOCK::TrendDir::DIR_UP;
			else if (CSignalAnalyzer::Calc30DownStruct(bars30))
				dir30 = STOCK::TrendDir::DIR_DOWN;
			else
				dir30 = STOCK::TrendDir::DIR_SIDE;
			valid30 = true;
		}

		// 5分钟趋势
		auto* min5Obj = stockDataForTrend->getMin5KLineData();
		if (min5Obj && min5Obj->data.size() >= 20)
		{
			std::vector<STOCK::Bar> bars5;
			bars5.reserve(min5Obj->data.size());
			for (const auto& kp : min5Obj->data) bars5.push_back(STOCK::Bar::FromKLinePoint(kp));
			if (CSignalAnalyzer::Calc5MinUp(bars5))
				dir5 = STOCK::TrendDir::DIR_UP;
			else if (CSignalAnalyzer::Calc5MinDown(bars5))
				dir5 = STOCK::TrendDir::DIR_DOWN;
			else
				dir5 = STOCK::TrendDir::DIR_SIDE;
			valid5 = true;
		}

		// 当前视图趋势
		if (viewMode == CHART_VIEW_MIN30_KLINE)
		{
			if (valid30)
			{
				dirCur = dir30;
				std::vector<STOCK::Bar> bars30;
				bars30.reserve(min30Obj->data.size());
				for (const auto& kp : min30Obj->data) bars30.push_back(STOCK::Bar::FromKLinePoint(kp));
				STOCK::TrendState30m state30 = CSignalAnalyzer::Get30mTrendState(bars30).state;
				if (dirCur == STOCK::TrendDir::DIR_SIDE)
				{
					if (state30 == STOCK::TrendState30m::STATE_STRONG)
						curSideTag = STOCK::SideTag::SIDE_LONG_POINT;
					else if (state30 == STOCK::TrendState30m::STATE_WEAK || state30 == STOCK::TrendState30m::STATE_WEAK_SHAKE)
						curSideTag = STOCK::SideTag::SIDE_SHORT_POINT;
				}
				validCur = true;
			}
		}
		else if (viewMode == CHART_VIEW_MIN5_KLINE)
		{
			if (valid5)
			{
				dirCur = dir5;
				validCur = true;
			}
		}
		else if (viewMode == CHART_VIEW_TIMELINE)
		{
			auto* tlObj = stockDataForTrend->getTimelineData();
			if (tlObj && tlObj->data.size() >= 10)
			{
				const auto& pts = tlObj->data;
				const auto& last = pts.back();
				double curPrice = last.price;
				double avgPrice = last.averagePrice;
				size_t n = pts.size();
				size_t half = n / 2;
				double firstHalfAvg = 0, secondHalfAvg = 0;
				size_t firstCnt = 0, secondCnt = 0;
				for (size_t i = 0; i < half && i < n; ++i) { firstHalfAvg += pts[i].price; ++firstCnt; }
				for (size_t i = half; i < n; ++i) { secondHalfAvg += pts[i].price; ++secondCnt; }
				if (firstCnt > 0) firstHalfAvg /= firstCnt;
				if (secondCnt > 0) secondHalfAvg /= secondCnt;
				bool priceUpTrend = (secondHalfAvg > firstHalfAvg) && (curPrice >= avgPrice);
				bool priceDownTrend = (secondHalfAvg < firstHalfAvg) && (curPrice <= avgPrice);
				if (priceUpTrend)
					dirCur = STOCK::TrendDir::DIR_UP;
				else if (priceDownTrend)
					dirCur = STOCK::TrendDir::DIR_DOWN;
				else
					dirCur = STOCK::TrendDir::DIR_SIDE;
				validCur = true;
			}
		}
		else  // CHART_VIEW_DAY_KLINE
		{
			// 日K视图：双周期综合判定
			if (valid5 && valid30)
			{
				std::vector<STOCK::Bar> bars5, bars30;
				bars5.reserve(min5Obj->data.size());
				for (const auto& kp : min5Obj->data) bars5.push_back(STOCK::Bar::FromKLinePoint(kp));
				bars30.reserve(min30Obj->data.size());
				for (const auto& kp : min30Obj->data) bars30.push_back(STOCK::Bar::FromKLinePoint(kp));
				STOCK::Volume outerVolTrend = stockInfo.outerVolume;
				STOCK::Volume innerVolTrend = stockInfo.innerVolume;
				STOCK::TrendResult trendResult = CSignalAnalyzer::CalcTrend(bars5, bars30, outerVolTrend, innerVolTrend);
				dirCur = trendResult.FinalTrend;
				curIsShortPullback = trendResult.IsShortPullback;
				curIsShortRebound = trendResult.IsShortRebound;
				curSideTag = trendResult.SideTagValue;
				validCur = true;
			}
		}
	}

	// 构建分段文本：趋势:上涨(30) 震荡(5) 低吸
	struct TextSeg { CString text; COLORREF color; };
	std::vector<TextSeg> segs;

	// 30分钟段
	if (!valid30)
		segs.push_back({ _T("30:--"), COLOR_GRAY_TEXT });
	else if (dir30 == STOCK::TrendDir::DIR_UP)
		segs.push_back({ _T("30:上涨"), COLOR_RED_UP });
	else if (dir30 == STOCK::TrendDir::DIR_DOWN)
		segs.push_back({ _T("30:下跌"), COLOR_GREEN_DOWN });
	else
		segs.push_back({ _T("30:震荡"), COLOR_GRAY_TEXT });
	segs.push_back({ _T(" "), COLOR_GRAY_TEXT });

	// 5分钟段
	if (!valid5)
		segs.push_back({ _T("5:--"), COLOR_GRAY_TEXT });
	else if (dir5 == STOCK::TrendDir::DIR_UP)
		segs.push_back({ _T("5:上涨"), COLOR_RED_UP });
	else if (dir5 == STOCK::TrendDir::DIR_DOWN)
		segs.push_back({ _T("5:下跌"), COLOR_GREEN_DOWN });
	else
		segs.push_back({ _T("5:震荡"), COLOR_GRAY_TEXT });
	segs.push_back({ _T(" "), COLOR_GRAY_TEXT });

	// 当前视图段
	if (!validCur)
	{
		segs.push_back({ _T("--"), COLOR_GRAY_TEXT });
	}
	else if (dirCur == STOCK::TrendDir::DIR_UP)
	{
		CString s = _T("上涨");
		if (curIsShortPullback) s += _T("(回调)");
		segs.push_back({ s, COLOR_RED_UP });
	}
	else if (dirCur == STOCK::TrendDir::DIR_DOWN)
	{
		CString s = _T("下跌");
		if (curIsShortRebound) s += _T("(反弹)");
		segs.push_back({ s, COLOR_GREEN_DOWN });
	}
	else
	{
		CString s = _T("震荡");
		if (curSideTag == STOCK::SideTag::SIDE_LONG_POINT)
			s += _T("(低吸)");
		else if (curSideTag == STOCK::SideTag::SIDE_SHORT_POINT)
			s += _T("(高抛)");
		segs.push_back({ s, COLOR_GRAY_TEXT });
	}

	// 分段着色绘制
	int drawX = lc.textX;
	int drawY = lc.RowY(1) + max(0, (lc.RowH(1) - memDC.GetTextExtent(_T("Ay")).cy) / 2);
	for (const auto& seg : segs)
	{
		memDC.SetTextColor(seg.color);
		memDC.TextOut(drawX, drawY, seg.text);
		drawX += memDC.GetTextExtent(seg.text).cx;
	}
}

// ============================================================================
// 绘制卖盘行（最高+卖三~卖一，行2-5）
// ============================================================================
void COrderBookPanel::DrawAskRows(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo, bool blinkOn)
{
	std::vector<OrderBookRow> priceRows;
	priceRows.reserve(4);  // 最高+卖三~卖一

	// 最高价（固定在卖三上面）
	{
		CString highTxt;
		double highDiff = stockInfo.highPrice - stockInfo.currentPrice;
		if (highDiff >= 0)
			highTxt.Format(_T("最高: %s +%s"), CCommon::FormatFloat(stockInfo.highPrice), CCommon::FormatFloat(highDiff));
		else
			highTxt.Format(_T("最高: %s %s"), CCommon::FormatFloat(stockInfo.highPrice), CCommon::FormatFloat(highDiff));

		OrderBookRow row;
		row.price = stockInfo.highPrice;
		row.text = highTxt;
		row.textColor = RGB(128, 0, 128);
		row.fillBackground = true;
		row.backgroundColor = RGB(220, 235, 250);
		priceRows.push_back(row);
	}

	// 卖三~卖一
	for (int idx = 2; idx >= 0; --idx)
	{
		STOCK::Price price = stockInfo.askLevels[idx].price;
		STOCK::Volume delta = GetOrderDeltaLots(price);
		priceRows.push_back(BuildAskRow(stockInfo, idx, delta));
	}

	DrawPriceRows(memDC, lc, priceRows, 2, blinkOn);
}

// ============================================================================
// 绘制净比00柱状图（行6）
// ============================================================================
void COrderBookPanel::DrawNetRatio00(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo)
{
	auto stockDataPtr00 = g_data.GetStockData(stockInfo.code);
	int row6H = lc.RowH(6);

	int periodBarX = lc.textX;
	int periodBarW = lc.right - periodBarX - g_data.RDPI(4);

	if (periodBarW > 0 && stockDataPtr00)
	{
		std::vector<double> recentRatios;
		const int barCount = 12;
		if (stockDataPtr00->GetRecentNetRatios(barCount, recentRatios))
		{
			int baseSlotW = periodBarW / barCount;
			int remSlotW = periodBarW % barCount;
			int barPad = max(1, baseSlotW / 8);
			int slotX = periodBarX;
			for (int i = 0; i < barCount; i++)
			{
				int curSlotW = baseSlotW + (i < remSlotW ? 1 : 0);
				int curBarX = slotX + barPad;
				int curBarW = curSlotW - barPad * 2;
				if (curBarW < 1) curBarW = 1;

				double r = recentRatios[i];
				COLORREF barColor;
				if (r > 0) barColor = COLOR_RED_UP;
				else if (r < 0) barColor = COLOR_GREEN_DOWN;
				else barColor = COLOR_BLACK;

				int maxBarH = row6H - g_data.RDPI(4);
				int barH = static_cast<int>((std::min)(std::sqrt(std::abs(r) / 100.0), 1.0) * maxBarH);
				if (barH < 2) barH = 2;

				int barY = lc.RowY(6) + (row6H - barH) / 2;
				memDC.FillSolidRect(curBarX, barY, curBarW, barH, barColor);
				slotX += curSlotW;
			}
		}
	}
}

// ============================================================================
// 绘制买盘行（买一~买三+最低（行7-10）
// ============================================================================
void COrderBookPanel::DrawBidRows(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo, bool blinkOn)
{
	std::vector<OrderBookRow> bottomRows;
	bottomRows.reserve(4);  // 买一~买三+最低

	// 买一~买三
	for (int i = 0; i < 3; i++)
	{
		STOCK::Price price = stockInfo.bidLevels[i].price;
		STOCK::Volume delta = GetOrderDeltaLots(price);
		bottomRows.push_back(BuildBidRow(stockInfo, i, delta));
	}

	// 最低价（固定在买三下面）
	{
		CString lowTxt;
		double lowDiff = stockInfo.lowPrice - stockInfo.currentPrice;
		if (lowDiff >= 0)
			lowTxt.Format(_T("最低: %s +%s"), CCommon::FormatFloat(stockInfo.lowPrice), CCommon::FormatFloat(lowDiff));
		else
			lowTxt.Format(_T("最低: %s %s"), CCommon::FormatFloat(stockInfo.lowPrice), CCommon::FormatFloat(lowDiff));

		OrderBookRow row;
		row.price = stockInfo.lowPrice;
		row.text = lowTxt;
		row.textColor = RGB(0, 100, 0);
		row.fillBackground = true;
		row.backgroundColor = RGB(220, 235, 250);
		bottomRows.push_back(row);
	}

	DrawPriceRows(memDC, lc, bottomRows, 7, blinkOn);
}

// ============================================================================
// 绘制净比01/05/10/20（行11-14）
// ============================================================================
void COrderBookPanel::DrawNetRatioPeriods(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo)
{
	auto stockId = stockInfo.code;
	auto stockDataPtr = g_data.GetStockData(stockInfo.code);
	const int netPeriods[] = { 1, 5, 10, 20 };

	for (int i = 0; i < 4; i++)
	{
		int periodBarY = lc.RowY(11 + i);
		int periodBarH = lc.rowHeight;
		CString periodLabel;
		periodLabel.Format(_T("净比%02d:"), netPeriods[i]);
		COLORREF periodLabelColor = COLOR_BLACK;
		STOCK::Volume diff = 0;
		double ratio = 0;
		bool hasData = stockDataPtr && stockDataPtr->GetInnerOuterNetDiff(netPeriods[i], diff, ratio);
		if (hasData)
			periodLabelColor = diff > 0 ? COLOR_RED_UP : (diff < 0 ? COLOR_GREEN_DOWN : COLOR_BLACK);
		memDC.SetTextColor(periodLabelColor);
		memDC.TextOut(lc.textX, periodBarY + max(0, (periodBarH - memDC.GetTextExtent(periodLabel).cy) / 2), periodLabel);

		int periodBarX = lc.textX + memDC.GetTextExtent(periodLabel).cx + g_data.RDPI(4);
		int periodBarW = lc.right - periodBarX - g_data.RDPI(4);
		if (periodBarW <= 0)
			continue;

		DrawRatioBar(memDC, periodBarX, periodBarY, periodBarW, periodBarH, ratio, diff);

		CString periodTxt;
		CString periodDiffTxt;
		if (hasData)
		{
			CString diffStr = CCommon::FormatVolumeInt(std::abs(diff) / 100.0);
			CString ratioTrend;

			// 计算趋势箭头
			auto stockPeriodRatioIt = m_lastPeriodRatioMap.find(stockId);
			if (stockPeriodRatioIt != m_lastPeriodRatioMap.end())
			{
				auto lastRatioIt = stockPeriodRatioIt->second.find(netPeriods[i]);
				if (lastRatioIt != stockPeriodRatioIt->second.end())
				{
					double absRatio = std::abs(ratio);
					double lastAbsRatio = std::abs(lastRatioIt->second);
					if (absRatio > lastAbsRatio)
					{
						ratioTrend = _T("↑");
						m_lastPeriodRatioMap[stockId][netPeriods[i]] = ratio;
						m_lastPeriodRatioTrendMap[stockId][netPeriods[i]] = ratioTrend;
					}
					else if (absRatio < lastAbsRatio)
					{
						ratioTrend = _T("↓");
						m_lastPeriodRatioMap[stockId][netPeriods[i]] = ratio;
						m_lastPeriodRatioTrendMap[stockId][netPeriods[i]] = ratioTrend;
					}
					else
					{
						auto stockPeriodTrendIt = m_lastPeriodRatioTrendMap.find(stockId);
						if (stockPeriodTrendIt != m_lastPeriodRatioTrendMap.end())
						{
							auto lastTrendIt = stockPeriodTrendIt->second.find(netPeriods[i]);
							if (lastTrendIt != stockPeriodTrendIt->second.end())
								ratioTrend = lastTrendIt->second;
						}
					}
				}
				else
				{
					STOCK::Volume previousDiff = 0;
					double previousRatio = 0;
					if (stockDataPtr->GetPreviousInnerOuterNetDiff(netPeriods[i], previousDiff, previousRatio))
					{
						ratioTrend = CalcNetRatioTrend(ratio, previousRatio);
						// 存储到period专用map
						if (!ratioTrend.IsEmpty())
							m_lastPeriodRatioTrendMap[stockId][netPeriods[i]] = ratioTrend;
					}
					m_lastPeriodRatioMap[stockId][netPeriods[i]] = ratio;
				}
			}
			else
			{
				STOCK::Volume previousDiff = 0;
				double previousRatio = 0;
				if (stockDataPtr->GetPreviousInnerOuterNetDiff(netPeriods[i], previousDiff, previousRatio))
				{
					ratioTrend = CalcNetRatioTrend(ratio, previousRatio);
					if (!ratioTrend.IsEmpty())
						m_lastPeriodRatioTrendMap[stockId][netPeriods[i]] = ratioTrend;
				}
				m_lastPeriodRatioMap[stockId][netPeriods[i]] = ratio;
			}

			CString diffSign = diff >= 0 ? _T("+") : _T("-");
			periodTxt.Format(_T("%.2f%s"), std::abs(ratio), ratioTrend.GetString());
			periodDiffTxt.Format(_T("%s%s"), diffSign.GetString(), diffStr.GetString());
		}
		else
		{
			periodTxt = _T("--");
			periodDiffTxt = _T("--");
		}

		DrawNetRatioBarText(memDC, periodBarX, periodBarY, periodBarW, periodBarH, periodTxt, periodDiffTxt);
	}
}

// ============================================================================
// 绘制净比99（行15）
// ============================================================================
void COrderBookPanel::DrawNetRatio99(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo)
{
	auto stockId = stockInfo.code;
	STOCK::Volume innerVol = stockInfo.innerVolume / 100;
	STOCK::Volume outerVol = stockInfo.outerVolume / 100;
	auto stockDataPtr = g_data.GetStockData(stockId);

	STOCK::Volume netDiff = outerVol - innerVol;
	STOCK::Volume totalInnerOuter = outerVol + innerVol;
	double netRatio = totalInnerOuter > 0 ? static_cast<double>(netDiff) / totalInnerOuter * 100 : 0;
	double absNetRatio = std::abs(netRatio);

	// 计算趋势箭头
	CString netRatioTrend;
	auto lastNetRatioIt = m_lastNetRatioMap.find(stockId);
	if (lastNetRatioIt != m_lastNetRatioMap.end())
	{
		double lastAbsNetRatio = std::abs(lastNetRatioIt->second);
		if (absNetRatio > lastAbsNetRatio)
		{
			netRatioTrend = _T("↑");
			m_lastNetRatioMap[stockId] = netRatio;
			m_lastNetRatioTrendMap[stockId] = netRatioTrend;
		}
		else if (absNetRatio < lastAbsNetRatio)
		{
			netRatioTrend = _T("↓");
			m_lastNetRatioMap[stockId] = netRatio;
			m_lastNetRatioTrendMap[stockId] = netRatioTrend;
		}
		else
		{
			auto lastTrendIt = m_lastNetRatioTrendMap.find(stockId);
			if (lastTrendIt != m_lastNetRatioTrendMap.end())
				netRatioTrend = lastTrendIt->second;
		}
	}
	else
	{
		double previousRatio = 0;
		if (stockDataPtr && stockDataPtr->GetPreviousInnerOuterTotalRatio(previousRatio))
		{
			netRatioTrend = CalcNetRatioTrend(netRatio, previousRatio);
			if (!netRatioTrend.IsEmpty())
				m_lastNetRatioTrendMap[stockId] = netRatioTrend;
		}
		m_lastNetRatioMap[stockId] = netRatio;
	}

	CString netDiffStr = CCommon::FormatVolumeInt(std::abs(netDiff));

	int barY = lc.RowY(15);
	int barH = lc.rowHeight;
	CString netRatioLabel = _T("净比99:");
	memDC.SetTextColor(netDiff > 0 ? COLOR_RED_UP : (netDiff < 0 ? COLOR_GREEN_DOWN : COLOR_BLACK));
	memDC.TextOut(lc.textX, barY + max(0, (barH - memDC.GetTextExtent(netRatioLabel).cy) / 2), netRatioLabel);
	int barX = lc.textX + memDC.GetTextExtent(netRatioLabel).cx + g_data.RDPI(4);
	int barW = lc.right - barX - g_data.RDPI(4);
	if (barW > 0)
	{
		DrawRatioBar(memDC, barX, barY, barW, barH, netRatio, netDiff);

		CString diffSign = netDiff >= 0 ? _T("+") : _T("-");
		CString netRatioTxt;
		netRatioTxt.Format(_T("%.2f%s"), std::abs(netRatio), netRatioTrend.GetString());
		CString netDiffTxt;
		netDiffTxt.Format(_T("%s%s"), diffSign.GetString(), netDiffStr.GetString());
		DrawNetRatioBarText(memDC, barX, barY, barW, barH, netRatioTxt, netDiffTxt);
	}
}

// ============================================================================
// 绘制振幅（行16）
// ============================================================================
void COrderBookPanel::DrawAmplitude(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo,
	const std::vector<STOCK::KLinePoint>& klineData)
{
	if (!klineData.empty())
	{
		int textXAmp = lc.left + g_data.RDPI(5) + 3;

		CString ampTxt;
		float fluctuation = stockInfo.highPrice - stockInfo.lowPrice;
		float fluctuationPercent = stockInfo.prevClosePrice != 0 ? (fluctuation / stockInfo.prevClosePrice) * 100 : 0;

		auto stockDataPtr2 = g_data.GetStockData(stockInfo.code);
		auto* klinePtr2 = stockDataPtr2 ? stockDataPtr2->getKLineData() : nullptr;
		double avgAmplitude5 = klinePtr2 ? klinePtr2->CalculateAverageAmplitude(5) : 0;
		CString amp5Str;
		if (avgAmplitude5 > 0)
			amp5Str.Format(_T("%.2f%%"), avgAmplitude5);
		else
			amp5Str = _T("--");
		ampTxt.Format(_T("振幅: 01:%.2f%% 05:%s"), fluctuationPercent, amp5Str.GetString());
		memDC.SetTextColor(COLOR_BLACK);
		memDC.TextOut(textXAmp, lc.RowY(16) + max(0, (lc.RowH(16) - memDC.GetTextExtent(ampTxt).cy) / 2), ampTxt);
	}
}

// ============================================================================
// 绘制换手率（行17）
// ============================================================================
void COrderBookPanel::DrawTurnoverRate(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo)
{
	CString turnoverTxt;
	turnoverTxt.Format(_T("换手率: %.2f%%"), stockInfo.turnoverRate);
	if (stockInfo.turnoverRate >= 5)
		memDC.SetTextColor(COLOR_RED_UP);
	else
		memDC.SetTextColor(COLOR_GRAY_TEXT);
	memDC.TextOut(lc.textX, lc.RowY(17) + max(0, (lc.RowH(17) - memDC.GetTextExtent(turnoverTxt).cy) / 2), turnoverTxt);
}

// ============================================================================
// 辅助函数
// ============================================================================

void COrderBookPanel::DrawOrderBookRowText(CDC& memDC, const OrderBookRow& row, int x, int y, int rowWidth, bool blinkOff)
{
	COLORREF textColor;
	if (row.darkBackground && !(row.blink && blinkOff))
		textColor = RGB(255, 255, 255);
	else
		textColor = row.textColor;
	memDC.SetTextColor(textColor);
	memDC.TextOut(x, y, row.text);
	if (!row.drawSmallSuffix || row.smallSuffix.IsEmpty())
	{
		// 只有右对齐后缀
		if (!row.rightAlignSuffix.IsEmpty())
		{
			CFont* oldFont = memDC.GetCurrentFont();
			LOGFONT lf;
			oldFont->GetLogFont(&lf);
			lf.lfHeight = lf.lfHeight * 7 / 8;
			CFont smallFont;
			smallFont.CreateFontIndirect(&lf);
			memDC.SelectObject(&smallFont);
			memDC.SetTextColor(row.rightAlignSuffixColor);
			int suffixW = memDC.GetTextExtent(row.rightAlignSuffix).cx;
			memDC.TextOut(x + rowWidth - suffixW - g_data.RDPI(4), y + g_data.RDPI(1), row.rightAlignSuffix);
			memDC.SelectObject(oldFont);
		}
		return;
	}

	int suffixX = x + memDC.GetTextExtent(row.text).cx;
	CFont* oldFont = memDC.GetCurrentFont();
	LOGFONT lf;
	oldFont->GetLogFont(&lf);
	lf.lfHeight = lf.lfHeight * 7 / 8;
	CFont smallFont;
	smallFont.CreateFontIndirect(&lf);
	memDC.SelectObject(&smallFont);
	memDC.SetTextColor(row.darkBackground ? RGB(255, 255, 200) : textColor);
	memDC.TextOut(suffixX, y + g_data.RDPI(1), row.smallSuffix);
	// 右对齐绘制瞬时变化量
	if (!row.rightAlignSuffix.IsEmpty())
	{
		memDC.SetTextColor(row.rightAlignSuffixColor);
		int raSuffixW = memDC.GetTextExtent(row.rightAlignSuffix).cx;
		memDC.TextOut(x + rowWidth - raSuffixW - g_data.RDPI(4), y + g_data.RDPI(1), row.rightAlignSuffix);
	}
	memDC.SelectObject(oldFont);
}

void COrderBookPanel::DrawRatioBar(CDC& memDC, int x, int y, int w, int h, double ratio, STOCK::Volume diff)
{
	if (w <= 0)
		return;
	COLORREF redColor = NET_RATIO_RED_COLORS[GetNetRatioColorIndex(ratio)];
	COLORREF greenColor = NET_RATIO_GREEN_COLORS[GetNetRatioColorIndex(ratio)];
	int midX = x + w / 2;
	int halfW = w / 2;
	int fillW = static_cast<int>(std::sqrt(std::abs(ratio) / 100.0) * halfW);
	fillW = min(fillW, halfW);
	memDC.FillSolidRect(x, y, w, h, RGB(230, 230, 230));
	int dominantW = min(w, halfW + fillW);
	if (diff > 0)
	{
		memDC.FillSolidRect(x, y, dominantW, h, redColor);
		memDC.FillSolidRect(x + dominantW, y, w - dominantW, h, greenColor);
	}
	else if (diff < 0)
	{
		memDC.FillSolidRect(x, y, dominantW, h, greenColor);
		memDC.FillSolidRect(x + dominantW, y, w - dominantW, h, redColor);
	}
	memDC.FillSolidRect(midX - 1, y, 2, h, RGB(180, 180, 180));
	CPen borderPen(PS_SOLID, 1, RGB(255, 255, 255));
	CPen* oldPen = memDC.SelectObject(&borderPen);
	CBrush* oldBrush = static_cast<CBrush*>(memDC.SelectStockObject(NULL_BRUSH));
	memDC.Rectangle(x, y, x + w, y + h);
	memDC.SelectObject(oldBrush);
	memDC.SelectObject(oldPen);
}

void COrderBookPanel::DrawNetRatioBarText(CDC& memDC, int x, int y, int w, int h, const CString& ratioText, const CString& diffText)
{
	CFont* oldFont = memDC.GetCurrentFont();
	LOGFONT lf;
	oldFont->GetLogFont(&lf);
	lf.lfHeight = lf.lfHeight * 27 / 32;
	CFont smallFont;
	smallFont.CreateFontIndirect(&lf);
	memDC.SelectObject(&smallFont);
	memDC.SetTextColor(RGB(255, 255, 255));
	int vCenter = max(0, (h - memDC.GetTextExtent(ratioText).cy) / 2);
	memDC.TextOut(x + g_data.RDPI(3), y + vCenter, ratioText);
	CSize diffSize = memDC.GetTextExtent(diffText);
	memDC.TextOut(x + w - diffSize.cx - g_data.RDPI(3), y + vCenter, diffText);
	memDC.SelectObject(oldFont);
}

int COrderBookPanel::GetNetRatioColorIndex(double ratio)
{
	double absRatioValue = std::abs(ratio);
	if (absRatioValue <= 30) return 0;
	if (absRatioValue <= 60) return 1;
	return 2;
}

STOCK::Volume COrderBookPanel::GetOrderDeltaLots(STOCK::Price price)
{
	if (!m_stockDataForAccum || price <= 0)
		return 0;
	auto it = m_stockDataForAccum->orderPriceAccumMap.find(price);
	if (it == m_stockDataForAccum->orderPriceAccumMap.end())
		return 0;
	return it->second.deltaVolume / 100;
}

CString COrderBookPanel::CalcNetRatioTrend(double ratio, double previousRatio)
{
	double absRatio = std::abs(ratio);
	double previousAbsRatio = std::abs(previousRatio);
	if (absRatio > previousAbsRatio)
		return _T("↑");
	else if (absRatio < previousAbsRatio)
		return _T("↓");
	return _T("");
}

COrderBookPanel::OrderBookRow COrderBookPanel::BuildAskRow(const STOCK::StockInfo& stockInfo, int idx, STOCK::Volume delta) const
{
	STOCK::Price price = stockInfo.askLevels[idx].price;
	STOCK::Volume volume = stockInfo.askLevels[idx].volume / 100;
	CString volumeStr;
	volumeStr.Format(_T("%lld"), static_cast<long long>(volume));
	CString priceStr = stockInfo.IsETF() ? CCommon::FormatETFPrice(price) : CCommon::FormatFloat(price);
	CString askTxt;
	askTxt.Format(_T("S%d:%s"), idx + 1, priceStr);
	CString askSuffix;
	askSuffix.Format(_T(" %s"), volumeStr.GetString());
	CString deltaStr;
	if (delta != 0)
	{
		CString deltaVal;
		deltaVal.Format(_T("%lld"), static_cast<long long>(std::abs(delta)));
		deltaStr.Format(_T("%s%s"), delta > 0 ? _T("+") : _T("-"), deltaVal.GetString());
	}

	OrderBookRow row;
	row.price = price;
	row.text = askTxt;
	row.smallSuffix = askSuffix;
	row.rightAlignSuffix = deltaStr;
	row.rightAlignSuffixColor = delta > 0 ? COLOR_RED_UP : COLOR_GREEN_DOWN;
	row.drawSmallSuffix = true;
	row.textColor = COLOR_RED_UP;
	// 卖一背景色（仅当前价格=卖一时显示）
	if (idx == 0 && stockInfo.currentPrice > 0 && price > 0 && stockInfo.currentPrice == price)
	{
		row.fillBackground = true;
		row.backgroundColor = RGB(255, 200, 200);
		// 挂单量≤1万时闪烁
		if (volume <= 10000)
			row.blink = true;
	}
	else
	{
		row.fillBackground = (stockInfo.currentPrice > 0 && price > 0 && stockInfo.currentPrice == price);
		row.backgroundColor = RGB(255, 200, 200);
	}
	return row;
}

COrderBookPanel::OrderBookRow COrderBookPanel::BuildBidRow(const STOCK::StockInfo& stockInfo, int idx, STOCK::Volume delta) const
{
	STOCK::Price price = stockInfo.bidLevels[idx].price;
	STOCK::Volume volume = stockInfo.bidLevels[idx].volume / 100;
	CString volumeStr;
	volumeStr.Format(_T("%lld"), static_cast<long long>(volume));
	CString priceStr = stockInfo.IsETF() ? CCommon::FormatETFPrice(price) : CCommon::FormatFloat(price);
	CString bidTxt;
	bidTxt.Format(_T("B%d:%s"), idx + 1, priceStr);
	CString bidSuffix;
	bidSuffix.Format(_T(" %s"), volumeStr.GetString());
	CString deltaStr;
	if (delta != 0)
	{
		CString deltaVal;
		deltaVal.Format(_T("%lld"), static_cast<long long>(std::abs(delta)));
		deltaStr.Format(_T("%s%s"), delta > 0 ? _T("+") : _T("-"), deltaVal.GetString());
	}

	OrderBookRow row;
	row.price = price;
	row.text = bidTxt;
	row.smallSuffix = bidSuffix;
	row.rightAlignSuffix = deltaStr;
	row.rightAlignSuffixColor = delta > 0 ? COLOR_RED_UP : COLOR_GREEN_DOWN;
	row.drawSmallSuffix = true;
	row.textColor = COLOR_GREEN_DOWN;
	// 买一背景色（仅当前价格=买一时显示）
	if (idx == 0 && stockInfo.currentPrice > 0 && price > 0 && stockInfo.currentPrice == price)
	{
		row.fillBackground = true;
		row.backgroundColor = RGB(200, 255, 200);
		// 挂单量≤1万时闪烁
		if (volume <= 10000)
			row.blink = true;
	}
	else
	{
		row.fillBackground = (stockInfo.currentPrice > 0 && price > 0 && stockInfo.currentPrice == price);
		row.backgroundColor = RGB(200, 255, 200);
	}
	return row;
}

void COrderBookPanel::DrawPriceRows(CDC& memDC, const LayoutContext& lc, const std::vector<OrderBookRow>& rows, int startRow, bool blinkOn)
{
	for (int i = 0; i < static_cast<int>(rows.size()); i++)
	{
		int y = lc.RowY(startRow + i);
		int h = lc.RowH(startRow + i);
		if (rows[i].fillBackground)
		{
			if (rows[i].blink && !blinkOn)
				memDC.FillSolidRect(lc.left, y, lc.right - lc.left, h, RGB(250, 250, 250));  // 闪烁关闭时用背景色
			else
				memDC.FillSolidRect(lc.left, y, lc.right - lc.left, h, rows[i].backgroundColor);
		}
		int textVCenter = max(0, (h - memDC.GetTextExtent(rows[i].text).cy) / 2);
		DrawOrderBookRowText(memDC, rows[i], lc.textX, y + textVCenter, lc.right - lc.textX, rows[i].blink && !blinkOn);
	}
}