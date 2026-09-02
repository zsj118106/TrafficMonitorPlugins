#include "pch.h"
#include "OrderBookPanel.h"
#include "ChartColors.h"
#include "Common.h"
#include "DataManager.h"
#include "SignalAnalyzer.h"
#include <algorithm>
#include <cmath>
#include <map>

// 成交量颜色（按实际成交额档位分色）
const COLORREF VOL_COL_NORMAL_BID = RGB(255, 13, 0);  // 买方正常单·浅红 216, 68, 68
const COLORREF VOL_COL_BIG_BID = RGB(255, 0, 255);    // 买方大单·深红
const COLORREF VOL_COL_HUGE_BID = RGB(102, 0, 102);   // 买方超大单·紫

const COLORREF VOL_COL_NORMAL_ASK = RGB(47, 158, 68);  // 卖方正常单·浅绿
const COLORREF VOL_COL_BIG_ASK = RGB(0, 230, 0);    // 卖方大单·深绿
const COLORREF VOL_COL_HUGE_ASK = RGB(10, 80, 55);    // 卖方超大单·墨绿

// 静态成员初始化
const COLORREF COrderBookPanel::NET_RATIO_RED_COLORS[] = {
	RGB(240, 40, 40),   // 0-30
	RGB(180, 50, 50),   // 30-60
	RGB(130, 20, 40)    // 60以上#CCE8CF
};
const COLORREF COrderBookPanel::NET_RATIO_GREEN_COLORS[] = {
	RGB(40, 240, 40),  // 0~30 浅亮绿（弱多）
	RGB(50, 180, 50),  // 30~60 中草绿（中多）
	RGB(20, 130, 40)   // 60以上 深墨绿（强多）
};
std::map<std::wstring, double> COrderBookPanel::m_lastNetRatioMap;
std::map<std::wstring, CString> COrderBookPanel::m_lastNetRatioTrendMap;

std::map<std::wstring, std::map<int, CString>> COrderBookPanel::m_lastPeriodRatioTrendMap;

void COrderBookPanel::Draw(CDC& memDC, int left, int right, int height, const STOCK::StockInfo& stockInfo,
	const std::vector<STOCK::KLinePoint>& klineData,
	UIViewMode viewMode)
{
	// 精简版布局（23行）：
	// 0=最高/最低, 1-5=卖五~卖一, (选线分割不占行，在卖一与买一之间), 6-10=买一~买五,
	// 11=累计净流入, 12-21=10行成交明细, 22=区间净流入
	// 已去掉：委比、趋势、净比05/30/99、振幅、换手率
	const int totalRows = 23;
	const int headerHeight = g_data.RDPI(26) + g_data.RDPI(20);
	const int obTitleH = g_data.RDPI(16);
	const int topOffset = headerHeight + obTitleH;
	const int panelW = right - left;
	memDC.FillSolidRect(left, headerHeight, panelW, obTitleH, RGB(245, 245, 245));
	const int rowHeight = (height - obTitleH) / totalRows;
	const int contentH = height - obTitleH;
	const int rem = contentH % totalRows;
	const int textX = left + g_data.RDPI(5) + 3;

	memDC.FillSolidRect(left, topOffset, panelW, contentH, RGB(250, 250, 250));
	memDC.SetBkMode(TRANSPARENT);

	// 刷新累计成交量缓存（选线/累计净流入/明细共用同一份）、刷新明细并计算净流入
	RefreshPriceCumVol(stockInfo);
	RefreshTickMini(stockInfo, 10);

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

	// 行较密，整体使用紧凑字体，避免S5-B5/明细/净流入各行文字相互挤压
	CFont* oldDrawFont = memDC.GetCurrentFont();
	LOGFONT dlf;
	oldDrawFont->GetLogFont(&dlf);
	{
		int absH = abs(dlf.lfHeight);
		int idealH = max(8, static_cast<int>(rowHeight * 0.82));
		idealH = min(absH, idealH);
		dlf.lfHeight = (dlf.lfHeight < 0) ? -idealH : idealH;
	}
	CFont drawCompactFont;
	drawCompactFont.CreateFontIndirect(&dlf);
	memDC.SelectObject(&drawCompactFont);

	// 0: 最高/最低
	DrawHighLow(memDC, lc, stockInfo);

	// 1-5: 卖五~卖一
	{
		std::vector<OrderBookRow> askRows;
		askRows.reserve(5);
		for (int idx = 4; idx >= 0; --idx)
		{
			STOCK::Price price = stockInfo.askLevels[idx].price;
			// 仅卖一(idx==0)显示后方累计主动成交量的瞬时变化
			STOCK::Volume delta = (idx == 0) ? GetOrderDeltaLots(price, true) : 0;
			askRows.push_back(BuildAskRow(stockInfo, idx, delta));
		}
		DrawPriceRows(memDC, lc, askRows, 1);
	}

	// 6-10: 买一~买五
	{
		std::vector<OrderBookRow> bidRows;
		bidRows.reserve(5);
		for (int i = 0; i < 5; i++)
		{
			STOCK::Price price = stockInfo.bidLevels[i].price;
			// 仅买一(idx==0)显示后方累计主动成交量的瞬时变化
			STOCK::Volume delta = (i == 0) ? GetOrderDeltaLots(price, false) : 0;
			bidRows.push_back(BuildBidRow(stockInfo, i, delta));
		}
		DrawPriceRows(memDC, lc, bidRows, 6);
	}

	// 选线分割（净比00，画在卖一与买一之间，不单独占行）
	// 在买卖行绘制之后再画，避免被买一/卖一背景色盖住；2px线以两行边界为中线（上下各1px），居中于卖一与买一之间
	DrawNetRatio00Ex(memDC, lc, stockInfo, lc.RowY(6) - 1);

	// 11: 累计净流入（整日）
	DrawNetInflowRow(memDC, lc, 11, L"累计净流入", m_cumNetInflow);

	// 12-21: 10行成交明细（最新在下）
	DrawTickMini(memDC, lc, 12);

	// 22: 区间净流入（当前10条明细）
	DrawNetInflowRow(memDC, lc, 22, L"区间净流入", m_intervalNetInflow);

	memDC.SelectObject(oldDrawFont);

	// viewMode/klineData 仅用于旧完整版，精简版不再使用，避免未使用告警
	(void)viewMode; (void)klineData;
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
	DrawRatioBar(memDC, wbBarX, wbBarY, wbBarW, wbBarH, wbRatio);
	CString wbTxt;
	wbTxt.Format(_T("%.2f"), std::abs(wbRatio));
	DrawNetRatioBarText(memDC, wbBarX, wbBarY, wbBarW, wbBarH, wbTxt, _T(""));
}

