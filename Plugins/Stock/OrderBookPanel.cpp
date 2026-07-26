#include "pch.h"
#include "OrderBookPanel.h"
#include "ChartColors.h"
#include "Common.h"
#include "DataManager.h"
#include "SignalAnalyzer.h"
#include <algorithm>
#include <cmath>
#include <map>

void COrderBookPanel::Draw(CDC& memDC, int left, int right, int height, const STOCK::StockInfo& stockInfo,
	const std::vector<STOCK::KLinePoint>& klineData,
	const std::wstring& stockId, bool isKLineMode, bool isMin5KLineMode, bool isMin30KLineMode)
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
	// 辅助：计算第i行(0-based)的Y坐标
	auto rowY = [&](int i) -> int {
		if (i < rem) return topOffset + i * (rowHeight + 1);
		else return topOffset + rem * (rowHeight + 1) + (i - rem) * rowHeight;
		};
	// 辅助：计算第i行的高度
	auto rowH = [&](int i) -> int {
		return (i < rem) ? (rowHeight + 1) : rowHeight;
		};
	// 填充内容区域背景，避免底部空白
	memDC.FillSolidRect(left, topOffset, panelW, contentH, RGB(250, 250, 250));

	memDC.SetBkMode(TRANSPARENT);

	STOCK::Volume innerVol = stockInfo.innerVolume / 100;
	STOCK::Volume outerVol = stockInfo.outerVolume / 100;

	int textX = left + g_data.RDPI(5) + 3;

	// 更新买一/卖一挂盘数量变化方向
	auto stockDataForAccum = g_data.GetStockData(stockId);
	auto GetOrderPriceAccumLots = [&](STOCK::Price price) -> STOCK::Volume {
		if (!stockDataForAccum || price <= 0)
			return 0;
		auto it = stockDataForAccum->orderPriceAccumMap.find(price);
		if (it == stockDataForAccum->orderPriceAccumMap.end())
			return 0;
		return it->second.accumSellVolume / 100;
		};
	CString ask1VolumeTrend, bid1VolumeTrend;
	{
		static std::map<std::wstring, STOCK::Volume> lastAsk1VolumeMap;
		static std::map<std::wstring, STOCK::Volume> lastBid1VolumeMap;
		static std::map<std::wstring, CString> lastAsk1VolumeTrendMap;
		static std::map<std::wstring, CString> lastBid1VolumeTrendMap;
		STOCK::Volume curAsk1Volume = stockInfo.askLevels[0].volume;
		STOCK::Volume curBid1Volume = stockInfo.bidLevels[0].volume;

		auto lastAsk1VolumeIt = lastAsk1VolumeMap.find(stockId);
		if (lastAsk1VolumeIt != lastAsk1VolumeMap.end())
		{
			if (curAsk1Volume > lastAsk1VolumeIt->second)
				ask1VolumeTrend = _T("↑");
			else if (curAsk1Volume < lastAsk1VolumeIt->second)
				ask1VolumeTrend = _T("↓");
			else
			{
				auto lastTrendIt = lastAsk1VolumeTrendMap.find(stockId);
				if (lastTrendIt != lastAsk1VolumeTrendMap.end())
					ask1VolumeTrend = lastTrendIt->second;
			}
		}
		lastAsk1VolumeMap[stockId] = curAsk1Volume;
		if (!ask1VolumeTrend.IsEmpty())
			lastAsk1VolumeTrendMap[stockId] = ask1VolumeTrend;

		auto lastBid1VolumeIt = lastBid1VolumeMap.find(stockId);
		if (lastBid1VolumeIt != lastBid1VolumeMap.end())
		{
			if (curBid1Volume > lastBid1VolumeIt->second)
				bid1VolumeTrend = _T("↑");
			else if (curBid1Volume < lastBid1VolumeIt->second)
				bid1VolumeTrend = _T("↓");
			else
			{
				auto lastTrendIt = lastBid1VolumeTrendMap.find(stockId);
				if (lastTrendIt != lastBid1VolumeTrendMap.end())
					bid1VolumeTrend = lastTrendIt->second;
			}
		}
		lastBid1VolumeMap[stockId] = curBid1Volume;
		if (!bid1VolumeTrend.IsEmpty())
			lastBid1VolumeTrendMap[stockId] = bid1VolumeTrend;
	}
	struct OrderBookRow
	{
		STOCK::Price price;
		CString text;
		CString smallSuffix;
		CString rightAlignSuffix;  // 右对齐的累加成交量
		COLORREF textColor;
		bool fillBackground{ false };
		COLORREF backgroundColor;
		bool drawSmallSuffix{ false };
		bool darkBackground{ false };  // 深色背景时文字改白色
		bool blink{ false };  // 闪烁效果：当前价=卖一/买一且挂单≤1万
	};

	std::vector<OrderBookRow> priceRows;
	priceRows.reserve(4);  // 最高+卖三~卖一（行0-3）

	std::vector<OrderBookRow> bottomRows;
	bottomRows.reserve(4);  // 买一~买三+最低（行5-8）

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
		STOCK::Volume volume = stockInfo.askLevels[idx].volume / 100;
		CString volumeStr = CCommon::FormatVolumeInt(volume);
		CString priceStr = stockInfo.IsETF() ? CCommon::FormatETFPrice(price) : CCommon::FormatFloat(price);
		CString askTxt;
		askTxt.Format(_T("S%d:%s"), idx + 1, priceStr);
		CString askSuffix;
		askSuffix.Format(_T(" %s"), volumeStr.GetString());
		STOCK::Volume accum = GetOrderPriceAccumLots(price);
		CString accumStr;
		if (accum > 0)
		{
			accumStr = CCommon::FormatVolumeInt(accum);
			if (idx == 0 && !ask1VolumeTrend.IsEmpty())
				askSuffix.AppendFormat(_T("%s"), ask1VolumeTrend.GetString());
		}

		OrderBookRow row;
		row.price = price;
		row.text = askTxt;
		row.smallSuffix = askSuffix;
		row.rightAlignSuffix = accumStr;
		row.drawSmallSuffix = true;
		row.textColor = COLOR_RED_UP;
		// 卖一背景色按3档强度区分（仅当前价格=卖一时显示）
		if (idx == 0 && stockInfo.currentPrice > 0 && price > 0 && stockInfo.currentPrice == price)
		{
			row.fillBackground = true;
			if (volume > accum)
				row.backgroundColor = RGB(255, 200, 200);   // 弱：挂单量>累加量
			else if (accum > volume * 2)
			{
				row.backgroundColor = RGB(180, 50, 50);     // 强：累加量/挂单量>2
				row.darkBackground = true;
			}
			else
			{
				row.backgroundColor = RGB(240, 40, 40);     // 中：1~2之间
				row.darkBackground = true;
			}
			// 挂单量≤1万时闪烁
			if (volume <= 10000)
				row.blink = true;
		}
		else
		{
			row.fillBackground = (stockInfo.currentPrice > 0 && price > 0 && stockInfo.currentPrice == price);
			row.backgroundColor = RGB(255, 200, 200);
		}
		priceRows.push_back(row);
	}

	// 买一~买三
	for (int i = 0; i < 3; i++)
	{
		STOCK::Price price = stockInfo.bidLevels[i].price;
		STOCK::Volume volume = stockInfo.bidLevels[i].volume / 100;
		CString volumeStr = CCommon::FormatVolumeInt(volume);
		CString priceStr = stockInfo.IsETF() ? CCommon::FormatETFPrice(price) : CCommon::FormatFloat(price);
		CString bidTxt;
		bidTxt.Format(_T("B%d:%s"), i + 1, priceStr);
		CString bidSuffix;
		bidSuffix.Format(_T(" %s"), volumeStr.GetString());
		STOCK::Volume accum = GetOrderPriceAccumLots(price);
		CString accumStr;
		if (accum > 0)
		{
			accumStr = CCommon::FormatVolumeInt(accum);
			if (i == 0 && !bid1VolumeTrend.IsEmpty())
				bidSuffix.AppendFormat(_T("%s"), bid1VolumeTrend.GetString());
		}

		OrderBookRow row;
		row.price = price;
		row.text = bidTxt;
		row.smallSuffix = bidSuffix;
		row.rightAlignSuffix = accumStr;
		row.drawSmallSuffix = true;
		row.textColor = COLOR_GREEN_DOWN;
		// 买一背景色按3档强度区分（仅当前价格=买一时显示）
		if (i == 0 && stockInfo.currentPrice > 0 && price > 0 && stockInfo.currentPrice == price)
		{
			row.fillBackground = true;
			if (volume > accum)
				row.backgroundColor = RGB(200, 255, 200);   // 弱：挂单量>累加量
			else if (accum > volume * 2)
			{
				row.backgroundColor = RGB(25, 120, 25);     // 强：累加量/挂单量>2
				row.darkBackground = true;
			}
			else
			{
				row.backgroundColor = RGB(50, 180, 50);     // 中：1~2之间
				row.darkBackground = true;
			}
			// 挂单量≤1万时闪烁
			if (volume <= 10000)
				row.blink = true;
		}
		else
		{
			row.fillBackground = (stockInfo.currentPrice > 0 && price > 0 && stockInfo.currentPrice == price);
			row.backgroundColor = RGB(200, 255, 200);
		}
		bottomRows.push_back(row);
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

	auto DrawOrderBookRowText = [&](const OrderBookRow& row, int x, int y, int rowWidth, bool blinkOff = false) {
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
				memDC.SetTextColor(row.darkBackground ? RGB(255, 255, 200) : textColor);
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
		// 右对齐绘制累加成交量
		if (!row.rightAlignSuffix.IsEmpty())
		{
			int raSuffixW = memDC.GetTextExtent(row.rightAlignSuffix).cx;
			memDC.TextOut(x + rowWidth - raSuffixW - g_data.RDPI(4), y + g_data.RDPI(1), row.rightAlignSuffix);
		}
		memDC.SelectObject(oldFont);
		};

	// 最高+卖三~卖一（行2-5）
	DWORD tickCount = GetTickCount();
	bool blinkOn = (tickCount / 500) % 2 == 0;  // 每500ms切换
	for (int i = 0; i < static_cast<int>(priceRows.size()); i++)
	{
		int y = rowY(2 + i);
		int h = rowH(2 + i);
		if (priceRows[i].fillBackground)
		{
			if (priceRows[i].blink && !blinkOn)
				memDC.FillSolidRect(left, y, right - left, h, RGB(250, 250, 250));  // 闪烁关闭时用背景色
			else
				memDC.FillSolidRect(left, y, right - left, h, priceRows[i].backgroundColor);
		}
		int textVCenter = max(0, (h - memDC.GetTextExtent(priceRows[i].text).cy) / 2);
		DrawOrderBookRowText(priceRows[i], textX, y + textVCenter, right - textX, priceRows[i].blink && !blinkOn);
	}

	// 净比00（行6，卖一和买一之间）— 提前绘制，无标签，图形占满整行
	{
		auto stockDataPtr00 = g_data.GetStockData(stockId);
		int row6H = rowH(6);

		int periodBarX = textX;
		int periodBarW = right - periodBarX - g_data.RDPI(4);

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

					int barY = rowY(6) + (row6H - barH) / 2;
					memDC.FillSolidRect(curBarX, barY, curBarW, barH, barColor);
					slotX += curSlotW;
				}
			}
		}
	}

	// 买一~买三+最低（行7-10）
	for (int i = 0; i < static_cast<int>(bottomRows.size()); i++)
	{
		int y = rowY(7 + i);
		int h = rowH(7 + i);
		if (bottomRows[i].fillBackground)
		{
			if (bottomRows[i].blink && !blinkOn)
				memDC.FillSolidRect(left, y, right - left, h, RGB(250, 250, 250));
			else
				memDC.FillSolidRect(left, y, right - left, h, bottomRows[i].backgroundColor);
		}
		int textVCenter = max(0, (h - memDC.GetTextExtent(bottomRows[i].text).cy) / 2);
		DrawOrderBookRowText(bottomRows[i], textX, y + textVCenter, right - textX, bottomRows[i].blink && !blinkOn);
	}

	static const COLORREF NET_RATIO_RED_COLORS[] = {
		RGB(240, 40, 40),   // 0-30
		RGB(180, 50, 50),   // 30-60
		RGB(130, 20, 40)    // 60以上
	};
	static const COLORREF NET_RATIO_GREEN_COLORS[] = {
		RGB(40, 240, 40),  // 0~30 浅亮绿（弱多）
		RGB(50, 180, 50),  // 30~60 中草绿（中多）
		RGB(20, 130, 40)   // 60以上 深墨绿（强多）
	};
	auto GetNetRatioColorIndex = [](double ratio) -> int {
		double absRatioValue = std::abs(ratio);
		if (absRatioValue <= 30) return 0;
		if (absRatioValue <= 60) return 1;
		return 2;
		};

	auto DrawNetRatioBarText = [&](int x, int y, int w, int h, const CString& ratioText, const CString& diffText) {
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
		};

	auto DrawRatioBar = [&](int x, int y, int w, int h, double ratio, STOCK::Volume diff) {
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
		};

	// 委比（行0，第一行）
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
	int wbBarY = rowY(0);
	int wbBarH = rowH(0);
	memDC.SetTextColor(wbRatio > 0 ? COLOR_RED_UP : (wbRatio < 0 ? COLOR_GREEN_DOWN : COLOR_BLACK));
	memDC.TextOut(textX, wbBarY + max(0, (wbBarH - memDC.GetTextExtent(wbLabel).cy) / 2), wbLabel);
	int wbBarX = textX + memDC.GetTextExtent(wbLabel).cx + g_data.RDPI(4);
	int wbBarW = right - wbBarX - g_data.RDPI(4);
	DrawRatioBar(wbBarX, wbBarY, wbBarW, wbBarH, wbRatio, bidTotal - askTotal);
	CString wbTxt;
	wbTxt.Format(_T("%.2f"), std::abs(wbRatio));
	DrawNetRatioBarText(wbBarX, wbBarY, wbBarW, wbBarH, wbTxt, _T(""));

	// 趋势、净比、振幅和换手率

	// 趋势判定（行1）
	// 同时显示30分钟、5分钟和当前视图趋势，格式：趋势:上涨(30) 震荡(5) 低吸
	{
		auto stockDataForTrend = g_data.GetStockData(stockId);

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

			// 当前视图趋势（沿用原逻辑）
			if (isMin30KLineMode)
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
			else if (isMin5KLineMode)
			{
				if (valid5)
				{
					dirCur = dir5;
					validCur = true;
				}
			}
			else if (!isKLineMode)
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
			else
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
		int drawX = textX;
		int drawY = rowY(1) + max(0, (rowH(1) - memDC.GetTextExtent(_T("Ay")).cy) / 2);
		for (const auto& seg : segs)
		{
			memDC.SetTextColor(seg.color);
			memDC.TextOut(drawX, drawY, seg.text);
			drawX += memDC.GetTextExtent(seg.text).cx;
		}
	}

	auto stockDataPtr = g_data.GetStockData(stockId);

	STOCK::Volume netDiff = outerVol - innerVol;
	STOCK::Volume totalInnerOuter = outerVol + innerVol;
	double netRatio = totalInnerOuter > 0 ? static_cast<double>(netDiff) / totalInnerOuter * 100 : 0;
	static std::map<std::wstring, double> lastNetRatioMap;
	static std::map<std::wstring, CString> lastNetRatioTrendMap;
	CString netRatioTrend;
	double absNetRatio = std::abs(netRatio);
	auto lastNetRatioIt = lastNetRatioMap.find(stockId);
	if (lastNetRatioIt != lastNetRatioMap.end())
	{
		double lastAbsNetRatio = std::abs(lastNetRatioIt->second);
		if (absNetRatio > lastAbsNetRatio)
		{
			netRatioTrend = _T("↑");
			lastNetRatioMap[stockId] = netRatio;
			lastNetRatioTrendMap[stockId] = netRatioTrend;
		}
		else if (absNetRatio < lastAbsNetRatio)
		{
			netRatioTrend = _T("↓");
			lastNetRatioMap[stockId] = netRatio;
			lastNetRatioTrendMap[stockId] = netRatioTrend;
		}
		else
		{
			auto lastTrendIt = lastNetRatioTrendMap.find(stockId);
			if (lastTrendIt != lastNetRatioTrendMap.end())
				netRatioTrend = lastTrendIt->second;
		}
	}
	else
	{
		double previousRatio = 0;
		if (stockDataPtr && stockDataPtr->GetPreviousInnerOuterTotalRatio(previousRatio))
		{
			double previousAbsRatio = std::abs(previousRatio);
			if (absNetRatio > previousAbsRatio)
			{
				netRatioTrend = _T("↑");
				lastNetRatioTrendMap[stockId] = netRatioTrend;
			}
			else if (absNetRatio < previousAbsRatio)
			{
				netRatioTrend = _T("↓");
				lastNetRatioTrendMap[stockId] = netRatioTrend;
			}
		}
		lastNetRatioMap[stockId] = netRatio;
	}
	CString netDiffStr = CCommon::FormatVolumeInt(std::abs(netDiff));

	int barY = rowY(15);
	int barH = rowHeight;
	COLORREF netRatioRedColor = NET_RATIO_RED_COLORS[GetNetRatioColorIndex(netRatio)];
	COLORREF netRatioGreenColor = NET_RATIO_GREEN_COLORS[GetNetRatioColorIndex(netRatio)];
	CString netRatioLabel = _T("净比99:");
	memDC.SetTextColor(netDiff > 0 ? COLOR_RED_UP : (netDiff < 0 ? COLOR_GREEN_DOWN : COLOR_BLACK));
	memDC.TextOut(textX, barY + max(0, (barH - memDC.GetTextExtent(netRatioLabel).cy) / 2), netRatioLabel);
	int barX = textX + memDC.GetTextExtent(netRatioLabel).cx + g_data.RDPI(4);
	int barW = right - barX - g_data.RDPI(4);
	if (barW > 0)
	{
		DrawRatioBar(barX, barY, barW, barH, netRatio, netDiff);

		CString diffSign = netDiff >= 0 ? _T("+") : _T("-");
		CString netRatioTxt;
		netRatioTxt.Format(_T("%.2f%s"), std::abs(netRatio), netRatioTrend.GetString());
		CString netDiffTxt;
		netDiffTxt.Format(_T("%s%s"), diffSign.GetString(), netDiffStr.GetString());
		DrawNetRatioBarText(barX, barY, barW, barH, netRatioTxt, netDiffTxt);
	}

	// 净差1/5/10/20（行11-14），使用净比条形图样式
	const int netPeriods[] = { 1, 5, 10, 20 };
	static std::map<std::wstring, std::map<int, double>> lastPeriodRatioMap;
	static std::map<std::wstring, std::map<int, CString>> lastPeriodRatioTrendMap;
	for (int i = 0; i < 4; i++)
	{
		int periodBarY = rowY(11 + i);
		int periodBarH = rowHeight;
		CString periodLabel;
		periodLabel.Format(_T("净比%02d:"), netPeriods[i]);
		COLORREF periodLabelColor = COLOR_BLACK;
		STOCK::Volume diff = 0;
		double ratio = 0;
		bool hasData = stockDataPtr && stockDataPtr->GetInnerOuterNetDiff(netPeriods[i], diff, ratio);
		COLORREF periodRedColor = NET_RATIO_RED_COLORS[GetNetRatioColorIndex(ratio)];
		COLORREF periodGreenColor = NET_RATIO_GREEN_COLORS[GetNetRatioColorIndex(ratio)];
		if (hasData)
			periodLabelColor = diff > 0 ? COLOR_RED_UP : (diff < 0 ? COLOR_GREEN_DOWN : COLOR_BLACK);
		memDC.SetTextColor(periodLabelColor);
		memDC.TextOut(textX, periodBarY + max(0, (periodBarH - memDC.GetTextExtent(periodLabel).cy) / 2), periodLabel);

		int periodBarX = textX + memDC.GetTextExtent(periodLabel).cx + g_data.RDPI(4);
		int periodBarW = right - periodBarX - g_data.RDPI(4);
		if (periodBarW <= 0)
			continue;

		DrawRatioBar(periodBarX, periodBarY, periodBarW, periodBarH, ratio, diff);

		CString periodTxt;
		CString periodDiffTxt;
		if (hasData)
		{
			CString diffStr = CCommon::FormatVolumeInt(std::abs(diff) / 100.0);
			CString ratioTrend;
			auto stockPeriodRatioIt = lastPeriodRatioMap.find(stockId);
			if (stockPeriodRatioIt != lastPeriodRatioMap.end())
			{
				auto lastRatioIt = stockPeriodRatioIt->second.find(netPeriods[i]);
				if (lastRatioIt != stockPeriodRatioIt->second.end())
				{
					double absRatio = std::abs(ratio);
					double lastAbsRatio = std::abs(lastRatioIt->second);
					if (absRatio > lastAbsRatio)
					{
						ratioTrend = _T("↑");
						lastPeriodRatioMap[stockId][netPeriods[i]] = ratio;
						lastPeriodRatioTrendMap[stockId][netPeriods[i]] = ratioTrend;
					}
					else if (absRatio < lastAbsRatio)
					{
						ratioTrend = _T("↓");
						lastPeriodRatioMap[stockId][netPeriods[i]] = ratio;
						lastPeriodRatioTrendMap[stockId][netPeriods[i]] = ratioTrend;
					}
					else
					{
						auto stockPeriodTrendIt = lastPeriodRatioTrendMap.find(stockId);
						if (stockPeriodTrendIt != lastPeriodRatioTrendMap.end())
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
						double absRatio = std::abs(ratio);
						double previousAbsRatio = std::abs(previousRatio);
						if (absRatio > previousAbsRatio)
						{
							ratioTrend = _T("↑");
							lastPeriodRatioTrendMap[stockId][netPeriods[i]] = ratioTrend;
						}
						else if (absRatio < previousAbsRatio)
						{
							ratioTrend = _T("↓");
							lastPeriodRatioTrendMap[stockId][netPeriods[i]] = ratioTrend;
						}
					}
					lastPeriodRatioMap[stockId][netPeriods[i]] = ratio;
				}
			}
			else
			{
				STOCK::Volume previousDiff = 0;
				double previousRatio = 0;
				if (stockDataPtr->GetPreviousInnerOuterNetDiff(netPeriods[i], previousDiff, previousRatio))
				{
					double absRatio = std::abs(ratio);
					double previousAbsRatio = std::abs(previousRatio);
					if (absRatio > previousAbsRatio)
					{
						ratioTrend = _T("↑");
						lastPeriodRatioTrendMap[stockId][netPeriods[i]] = ratioTrend;
					}
					else if (absRatio < previousAbsRatio)
					{
						ratioTrend = _T("↓");
						lastPeriodRatioTrendMap[stockId][netPeriods[i]] = ratioTrend;
					}
				}
				lastPeriodRatioMap[stockId][netPeriods[i]] = ratio;
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

		DrawNetRatioBarText(periodBarX, periodBarY, periodBarW, periodBarH, periodTxt, periodDiffTxt);
	}

	// 振幅、换手率（所有净比下方）
	if (!klineData.empty())
	{
		int textXAmp = left + g_data.RDPI(5) + 3;

		CString ampTxt;
		float fluctuation = stockInfo.highPrice - stockInfo.lowPrice;
		float fluctuationPercent = stockInfo.prevClosePrice != 0 ? (fluctuation / stockInfo.prevClosePrice) * 100 : 0;

		auto stockDataPtr2 = g_data.GetStockData(stockId);
		auto* klinePtr2 = stockDataPtr2 ? stockDataPtr2->getKLineData() : nullptr;
		double avgAmplitude5 = klinePtr2 ? klinePtr2->CalculateAverageAmplitude(5) : 0;
		CString amp5Str;
		if (avgAmplitude5 > 0)
			amp5Str.Format(_T("%.2f%%"), avgAmplitude5);
		else
			amp5Str = _T("--");
		ampTxt.Format(_T("振幅: 01:%.2f%% 05:%s"), fluctuationPercent, amp5Str.GetString());
		memDC.SetTextColor(COLOR_BLACK);
		memDC.TextOut(textXAmp, rowY(16) + max(0, (rowH(16) - memDC.GetTextExtent(ampTxt).cy) / 2), ampTxt);
	}

	CString turnoverTxt;
	turnoverTxt.Format(_T("换手率: %.2f%%"), stockInfo.turnoverRate);
	if (stockInfo.turnoverRate >= 5)
		memDC.SetTextColor(COLOR_RED_UP);
	else
		memDC.SetTextColor(COLOR_GRAY_TEXT);
	memDC.TextOut(textX, rowY(17) + max(0, (rowH(17) - memDC.GetTextExtent(turnoverTxt).cy) / 2), turnoverTxt);
}