// ============================================================================
// 绘制趋势判定（行1）
// 数据计算（趋势判定+分段文本）已抽离至 CSignalAnalyzer::CalcTrendSegments，
// 本函数仅负责把计算结果逐段渲染
// ============================================================================
void COrderBookPanel::DrawTrend(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo,
	UIViewMode viewMode)
{
	auto segs = CSignalAnalyzer::CalcTrendSegments(stockInfo);

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
// 绘制最高/最低合并行（行2）+ 卖盘行（卖五~卖一，行3-7）
// ============================================================================
void COrderBookPanel::DrawAskRows(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo)
{
	// 行2：最高/最低合并行
	{
		int rowY = lc.RowY(2);
		int rowH = lc.RowH(2);

		// 使用小字体
		CFont* oldFont = memDC.GetCurrentFont();
		LOGFONT lf;
		oldFont->GetLogFont(&lf);
		lf.lfHeight = lf.lfHeight * 7 / 8;
		CFont smallFont;
		smallFont.CreateFontIndirect(&lf);
		memDC.SelectObject(&smallFont);

		CString highPart, lowPart;
		double highDiff = stockInfo.highPrice - stockInfo.currentPrice;
		double lowDiff = stockInfo.lowPrice - stockInfo.currentPrice;
		CString highSign = highDiff >= 0 ? _T("+") : _T("");
		CString lowSign = lowDiff >= 0 ? _T("+") : _T("");
		highPart.Format(_T("H:%s %s%s"), CCommon::FormatFloat(stockInfo.highPrice), highSign.GetString(), CCommon::FormatFloat(highDiff));
		lowPart.Format(_T("L:%s %s%s"), CCommon::FormatFloat(stockInfo.lowPrice), lowSign.GetString(), CCommon::FormatFloat(lowDiff));

		memDC.FillSolidRect(lc.left, rowY, lc.panelW, rowH, RGB(220, 235, 250));
		int textY = rowY + max(0, (rowH - memDC.GetTextExtent(highPart).cy) / 2);
		memDC.SetTextColor(RGB(128, 0, 128));
		memDC.TextOut(lc.textX, textY, highPart);
		int lowW = memDC.GetTextExtent(lowPart).cx;
		int lowX = lc.left + lc.panelW - lowW;
		memDC.SetTextColor(RGB(0, 100, 0));
		memDC.TextOut(lowX, textY, lowPart);

		memDC.SelectObject(oldFont);
	}

	// 行3-7：卖五~卖一
	std::vector<OrderBookRow> priceRows;
	priceRows.reserve(5);

	for (int idx = 4; idx >= 0; --idx)
	{
		STOCK::Price price = stockInfo.askLevels[idx].price;
		// 仅卖一(idx==0)显示后方累计主动成交量的瞬时变化
		STOCK::Volume delta = (idx == 0) ? GetOrderDeltaLots(price, true) : 0;
		priceRows.push_back(BuildAskRow(stockInfo, idx, delta));
	}

	DrawPriceRows(memDC, lc, priceRows, 3);
}

// ============================================================================
// 绘制净比00 - 卖一与买一之间的分隔线（1分钟加权平均净比）
// ============================================================================
void COrderBookPanel::DrawNetRatio00(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo)
{
	auto stockDataPtr00 = g_data.GetStockData(stockInfo.code);
	if (!stockDataPtr00)
		return;

	// 从secVolumePool取1分钟加权平均净比
	STOCK::Volume diff = 0;
	double ratio = 0;
	if (!stockDataPtr00->GetSecNetDiff(1, diff, ratio))
		return;

	int barX = lc.textX;
	int barW = lc.right - barX - g_data.RDPI(4);
	if (barW <= 0)
		return;

	// 画在卖一(行7)和买一(行8)之间的间隙，不占独立行；2px线以边界为中线居中（上下各1px）
	int barY = lc.RowY(8) - 1;
	int midX = barX + barW / 2;
	int halfW = barW / 2;
	int fillW = static_cast<int>(std::sqrt(std::abs(ratio) / 100.0) * halfW);
	fillW = min(fillW, halfW);
	int dominantW = min(barW, halfW + fillW);

	COLORREF redColor = NET_RATIO_RED_COLORS[GetNetRatioColorIndex(ratio)];
	COLORREF greenColor = NET_RATIO_GREEN_COLORS[GetNetRatioColorIndex(ratio)];

	if (ratio > 0)
	{
		memDC.FillSolidRect(barX, barY, dominantW, 2, redColor);
		memDC.FillSolidRect(barX + dominantW, barY, barW - dominantW, 2, greenColor);
	}
	else if (ratio < 0)
	{
		memDC.FillSolidRect(barX, barY, dominantW, 2, greenColor);
		memDC.FillSolidRect(barX + dominantW, barY, barW - dominantW, 2, redColor);
	}
	else
	{
		memDC.FillSolidRect(barX, barY, barW, 2, RGB(230, 230, 230));
	}
	// 中间白色竖线
	memDC.FillSolidRect(midX - 1, barY, 2, 2, RGB(255, 255, 255));
}

// ============================================================================
// 精简版盘口（新版Draw）子项实现
// ============================================================================

void COrderBookPanel::DrawHighLow(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo)
{
	int rowY = lc.RowY(0);
	int rowH = lc.RowH(0);

	// 使用小字体
	CFont* oldFont = memDC.GetCurrentFont();
	LOGFONT lf;
	oldFont->GetLogFont(&lf);
	//lf.lfHeight = lf.lfHeight * 7 / 8;
	CFont smallFont;
	smallFont.CreateFontIndirect(&lf);
	memDC.SelectObject(&smallFont);

	CString highPart, lowPart;
	double highDiff = stockInfo.highPrice - stockInfo.currentPrice;
	double lowDiff = stockInfo.lowPrice - stockInfo.currentPrice;
	CString highSign = highDiff >= 0 ? _T("+") : _T("");
	CString lowSign = lowDiff >= 0 ? _T("+") : _T("");
	highPart.Format(_T("H:%s %s%s"), CCommon::FormatFloat(stockInfo.highPrice), highSign.GetString(), CCommon::FormatFloat(highDiff));
	lowPart.Format(_T("L:%s %s%s"), CCommon::FormatFloat(stockInfo.lowPrice), lowSign.GetString(), CCommon::FormatFloat(lowDiff));

	memDC.FillSolidRect(lc.left, rowY, lc.panelW, rowH, RGB(220, 235, 250));
	int textY = rowY + max(0, (rowH - memDC.GetTextExtent(highPart).cy) / 2);
	memDC.SetTextColor(RGB(128, 0, 128));
	memDC.TextOut(lc.textX, textY, highPart);
	int lowW = memDC.GetTextExtent(lowPart).cx;
	int lowX = lc.left + lc.panelW - lowW;
	memDC.SetTextColor(RGB(0, 100, 0));
	memDC.TextOut(lowX, textY, lowPart);

	memDC.SelectObject(oldFont);
}

void COrderBookPanel::DrawNetRatio00Ex(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo, int barY)
{
	auto stockDataPtr00 = g_data.GetStockData(stockInfo.code);
	if (!stockDataPtr00)
		return;

	// 从secVolumePool取1分钟加权平均净比
	STOCK::Volume diff = 0;
	double ratio = 0;
	if (!stockDataPtr00->GetSecNetDiff(1, diff, ratio))
		return;

	int barX = lc.textX;
	int barW = lc.right - barX - g_data.RDPI(4);
	if (barW <= 0)
		return;

	int midX = barX + barW / 2;
	int halfW = barW / 2;
	int fillW = static_cast<int>(std::sqrt(std::abs(ratio) / 100.0) * halfW);
	fillW = min(fillW, halfW);
	int dominantW = min(barW, halfW + fillW);

	COLORREF redColor = NET_RATIO_RED_COLORS[GetNetRatioColorIndex(ratio)];
	COLORREF greenColor = NET_RATIO_GREEN_COLORS[GetNetRatioColorIndex(ratio)];

	if (ratio > 0)
	{
		memDC.FillSolidRect(barX, barY, dominantW, 2, redColor);
		memDC.FillSolidRect(barX + dominantW, barY, barW - dominantW, 2, greenColor);
	}
	else if (ratio < 0)
	{
		memDC.FillSolidRect(barX, barY, dominantW, 2, greenColor);
		memDC.FillSolidRect(barX + dominantW, barY, barW - dominantW, 2, redColor);
	}
	else
	{
		memDC.FillSolidRect(barX, barY, barW, 2, RGB(230, 230, 230));
	}
	memDC.FillSolidRect(midX - 1, barY, 2, 2, RGB(255, 255, 255));
}

void COrderBookPanel::DrawNetInflowRow(CDC& memDC, const LayoutContext& lc, int rowIndex, const CString& label, double netInflow)
{
	int y = lc.RowY(rowIndex);
	int h = lc.RowH(rowIndex);

	// 行顶部分隔线
	CPen sepPen(PS_SOLID, 1, RGB(200, 200, 200));
	CPen* oldPen = memDC.GetCurrentPen();
	memDC.SelectObject(&sepPen);
	memDC.MoveTo(lc.left, y);
	memDC.LineTo(lc.right, y);
	memDC.SelectObject(oldPen);

	bool isPos = (netInflow >= 0);
	CString valTxt;
	valTxt.Format(L"%s%.2f万", isPos ? L"+" : L"-", std::abs(netInflow / 10000.0));

	memDC.SetTextColor(isPos ? COLOR_RED_UP : COLOR_GREEN_DOWN);
	int textY = y + max(0, (h - memDC.GetTextExtent(L"00").cy) / 2);
	memDC.TextOut(lc.textX, textY, label);
	int valW = memDC.GetTextExtent(valTxt).cx;
	memDC.TextOut(lc.right - valW - g_data.RDPI(2), textY, valTxt);
}

void COrderBookPanel::RefreshTickMini(const STOCK::StockInfo& stockInfo, int limit)
{
	const std::wstring& code = stockInfo.code;
	// 同一只股票至少每隔3秒刷新一次明细
	if (code != m_tickMiniCode || GetTickCount() - m_lastTickMiniRefreshTick >= 3000)
	{
		m_tickMiniCode = code;
		m_lastTickMiniRefreshTick = GetTickCount();
		m_tickMiniDetails.clear();
		if (!code.empty() && g_data.GetDbManager().IsOpen())
		{
			bool isEtf = stockInfo.IsETF();
			auto raw = g_data.GetDbManager().LoadLatestTransactions(code, limit);
			for (auto& t : raw)
			{
				// ETF 成交价格在DB中被Python放大了10倍，此处除以10还原
				if (isEtf)
					t.price = t.price / 10.0;
				m_tickMiniDetails.push_back(std::move(t));
			}
		}
	}

	// 整日累计净流入（复用盘口共享的 per-2s 分组统计缓存 m_priceCumVolMap）
	m_cumNetInflow = 0.0;
	for (const auto& kv : m_priceCumVolMap)
	{
		Price price = kv.first / 10000.0;   // 由量化key还原真实价格
		m_cumNetInflow += (kv.second.activeBuyVol - kv.second.activeSellVol) * price * 100.0;
	}

	// 区间净流入 = 当前明细(仅0/1)的买卖差
	m_intervalNetInflow = 0.0;
	for (const auto& t : m_tickMiniDetails)
	{
		double amt = t.price * static_cast<double>(t.vol) * 100.0;   // 元
		if (t.buyOrSell == 1)      // 主动卖：流出
			m_intervalNetInflow -= amt;
		else                       // 主动买/中性：流入
			m_intervalNetInflow += amt;
	}
}

void COrderBookPanel::DrawTickMini(CDC& memDC, const LayoutContext& lc, int startRow)
{
	const int showCount = min(10, static_cast<int>(m_tickMiniDetails.size()));
	if (showCount <= 0)
		return;

	// 直接继承精简版Draw已选定的紧凑字体，避免再次缩放导致明细过小
	CPen* oldPen = memDC.GetCurrentPen();

	// 最新在下：从最旧往上画，最新落在最后一行
	for (int i = showCount - 1; i >= 0; --i)
	{
		const auto& t = m_tickMiniDetails[i];
		int rowIdx = startRow + (showCount - 1 - i);
		int y = lc.RowY(rowIdx);
		int h = lc.RowH(rowIdx);

		bool isSell = (t.buyOrSell == 1);
		COLORREF lineColor = isSell ? COLOR_GREEN_DOWN : COLOR_RED_UP;

		CString priceStr;
		priceStr.Format(L"%.3f", t.price);
		CString timeStr = CCommon::StrToUnicode(t.timeKey.c_str()).c_str();
		CString volStr;
		volStr.Format(L"%lld", static_cast<long long>(t.vol));
		const wchar_t* dir = isSell ? L"S" : L"B";

		// 行间分隔线
		if (rowIdx > startRow)
		{
			CPen sepPen(PS_SOLID, 1, RGB(240, 240, 240));
			memDC.SelectObject(&sepPen);
			memDC.MoveTo(lc.left, y);
			memDC.LineTo(lc.right, y);
			memDC.SelectObject(oldPen);
		}

		int textY = y + max(0, (h - memDC.GetTextExtent(L"00").cy) / 2);

		// 左侧：时间 价格（中间空一格）
		CString leftTxt = timeStr + L"    " + priceStr;
		memDC.SetTextColor(lineColor);
		memDC.TextOut(lc.textX, textY, leftTxt);

		// 成交量：右对齐，紧挨 S/B；单独按成交额档位配色
		int volW = memDC.GetTextExtent(volStr).cx;
		int dirW = memDC.GetTextExtent(dir).cx;
		int sbX = lc.right - dirW - g_data.RDPI(2);
		int volX = sbX - volW - g_data.RDPI(12);
		volX = max(volX, lc.textX + memDC.GetTextExtent(leftTxt).cx + g_data.RDPI(8));

		double turnover = t.price * static_cast<double>(t.vol) * 100.0;
		COLORREF volColor;
		if (turnover >= 500000.0) {          // 超大单 ≥50万
			volColor = isSell ? VOL_COL_HUGE_ASK : VOL_COL_HUGE_BID;
			memDC.SetTextColor(RGB(160, 160, 160));
			memDC.TextOut(volX + 1, textY + 1, volStr);
		}
		else if (turnover >= 200000.0)     // 大单 20万~50万
			volColor = isSell ? VOL_COL_BIG_ASK : VOL_COL_BIG_BID;
		else                               // 正常单 <20万
			volColor = isSell ? VOL_COL_NORMAL_ASK : VOL_COL_NORMAL_BID;
		memDC.SetTextColor(volColor);
		memDC.TextOut(volX, textY, volStr);

		// 最右：S/B
		memDC.SetTextColor(lineColor);
		memDC.TextOut(sbX, textY, dir);
	}

	memDC.SelectObject(oldPen);
}

// ============================================================================
// 绘制买盘行（买一~买五，行8-12）
// ============================================================================
void COrderBookPanel::DrawBidRows(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo)
{
	std::vector<OrderBookRow> bottomRows;
	bottomRows.reserve(5);  // 买一~买五

	// 买一~买五
	for (int i = 0; i < 5; i++)
	{
		STOCK::Price price = stockInfo.bidLevels[i].price;
		// 仅买一(idx==0)显示后方累计主动成交量的瞬时变化
		STOCK::Volume delta = (i == 0) ? GetOrderDeltaLots(price, false) : 0;
		bottomRows.push_back(BuildBidRow(stockInfo, i, delta));
	}

	DrawPriceRows(memDC, lc, bottomRows, 8);
}

// ============================================================================
// 绘制成交明细界面（MX模式）：显示最近20条成交，每隔3秒从数据库刷新
// ============================================================================
void COrderBookPanel::DrawTickDetail(CDC& memDC, int left, int right, int height, const STOCK::StockInfo& stockInfo)
{
	const std::wstring& code = stockInfo.code;
	// 同一只股票至少每隔3秒刷新一次数据库明细
	if (code != m_tickDetailCode || GetTickCount() - m_lastTickRefreshTick >= 3000)
	{
		m_tickDetailCode = code;
		m_lastTickRefreshTick = GetTickCount();
		m_tickDetails.clear();
		if (!code.empty() && g_data.GetDbManager().IsOpen())
		{
			bool isEtf = stockInfo.IsETF();
			auto raw = g_data.GetDbManager().LoadLatestTransactions(code, 20);
			for (auto& t : raw)
			{
				// ETF 成交价格在DB中被Python放大了10倍，此处除以10还原
				if (isEtf)
					t.price = t.price / 10.0;
				m_tickDetails.push_back(std::move(t));
			}
		}
	}

	// 复用盘口面板同一份 per-2s 分组统计缓存，避免额外查询数据库
	// 由 m_priceCumVolMap 计算整日累计净流入额（元）
	RefreshPriceCumVol(stockInfo);
	m_cumNetInflow = 0.0;
	for (const auto& kv : m_priceCumVolMap)
	{
		Price price = kv.first / 10000.0;   // 由量化key还原真实价格
		m_cumNetInflow += (kv.second.activeBuyVol - kv.second.activeSellVol) * price * 100.0;
	}

	if (m_tickDetails.empty())
		return;

	// 内容区域（盘口标题栏下方）
	int headerHeight = g_data.RDPI(26) + g_data.RDPI(20);
	int obTitleH = g_data.RDPI(16);
	int topOffset = headerHeight + obTitleH;
	int contentH = height - obTitleH;
	if (contentH <= 0)
		return;
	int textX = left + g_data.RDPI(5) + 3;

	// 绘制盘口标题栏背景，避免残留其它模式内容
	memDC.FillSolidRect(left, headerHeight, right - left, obTitleH, RGB(245, 245, 245));

	const int showCount = min(20, static_cast<int>(m_tickDetails.size()));
	// 1行累计净流入 + 20行明细 + 1行净流入汇总
	const int rowH = max(1, contentH / 22);
	CPen* oldPen = memDC.GetCurrentPen();

	// 行变密，改用较小字体，保证每行内容完整显示
	CFont* oldTickFont = memDC.GetCurrentFont();
	LOGFONT lf;
	oldTickFont->GetLogFont(&lf);
	{
		int absH = abs(lf.lfHeight);
		int idealH = max(8, static_cast<int>(rowH * 0.85));
		idealH = min(absH, idealH);
		lf.lfHeight = (lf.lfHeight < 0) ? -idealH : idealH;
	}
	CFont tickSmallFont;
	tickSmallFont.CreateFontIndirect(&lf);
	memDC.SelectObject(&tickSmallFont);

	// 顶部汇总行：整日累计净流入（元），标签在左、金额右对齐
	{
		bool isPos = (m_cumNetInflow >= 0);
		double wanVal = m_cumNetInflow / 10000.0;
		CString labelTxt = L"累计净流入:";
		CString valTxt;
		valTxt.Format(L"%s%.2f万", isPos ? L"+" : L"-", std::abs(wanVal));

		memDC.SetTextColor(isPos ? COLOR_RED_UP : COLOR_GREEN_DOWN);
		int textY = topOffset + max(0, (rowH - memDC.GetTextExtent(L"00").cy) / 2);
		memDC.TextOut(textX, textY, labelTxt);
		// 金额右对齐到面板右缘
		int valW = memDC.GetTextExtent(valTxt).cx;
		memDC.TextOut(right - valW - g_data.RDPI(2), textY, valTxt);

		// 与明细区之间画分隔线
		CPen sepPen(PS_SOLID, 1, RGB(200, 200, 200));
		memDC.SelectObject(&sepPen);
		int sepY = topOffset + rowH - 1;
		memDC.MoveTo(left, sepY);
		memDC.LineTo(right, sepY);
		memDC.SelectObject(oldPen);
	}

	// 明细区从第二行开始
	int rowY = topOffset + rowH;
	// m_tickDetails 按id倒序（最新在前），这里从最旧往上画，让最新时间显示在最下方一行
	for (int i = showCount - 1; i >= 0; --i)
	{
		const auto& t = m_tickDetails[i];
		// 方向：buyOrSell=1→S(卖)，0/2→B(买)
		const wchar_t* dir = (t.buyOrSell == 1) ? L"S" : L"B";
		// 价格固定保留3位小数
		CString priceStr;
		priceStr.Format(L"%.3f", t.price);
		// 时间 hh:mm
		CString timeStr = CCommon::StrToUnicode(t.timeKey.c_str()).c_str();
		CString volStr;
		// DB中成交量为手，盘口挂单量显示单位也是手，直接显示保持一致
		volStr.Format(L"%lld", static_cast<long long>(t.vol));

		// 整行颜色：主动买(0/2)=红，主动卖(1)=绿
		bool isSell = (t.buyOrSell == 1);
		COLORREF lineColor = isSell ? COLOR_GREEN_DOWN : COLOR_RED_UP;

		// 实际成交额（元）= 成交价 * 成交量(手) * 100；按档位为成交量单独配色
		double turnover = t.price * static_cast<double>(t.vol) * 100.0;
		COLORREF volColor;
		if (turnover >= 500000.0) {          // 超大单 ≥50万
			volColor = isSell ? VOL_COL_HUGE_ASK : VOL_COL_HUGE_BID;
		}
		else if (turnover >= 200000.0)     // 大单 20万~50万
			volColor = isSell ? VOL_COL_BIG_ASK : VOL_COL_BIG_BID;
		else                               // 正常单 <20万
			volColor = isSell ? VOL_COL_NORMAL_ASK : VOL_COL_NORMAL_BID;

		// 各行底部画分隔线
		if (i < showCount - 1)
		{
			CPen sepPen(PS_SOLID, 1, RGB(230, 230, 230));
			memDC.SelectObject(&sepPen);
			memDC.MoveTo(left, rowY);
			memDC.LineTo(right, rowY);
			memDC.SelectObject(oldPen);
		}

		int textY = rowY + max(0, (rowH - memDC.GetTextExtent(L"00").cy) / 2);

		// 左侧：时间 价格（中间空4格）
		CString leftTxt;
		leftTxt = timeStr + L"    " + priceStr;
		memDC.SetTextColor(lineColor);
		memDC.TextOut(textX, textY, leftTxt);

		// 成交量：右对齐，紧挨 S/B 左侧
		int volW = memDC.GetTextExtent(volStr).cx;
		int dirW = memDC.GetTextExtent(dir).cx;
		// 布局：最右 S/B（右对齐），其左侧为成交量（右对齐），两列之间留一定间隔
		int sbX = right - dirW - g_data.RDPI(2);
		int volX = sbX - volW - g_data.RDPI(12);
		volX = max(volX, textX + memDC.GetTextExtent(leftTxt).cx + g_data.RDPI(8));

		if (turnover >= 500000.0) {          // 超大单 ≥50万
			memDC.SetTextColor(RGB(160, 160, 160));
			memDC.TextOut(volX + 1, textY + 1, volStr);
		}
		// 成交量单独按成交额档位配色
		memDC.SetTextColor(volColor);
		memDC.TextOut(volX, textY, volStr);

		// 最右：S/B 右对齐
		memDC.SetTextColor(lineColor);
		memDC.TextOut(sbX, textY, dir);

		rowY += rowH;
	}

	// 底部汇总行：净流入额 = Σ(主动买量×价格) - Σ(主动卖量×价格)，正红负绿
	// 固定最后一行（第22行）
	{
		int footY = topOffset + 21 * rowH;
		double netInflow = 0.0;
		for (const auto& t : m_tickDetails)
		{
			double amt = t.price * static_cast<double>(t.vol) * 100.0;   // 元
			if (t.buyOrSell == 1)      // 主动卖：流出
				netInflow -= amt;
			else                       // 主动买/中性：流入
				netInflow += amt;
		}

		// 分隔线（与汇总行上方）
		{
			CPen sepPen(PS_SOLID, 1, RGB(200, 200, 200));
			memDC.SelectObject(&sepPen);
			memDC.MoveTo(left, footY);
			memDC.LineTo(right, footY);
			memDC.SelectObject(oldPen);
		}

		int textY = footY + max(0, (rowH - memDC.GetTextExtent(L"00").cy) / 2);
		bool isPositive = (netInflow >= 0);
		memDC.SetTextColor(isPositive ? COLOR_RED_UP : COLOR_GREEN_DOWN);

		CString labelTxt = L"净流入:";
		CString valTxt;
		valTxt.Format(L"%s%.2f万", isPositive ? L"+" : L"-", std::abs(netInflow / 10000.0));
		memDC.TextOut(textX, textY, labelTxt);
		// 金额右对齐到面板右缘
		int valW = memDC.GetTextExtent(valTxt).cx;
		memDC.TextOut(right - valW - g_data.RDPI(2), textY, valTxt);
	}

	memDC.SelectObject(oldTickFont);
	memDC.SelectObject(oldPen);
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
		DrawRatioBar(memDC, barX, barY, barW, barH, netRatio);

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

void COrderBookPanel::DrawOrderBookRowText(CDC& memDC, const OrderBookRow& row, int x, int y, int rowWidth,
	int rowLeft, int rowTop, int rowHeight)
{
	COLORREF textColor;
	if (row.darkBackground)
		textColor = RGB(255, 255, 255);
	else
		textColor = row.textColor;
	memDC.SetTextColor(textColor);

	// 粗体字体
	CFont* pOldFont = nullptr;
	CFont boldFont;
	if (row.bold)
	{
		pOldFont = memDC.GetCurrentFont();
		LOGFONT lf;
		pOldFont->GetLogFont(&lf);
		lf.lfWeight = FW_BOLD;
		boldFont.CreateFontIndirect(&lf);
		memDC.SelectObject(&boldFont);
	}

	memDC.TextOut(x, y, row.text);

	if (row.bold && pOldFont)
		memDC.SelectObject(pOldFont);
	if (!row.drawSmallSuffix || row.smallSuffix.IsEmpty())
	{
		// 只有右对齐后缀
		if (!row.rightAlignSuffix.IsEmpty())
		{
			CFont* oldFont = memDC.GetCurrentFont();
			LOGFONT lf;
			oldFont->GetLogFont(&lf);
			// 精简版盘口已按行高缩小字体，后缀直接沿用当前字体，避免双重缩小
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
	// 精简版盘口已按行高缩小字体，后缀直接沿用当前字体，避免双重缩小
	CFont smallFont;
	smallFont.CreateFontIndirect(&lf);
	memDC.SelectObject(&smallFont);
	memDC.SetTextColor(row.darkBackground ? RGB(255, 255, 200) : textColor);
	memDC.TextOut(suffixX, y + g_data.RDPI(1), row.smallSuffix);
	// 右对齐绘制累计成交量和瞬时变化量
	// 布局：[...smallSuffix] [rightAlignSuffix] [cumVolSuffix] 右边距
	int rightEdge = x + rowWidth - g_data.RDPI(4);
	if (!row.cumVolSuffix.IsEmpty())
	{
		memDC.SetTextColor(row.cumVolSuffixColor);
		int cumVolW = memDC.GetTextExtent(row.cumVolSuffix).cx;
		memDC.TextOut(rightEdge - cumVolW, y + g_data.RDPI(1), row.cumVolSuffix);
		rightEdge -= cumVolW;
	}
	if (!row.rightAlignSuffix.IsEmpty())
	{
		memDC.SetTextColor(row.rightAlignSuffixColor);
		int raSuffixW = memDC.GetTextExtent(row.rightAlignSuffix).cx;
		memDC.TextOut(rightEdge - raSuffixW, y + g_data.RDPI(1), row.rightAlignSuffix);
	}
	memDC.SelectObject(oldFont);

	// 基金净值横线：当该行价格与净值（保留3位小数）位于同一行时，绘制一条1像素的紫色净值横线
	/*{
		char buff[128];
		sprintf_s(buff, "row iopv = %g", row.IOPV);
		CCommon::WriteLog(buff, g_data.m_log_path.c_str());
	}*/
	if (row.IOPV > 0.1)
	{
		double truncIopv = std::trunc(row.IOPV * 1000.0) / 1000.0;
		if (row.price == truncIopv)
		{
			// 横线覆盖当前行整行宽度，高度固定1像素
			// 位置：行内价格自下而上递增（行底=本档价格，行顶=下一档价格），
			//       净值高出本档价 lastDigit/10 个价位，故横线距行底高度 = lastDigit/10 * 行高；
			//       GDI坐标从上往下，因此 lineY = rowTop + 行高*(10-lastDigit)/10。
			//       例如净值为1.2344（高出本档价1.234正好4个万分位），横线绘制在距行底40%（自上而下60%）处
			long long iopvScaled = std::llround(row.IOPV * 10000.0);   // 净值放大到万分位整数，取最后一位
			int lastDigit = static_cast<int>(iopvScaled % 10);
			if (lastDigit < 0)
				lastDigit = -lastDigit;
			int lineY = rowTop + rowHeight * (9 - lastDigit) / 10;
			int lineRight = x + rowWidth;   // 当前行右边界
			CPen navPen(PS_SOLID, 1, RGB(160, 32, 240));   // 基金净值紫色，与分时净值线颜色一致
			CPen* pOldPen = memDC.SelectObject(&navPen);
			memDC.MoveTo(rowLeft, lineY);
			memDC.LineTo(lineRight, lineY);
			memDC.SelectObject(pOldPen);
		}
	}
}

void COrderBookPanel::DrawRatioBar(CDC& memDC, int x, int y, int w, int h, double ratio)
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
	if (ratio > 0)
	{
		memDC.FillSolidRect(x, y, dominantW, h, redColor);
		memDC.FillSolidRect(x + dominantW, y, w - dominantW, h, greenColor);
	}
	else if (ratio < 0)
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

// 将价格量化到0.0001精度作为map key，规避浮点相等比较误差
// 例如 ETF 真实价格0.59 与数据库 5.9/10=0.5900000000000001 会被归一到同一个key，避免查不到
static long long PriceToKey(STOCK::Price price)
{
	return static_cast<long long>(std::llround(price * 10000.0));
}

STOCK::Volume COrderBookPanel::GetOrderDeltaLots(STOCK::Price price, bool isAskSide) const
{
	if (price <= 0)
		return 0;
	long long key = PriceToKey(price);
	// 需要当前采样和上次采样都存在才能计算变化量
	auto curIt = m_priceCumVolMap.find(key);
	auto prevIt = m_priceCumVolPrev.find(key);
	if (curIt == m_priceCumVolMap.end() || prevIt == m_priceCumVolPrev.end())
		return 0;
	// 卖一(ask)显示累计主动买变化量，买一(bid)显示累计主动卖变化量
	return isAskSide
		? (curIt->second.activeBuyVol - prevIt->second.activeBuyVol)
		: (curIt->second.activeSellVol - prevIt->second.activeSellVol);
}

STOCK::Volume COrderBookPanel::GetOrderBookCumVol(STOCK::Price price, bool isAskSide) const
{
	if (price <= 0)
		return 0;
	auto it = m_priceCumVolMap.find(PriceToKey(price));
	if (it == m_priceCumVolMap.end())
		return 0;
	// 卖盘(ask)显示主动买成交量，买盘(bid)显示主动卖成交量
	return isAskSide ? it->second.activeBuyVol : it->second.activeSellVol;
}

void COrderBookPanel::RefreshPriceCumVol(const STOCK::StockInfo& stockInfo)
{
	const std::wstring& code = stockInfo.code;
	// 同一只股票至少每隔2秒刷新一次，避免高频重绘时频繁查询数据库
	if (!code.empty() && code == m_priceCumVolCode &&
		GetTickCount() - m_lastCumVolRefreshTick < 2000)
		return;

	// 股票切换时立即刷新；清空上一次股票的数据（prev也一并清空，避免变化量跨股票残留）
	if (code != m_priceCumVolCode)
	{
		m_priceCumVolMap.clear();
		m_priceCumVolPrev.clear();
	}
	m_priceCumVolCode = code;
	m_lastCumVolRefreshTick = GetTickCount();

	if (code.empty() || !g_data.GetDbManager().IsOpen())
		return;

	// 数据库中的ETF成交价格被Python放大了10倍，查询结果需除以10还原为真实价格
	bool isEtf = stockInfo.IsETF();

	std::string today = CCommon::GetTodayDate();
	auto stats = g_data.GetDbManager().LoadPriceVolumeStats(code, today);

	// 保存本次采样前的累计值，用于计算瞬时变化量（仅首次采样时 prev 为空，变化量显示0）
	m_priceCumVolPrev = m_priceCumVolMap;
	m_priceCumVolMap.clear();

	for (const auto& s : stats)
	{
		// s.buyOrSell: 1=主动卖, 0=主动买
		Price realPrice = isEtf ? (s.price / 10.0) : s.price;
		long long key = PriceToKey(realPrice);
		if (s.buyOrSell == 0)
			m_priceCumVolMap[key].activeBuyVol = s.vol;    // 主动买
		else if (s.buyOrSell == 1)
			m_priceCumVolMap[key].activeSellVol = s.vol;   // 主动卖
	}
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
	askTxt.Format(_T("%s"), priceStr); //askTxt.Format(_T("S%d:%s"), idx + 1, priceStr);
	CString askSuffix;
	askSuffix.Format(_T(" %s"), volumeStr.GetString());
	CString deltaStr;
	if (delta != 0)
	{
		CString deltaVal;
		deltaVal.Format(_T("%lld"), static_cast<long long>(std::abs(delta)));
		deltaStr.Format(_T("%s%s"), delta > 0 ? _T("+") : _T("-"), deltaVal.GetString());
	}

	// 累计成交量（卖盘显示主动买成交量）
	STOCK::Volume cumVol = GetOrderBookCumVol(price, true);
	CString cumVolStr;
	if (cumVol > 0)
		cumVolStr.Format(_T(" %lld"), static_cast<long long>(cumVol));

	OrderBookRow row;
	row.price = price;
	row.IOPV = stockInfo.iopv;
	row.text = askTxt;
	row.smallSuffix = askSuffix;
	row.rightAlignSuffix = deltaStr;
	row.rightAlignSuffixColor = COLOR_RED_UP;
	row.cumVolSuffix = cumVolStr;
	row.cumVolSuffixColor = RGB(128, 0, 128);
	row.drawSmallSuffix = true;
	row.textColor = (stockInfo.highPrice > 0 && price > 0 && price == stockInfo.highPrice) ? RGB(128, 0, 128) : COLOR_RED_UP;
	row.bold = (stockInfo.highPrice > 0 && price > 0 && price == stockInfo.highPrice);
	// 卖一背景色（仅当前价格=卖一时显示）
	if (idx == 0 && stockInfo.currentPrice > 0 && price > 0 && stockInfo.currentPrice == price)
	{
		row.fillBackground = true;
		row.backgroundColor = RGB(255, 200, 200);
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
	bidTxt.Format(_T("%s"), priceStr); //bidTxt.Format(_T("B%d:%s"), idx + 1, priceStr);
	CString bidSuffix;
	bidSuffix.Format(_T(" %s"), volumeStr.GetString());
	CString deltaStr;
	if (delta != 0)
	{
		CString deltaVal;
		deltaVal.Format(_T("%lld"), static_cast<long long>(std::abs(delta)));
		deltaStr.Format(_T("%s%s"), delta > 0 ? _T("+") : _T("-"), deltaVal.GetString());
	}

	// 累计成交量（买盘显示主动卖成交量）
	STOCK::Volume cumVol = GetOrderBookCumVol(price, false);
	CString cumVolStr;
	if (cumVol > 0)
		cumVolStr.Format(_T(" %lld"), static_cast<long long>(cumVol));

	OrderBookRow row;
	row.price = price;
	row.IOPV = stockInfo.iopv;
	row.text = bidTxt;
	row.smallSuffix = bidSuffix;
	row.rightAlignSuffix = deltaStr;
	row.rightAlignSuffixColor = COLOR_GREEN_DOWN;
	row.cumVolSuffix = cumVolStr;
	row.cumVolSuffixColor = RGB(0, 100, 0);
	row.drawSmallSuffix = true;
	row.textColor = (stockInfo.lowPrice > 0 && price > 0 && price == stockInfo.lowPrice) ? RGB(0, 100, 0) : COLOR_GREEN_DOWN;
	row.bold = (stockInfo.lowPrice > 0 && price > 0 && price == stockInfo.lowPrice);
	// 买一背景色（仅当前价格=买一时显示）
	if (idx == 0 && stockInfo.currentPrice > 0 && price > 0 && stockInfo.currentPrice == price)
	{
		row.fillBackground = true;
		row.backgroundColor = RGB(200, 255, 200);
	}
	else
	{
		row.fillBackground = (stockInfo.currentPrice > 0 && price > 0 && stockInfo.currentPrice == price);
		row.backgroundColor = RGB(200, 255, 200);
	}
	return row;
}

void COrderBookPanel::DrawPriceRows(CDC& memDC, const LayoutContext& lc, const std::vector<OrderBookRow>& rows, int startRow)
{
	for (int i = 0; i < static_cast<int>(rows.size()); i++)
	{
		int y = lc.RowY(startRow + i);
		int h = lc.RowH(startRow + i);
		if (rows[i].fillBackground)
		{
			memDC.FillSolidRect(lc.left, y, lc.right - lc.left, h, rows[i].backgroundColor);
		}
		int textVCenter = max(0, (h - memDC.GetTextExtent(rows[i].text).cy) / 2);
		DrawOrderBookRowText(memDC, rows[i], lc.textX, y + textVCenter, lc.right - lc.textX, lc.left, y, h);
	}
}