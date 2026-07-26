#include "pch.h"
#include "TimelineChart.h"
#include "ChartColors.h"
#include "Common.h"
#include "DataManager.h"
#include "SignalAnalyzer.h"
#include "StockIndicator.h"
#include "KLineChart.h"
#include "IndicatorChart.h"
#include "StatusBarPanel.h"
#include <algorithm>
#include <cmath>
#include <set>

// 时间标记结构体（供分时图绘制函数共用）
struct TimeMarker {
	const TCHAR* label;
	int minutesFromStart;
};

// 辅助函数：绘制价格点标签（最高/最低价标注）
static void DrawPricePointLabel(CDC& memDC, int pointX, int pointY, int chartLeft, int chartTop, int chartWidth, int chartHeight,
	STOCK::Price price, bool isHigh, COLORREF color)
{
	CString label = CCommon::FormatFloat(price);
	CSize labelSize = memDC.GetTextExtent(label);
	const int gap = g_data.RDPI(4);
	const int arrowGap = g_data.RDPI(10);
	const int chartRight = chartLeft + chartWidth;
	const int chartBottom = chartTop + chartHeight;

	int labelX = pointX - labelSize.cx / 2;
	int labelY = isHigh ? pointY - labelSize.cy - arrowGap : pointY + arrowGap;
	bool useSideLabel = labelX < chartLeft || labelX + labelSize.cx > chartRight;

	if (useSideLabel)
	{
		if (pointX < chartLeft + chartWidth / 2)
		{
			label.Format(_T("\u2190%s"), CCommon::FormatFloat(price));
			labelSize = memDC.GetTextExtent(label);
			labelX = pointX + gap;
		}
		else
		{
			label.Format(_T("%s\u2192"), CCommon::FormatFloat(price));
			labelSize = memDC.GetTextExtent(label);
			labelX = pointX - labelSize.cx - gap;
		}
		labelY = pointY - labelSize.cy / 2;
		labelX = max(chartLeft, min(labelX, chartRight - labelSize.cx));
		labelY = max(chartTop, min(labelY, chartBottom - labelSize.cy));
		memDC.SetTextColor(color);
		memDC.TextOut(labelX, labelY, label);
		return;
	}

	labelY = max(chartTop, min(labelY, chartBottom - labelSize.cy));
	memDC.SetTextColor(color);
	memDC.TextOut(labelX, labelY, label);

	int fromX = labelX + labelSize.cx / 2;
	int fromY = isHigh ? labelY + labelSize.cy : labelY;
	if (abs(pointY - fromY) > g_data.RDPI(2))
	{
		CPen pen(PS_SOLID, 1, color);
		CPen* pOldPen = memDC.SelectObject(&pen);
		memDC.MoveTo(fromX, fromY);
		memDC.LineTo(pointX, pointY);

		int dir = (pointY >= fromY) ? 1 : -1;
		int arrowLen = g_data.RDPI(4);
		int arrowHalf = g_data.RDPI(3);
		memDC.MoveTo(pointX, pointY);
		memDC.LineTo(pointX - arrowHalf, pointY - dir * arrowLen);
		memDC.MoveTo(pointX, pointY);
		memDC.LineTo(pointX + arrowHalf, pointY - dir * arrowLen);
		memDC.SelectObject(pOldPen);
	}
}

CPoint CTimelineChart::Stock2Point(int x, int y, int w, int h, double unitY, const STOCK::TimelinePoint& item, const STOCK::Price prevClosePrice)
{
	CPoint p = CPoint();
	std::vector<std::string> time_arr = CCommon::split(item.time, ":");
	if (time_arr.size() == 3)
	{
		static int before12ClockOffset = 570;
		static int after12ClockOffset = 660;
		static float totalMinutes = 240.0;

		int hour = _ttoi(CString(time_arr[0].c_str()));
		int minute = _ttoi(CString(time_arr[1].c_str()));

		int countX = hour * 60 + minute;

		if (hour < 12)
		{
			countX -= before12ClockOffset;
		}
		else if (hour >= 13)
		{
			countX -= after12ClockOffset;
		}
		p.x = w / totalMinutes * countX;
	}
	p.y = (item.price - prevClosePrice) * unitY * 100;
	return p;
}

void CTimelineChart::DrawTimelineHeader(CDC& memDC, const TimelineDrawContext& ctx, const HoverState& hover)
{
	CPoint origOrg = memDC.GetViewportOrg();
	memDC.SetViewportOrg(0, 0);

	CString macdSignal;
	auto stockData = g_data.GetStockData(hover.stockId);
	if (stockData && stockData->info.is_ok)
		macdSignal = stockData->macdTrendSignal;

	CStatusBarPanel statusBarPanel;
	statusBarPanel.DrawHeader(memDC, ctx.realtimeData, ctx.windowWidth, g_data.RDPI(26), macdSignal);
	memDC.SetViewportOrg(origOrg);
}

void CTimelineChart::DrawTimelineMAIndicators(CDC& memDC, const TimelineDrawContext& ctx)
{
	int maY = g_data.RDPI(26) + g_data.RDPI(2);
	if (ctx.realtimeData.currentPrice <= 0)
		return;

	CString highTxt;
	COLORREF highColor = (ctx.realtimeData.highPrice >= ctx.realtimeData.prevClosePrice) ? COLOR_RED_UP : COLOR_GREEN_DOWN;
	highTxt.Format(_T("最高:%s "), CCommon::FormatFloat(ctx.realtimeData.highPrice));

	CString lowTxt;
	COLORREF lowColor = (ctx.realtimeData.lowPrice >= ctx.realtimeData.prevClosePrice) ? COLOR_RED_UP : COLOR_GREEN_DOWN;
	lowTxt.Format(_T("最低:%s"), CCommon::FormatFloat(ctx.realtimeData.lowPrice));

	int totalWidth = memDC.GetTextExtent(highTxt).cx + memDC.GetTextExtent(lowTxt).cx;
	int startX = (ctx.chartWidth - totalWidth) / 2;
	startX = max(g_data.RDPI(5), startX);

	memDC.SetTextColor(highColor);
	memDC.TextOut(startX, maY, highTxt);
	startX += memDC.GetTextExtent(highTxt).cx;

	memDC.SetTextColor(lowColor);
	memDC.TextOut(startX, maY, lowTxt);
}

void CTimelineChart::DrawTimelineBackgroundHighlights(CDC& memDC, const TimelineDrawContext& ctx)
{
	if (!ctx.timelinePoint || ctx.timelinePoint->size() < 240)
		return;
	CBrush blueBrush(COLOR_LIGHT_BLUE);
	memDC.FillRect(CRect(ctx.chartWidth * 0 / 240, ctx.priceChartTop, ctx.chartWidth * 30 / 240, ctx.priceChartTop + ctx.priceChartHeight), &blueBrush);
	memDC.FillRect(CRect(ctx.chartWidth * 140 / 240, ctx.priceChartTop, ctx.chartWidth * 150 / 240, ctx.priceChartTop + ctx.priceChartHeight), &blueBrush);

	CBrush greenBrush(COLOR_LIGHT_GREEN);
	memDC.FillRect(CRect(ctx.chartWidth * 210 / 240, ctx.priceChartTop, ctx.chartWidth * 225 / 240, ctx.priceChartTop + ctx.priceChartHeight), &greenBrush);
}

void CTimelineChart::DrawTimelineGridLines(CDC& memDC, const TimelineDrawContext& ctx)
{
	CPen pGrid(PS_SOLID, 1, COLOR_GRAY_GRID);
	CPen* pOldPen = memDC.SelectObject(&pGrid);

	if (ctx.timelinePoint && !ctx.timelinePoint->empty())
	{
		const int totalPts = static_cast<int>(ctx.timelinePoint->size());
		const int numVLines = 4;
		for (int i = 0; i <= numVLines; i++)
		{
			int idx = totalPts * i / numVLines;
			if (idx >= totalPts) idx = totalPts - 1;
			int xPos = ctx.chartWidth * i / numVLines;
			memDC.MoveTo(xPos, ctx.priceChartTop);
			memDC.LineTo(xPos, ctx.priceChartTop + ctx.priceChartHeight);
		}
	}

	memDC.MoveTo(0, ctx.priceChartTop + ctx.priceChartHeight);
	memDC.LineTo(ctx.chartWidth, ctx.priceChartTop + ctx.priceChartHeight);

	if (ctx.maxPrice > 0 && ctx.minPrice >= 0 && ctx.maxPrice > ctx.minPrice && ctx.niceStep > 0)
	{
		CPen pGridLine(PS_DOT, 1, COLOR_GRAY_GRID);
		memDC.SelectObject(&pGridLine);
		double priceRange = ctx.maxPrice - ctx.minPrice;
		double unitY = ctx.unitY;
		int labelCount = static_cast<int>(round(priceRange / ctx.niceStep));
		for (int i = 0; i <= labelCount; i++)
		{
			double labelPrice = round((ctx.minPrice + i * ctx.niceStep) * 1000.0) / 1000.0;
			int y = ctx.priceChartTop + ctx.priceChartHeight - static_cast<int>(round((labelPrice - ctx.minPrice) * unitY));
			memDC.MoveTo(0, y);
			memDC.LineTo(ctx.chartWidth, y);
		}
	}

	if (ctx.maxPrice > 0 && ctx.minPrice >= 0 && ctx.maxPrice > ctx.minPrice && ctx.realtimeData.prevClosePrice > 0
		&& ctx.realtimeData.prevClosePrice >= ctx.minPrice && ctx.realtimeData.prevClosePrice <= ctx.maxPrice)
	{
		CPen pMiddleLine(PS_DASHDOT, 1, COLOR_GRAY_MIDDLE);
		memDC.SelectObject(&pMiddleLine);
		int prevCloseY = ctx.priceChartTop + ctx.priceChartHeight - static_cast<int>(round((ctx.realtimeData.prevClosePrice - ctx.minPrice) * ctx.unitY));
		prevCloseY = max(ctx.priceChartTop, min(prevCloseY, ctx.priceChartTop + ctx.priceChartHeight));
		memDC.MoveTo(0, prevCloseY);
		memDC.LineTo(ctx.chartWidth, prevCloseY);
	}

	memDC.SelectObject(pOldPen);
}

void CTimelineChart::DrawTimelinePriceLabels(CDC& memDC, const TimelineDrawContext& ctx)
{
	if (ctx.maxPrice > 0 && ctx.minPrice >= 0 && ctx.maxPrice > ctx.minPrice && ctx.niceStep > 0)
	{
		int oldBkMode = memDC.SetBkMode(TRANSPARENT);
		double priceRange = ctx.maxPrice - ctx.minPrice;
		double unitY = ctx.unitY;

		int labelCount = static_cast<int>(round(priceRange / ctx.niceStep));
		for (int i = 0; i <= labelCount; i++)
		{
			double labelPrice = round((ctx.minPrice + i * ctx.niceStep) * 1000.0) / 1000.0;
			int y = ctx.priceChartTop + ctx.priceChartHeight - static_cast<int>(round((labelPrice - ctx.minPrice) * unitY));
			CString priceTxt = ctx.realtimeData.IsETF() ? CCommon::FormatETFPrice(labelPrice) : CCommon::FormatFloat(labelPrice);
			CSize sz = memDC.GetTextExtent(priceTxt);
			int labelX = -sz.cx - g_data.RDPI(4);
			int labelY = y - sz.cy / 2;
			if (ctx.realtimeData.prevClosePrice > 0 && labelPrice > ctx.realtimeData.prevClosePrice + ctx.niceStep * 0.01)
				memDC.SetTextColor(COLOR_RED_UP);
			else if (ctx.realtimeData.prevClosePrice > 0 && labelPrice < ctx.realtimeData.prevClosePrice - ctx.niceStep * 0.01)
				memDC.SetTextColor(COLOR_GREEN_DOWN);
			else
				memDC.SetTextColor(COLOR_BLACK);
			memDC.TextOut(labelX, labelY, priceTxt);
		}

		if (ctx.realtimeData.prevClosePrice > 0
			&& ctx.realtimeData.prevClosePrice >= ctx.minPrice && ctx.realtimeData.prevClosePrice <= ctx.maxPrice)
		{
			memDC.SetTextColor(COLOR_GRAY_PURPLE);
			CString prevTxt = ctx.realtimeData.IsETF() ? CCommon::FormatETFPrice(ctx.realtimeData.prevClosePrice) : CCommon::FormatFloat(ctx.realtimeData.prevClosePrice);
			CSize prevSize = memDC.GetTextExtent(prevTxt);
			int prevY = ctx.priceChartTop + ctx.priceChartHeight - static_cast<int>(round((ctx.realtimeData.prevClosePrice - ctx.minPrice) * unitY));
			int labelY = prevY - prevSize.cy / 2;
			memDC.TextOut(-prevSize.cx - g_data.RDPI(4), labelY, prevTxt);
		}
		memDC.SetBkMode(oldBkMode);
		return;
	}

	STOCK::Price priceLimit = ctx.realtimeData.priceLimit;
	int oldBkMode = memDC.SetBkMode(TRANSPARENT);

	memDC.SetTextColor(COLOR_RED_UP);
	CString upperLimitTxt = CCommon::FormatFloat(ctx.realtimeData.prevClosePrice + priceLimit);
	CSize upperSize = memDC.GetTextExtent(upperLimitTxt);
	int upperY = ctx.priceChartTop - upperSize.cy / 2;
	upperY = max(ctx.priceChartTop, min(upperY, ctx.priceChartTop + ctx.priceChartHeight - upperSize.cy));
	memDC.TextOut(-upperSize.cx - g_data.RDPI(4), upperY, upperLimitTxt);

	memDC.SetTextColor(COLOR_GREEN_DOWN);
	CString lowerLimitTxt = CCommon::FormatFloat(ctx.realtimeData.prevClosePrice - priceLimit);
	CSize lowerSize = memDC.GetTextExtent(lowerLimitTxt);
	int lowerY = ctx.priceChartTop + ctx.priceChartHeight - lowerSize.cy / 2;
	lowerY = max(ctx.priceChartTop, min(lowerY, ctx.priceChartTop + ctx.priceChartHeight - lowerSize.cy));
	memDC.TextOut(-lowerSize.cx - g_data.RDPI(4), lowerY, lowerLimitTxt);

	memDC.SetTextColor(COLOR_GRAY_PURPLE);
	CString middleTxt = CCommon::FormatFloat(ctx.realtimeData.prevClosePrice);
	CSize midSize = memDC.GetTextExtent(middleTxt);
	int midY = ctx.priceChartTop + ctx.priceChartHeight / 2 - midSize.cy / 2;
	midY = max(ctx.priceChartTop, min(midY, ctx.priceChartTop + ctx.priceChartHeight - midSize.cy));
	memDC.TextOut(-midSize.cx - g_data.RDPI(4), midY, middleTxt);

	memDC.SetBkMode(oldBkMode);
}

void CTimelineChart::DrawTimelineCostAndProfitLines(CDC& memDC, const TimelineDrawContext& ctx, const HoverState& hover)
{
	(void)memDC;
	(void)ctx;
	(void)hover;
}

void CTimelineChart::DrawTimelineGridAndLines(CDC& memDC, const TimelineDrawContext& ctx, const HoverState& hover)
{
	DrawTimelineBackgroundHighlights(memDC, ctx);
	DrawTimelineGridLines(memDC, ctx);
	DrawTimelinePriceLabels(memDC, ctx);
	DrawTimelineCostAndProfitLines(memDC, ctx, hover);
}

void CTimelineChart::DrawTimelinePriceCurve(CDC& memDC, const TimelineDrawContext& ctx, const HoverState& hover)
{
	const auto& timelinePoint = *ctx.timelinePoint;
	if (timelinePoint.empty())
		return;

	const int totalPoints = static_cast<int>(timelinePoint.size());
	const int xAxisPts = ctx.xAxisPoints > 0 ? ctx.xAxisPoints : totalPoints;

	STOCK::Price maxPrice = ctx.maxPrice;
	STOCK::Price minPrice = ctx.minPrice;
	double unitY = ctx.unitY;
	if (maxPrice <= 0 || minPrice < 0 || maxPrice <= minPrice || unitY <= 0)
	{
		STOCK::Price priceLimit = ctx.realtimeData.priceLimit;
		maxPrice = ctx.realtimeData.prevClosePrice + priceLimit;
		minPrice = ctx.realtimeData.prevClosePrice - priceLimit;
		const int pricePaddingY = g_data.RDPI(10);
		double paddingPrice = (maxPrice - minPrice) * pricePaddingY / ctx.priceChartHeight;
		maxPrice += paddingPrice;
		minPrice -= paddingPrice;
		unitY = ctx.priceChartHeight / (maxPrice - minPrice);
	}

	CPen pKLine(PS_SOLID, 2, COLOR_DARK_GRAY_BORDER);
	memDC.SelectObject(&pKLine);

	std::vector<CPoint> dataPoints;
	dataPoints.reserve(timelinePoint.size());
	for (int i = 0; i < totalPoints; i++)
	{
		const auto& item = timelinePoint[i];
		int pointX = static_cast<int>(ctx.chartWidth / static_cast<float>(xAxisPts) * i) + static_cast<int>(ctx.chartWidth / static_cast<float>(xAxisPts) / 2);
		double yVal = (item.price - minPrice) * unitY;
		dataPoints.push_back(CPoint(pointX, static_cast<int>(round(yVal))));
	}

	if (!dataPoints.empty())
	{
		memDC.MoveTo(dataPoints[0].x, ctx.priceChartTop + ctx.priceChartHeight - dataPoints[0].y);
		for (int i = 1; i < static_cast<int>(dataPoints.size()); i++)
			memDC.LineTo(dataPoints[i].x, ctx.priceChartTop + ctx.priceChartHeight - dataPoints[i].y);

		// 最高/最低价标签
		{
			STOCK::Price hiPrice = 0, loPrice = (std::numeric_limits<STOCK::Price>::max)();
			int hiIdx = -1, loIdx = -1;
			for (int i = 0; i < totalPoints; i++)
			{
				STOCK::Price p = timelinePoint[i].price;
				if (p > 0)
				{
					if (p > hiPrice) { hiPrice = p; hiIdx = i; }
					if (p >= hiPrice) { hiPrice = p; hiIdx = i; }
					if (p < loPrice) { loPrice = p; loIdx = i; }
					if (p <= loPrice) { loPrice = p; loIdx = i; }
				}
			}

			STOCK::Price prevClose = ctx.realtimeData.prevClosePrice;

			if (hiIdx >= 0 && hiPrice > 0)
			{
				int hiX = dataPoints[hiIdx].x;
				int hiY = ctx.priceChartTop + ctx.priceChartHeight - dataPoints[hiIdx].y;
				DrawPricePointLabel(memDC, hiX, hiY, 0, ctx.priceChartTop, ctx.chartWidth, ctx.priceChartHeight,
					hiPrice, true, COLOR_RED_UP);
			}

			if (loIdx >= 0 && loPrice > 0 && loIdx != hiIdx)
			{
				int loX = dataPoints[loIdx].x;
				int loY = ctx.priceChartTop + ctx.priceChartHeight - dataPoints[loIdx].y;
				DrawPricePointLabel(memDC, loX, loY, 0, ctx.priceChartTop, ctx.chartWidth, ctx.priceChartHeight,
					loPrice, false, COLOR_GREEN_DOWN);
			}
		}

		// 均价线
		CPen avgLinePen(PS_SOLID, 1, COLOR_GOLDEN);
		CPen* pOldAvgPen = memDC.SelectObject(&avgLinePen);
		bool firstAvgPoint = true;
		for (int i = 0; i < totalPoints; i++)
		{
			const auto& item = timelinePoint[i];
			if (item.averagePrice <= 0)
			{
				firstAvgPoint = true;
				continue;
			}
			int pointX = static_cast<int>(ctx.chartWidth / static_cast<float>(xAxisPts) * i) + static_cast<int>(ctx.chartWidth / static_cast<float>(xAxisPts) / 2);
			int py = ctx.priceChartTop + ctx.priceChartHeight - static_cast<int>(round((item.averagePrice - minPrice) * unitY));
			py = max(ctx.priceChartTop, min(py, ctx.priceChartTop + ctx.priceChartHeight));
			if (firstAvgPoint)
			{
				memDC.MoveTo(pointX, py);
				firstAvgPoint = false;
			}
			else
			{
				memDC.LineTo(pointX, py);
			}
		}
		memDC.SelectObject(pOldAvgPen);
	}

	// MA5/MA10/MA20均线
	if (hover.showMA)
	{
		const COLORREF ma5Color = RGB(0, 0, 230);
		const COLORREF ma10Color = RGB(0, 166, 235);
		const COLORREF ma20Color = RGB(169, 102, 186);

		auto drawMALine = [&](int fieldOffset, COLORREF color) {
			CPen maPen(PS_SOLID, 1, color);
			memDC.SelectObject(&maPen);
			bool first = true;
			for (int i = 0; i < totalPoints; i++)
			{
				const auto& item = timelinePoint[i];
				STOCK::Price maVal = 0;
				switch (fieldOffset)
				{
				case 5: maVal = item.ma5; break;
				case 10: maVal = item.ma10; break;
				case 20: maVal = item.ma20; break;
				}
				if (maVal <= 0) { first = true; continue; }
				int pointX = static_cast<int>(ctx.chartWidth / static_cast<float>(xAxisPts) * i) + static_cast<int>(ctx.chartWidth / static_cast<float>(xAxisPts) / 2);
				double yVal = (maVal - minPrice) * unitY;
				int py = ctx.priceChartTop + ctx.priceChartHeight - static_cast<int>(round(yVal));
				if (first)
				{
					memDC.MoveTo(pointX, py);
					first = false;
				}
				else
				{
					memDC.LineTo(pointX, py);
				}
			}
			};

		drawMALine(5, ma5Color);
		drawMALine(10, ma10Color);
		drawMALine(20, ma20Color);
	}

	// 布林带
	if (hover.showBollBands)
	{
		const int N = 20;
		const int K = 2;

		const auto& fullData = ctx.fullTimeline ? *ctx.fullTimeline : timelinePoint;

		std::vector<double> upperBand(totalPoints, 0);
		std::vector<double> middleBand(totalPoints, 0);
		std::vector<double> lowerBand(totalPoints, 0);

		for (int i = 0; i < totalPoints; i++)
		{
			int globalIdx = ctx.startIndex + i;
			if (globalIdx < N - 1)
			{
				upperBand[i] = middleBand[i] = lowerBand[i] = 0;
				continue;
			}
			double sum = 0;
			for (int j = globalIdx - N + 1; j <= globalIdx; j++)
			{
				sum += fullData[j].price;
			}
			double ma = sum / N;
			double variance = 0;
			for (int j = globalIdx - N + 1; j <= globalIdx; j++)
			{
				double diff = fullData[j].price - ma;
				variance += diff * diff;
			}
			double stddev = std::sqrt(variance / N);
			middleBand[i] = ma;
			upperBand[i] = ma + K * stddev;
			lowerBand[i] = ma - K * stddev;
		}

		auto priceToY = [&](double price) -> int {
			return ctx.priceChartTop + ctx.priceChartHeight - static_cast<int>(round((price - minPrice) * unitY));
			};

		auto drawBandLine = [&](const std::vector<double>& band, COLORREF color) {
			CPen bandPen(PS_SOLID, 1, color);
			memDC.SelectObject(&bandPen);
			bool first = true;
			for (int i = 0; i < totalPoints; i++)
			{
				if (band[i] <= 0) continue;
				int pointX = static_cast<int>(ctx.chartWidth / static_cast<float>(xAxisPts) * i) + static_cast<int>(ctx.chartWidth / static_cast<float>(xAxisPts) / 2);
				int py = priceToY(band[i]);
				if (first)
				{
					memDC.MoveTo(pointX, py);
					first = false;
				}
				else
				{
					memDC.LineTo(pointX, py);
				}
			}
			};

		drawBandLine(upperBand, COLOR_RED_UP);
		drawBandLine(middleBand, RGB(0, 0, 230));
		drawBandLine(lowerBand, COLOR_GREEN_DOWN);
	}

	// 振幅上下线
	if (hover.showAmplitudeBands)
	{
		auto stockData = g_data.GetStockData(hover.stockId);
		auto* dayKLineObj = stockData ? stockData->getKLineData() : nullptr;
		double avgAmplitude = dayKLineObj ? dayKLineObj->CalculateAverageAmplitude(5) : 0;
		if (avgAmplitude > 0)
		{
			double ampRatio = avgAmplitude / 100.0 / 2.0;

			auto priceToY = [&](double price) -> int {
				int py = ctx.priceChartTop + ctx.priceChartHeight - static_cast<int>(round((price - minPrice) * unitY));
				return max(ctx.priceChartTop, min(py, ctx.priceChartTop + ctx.priceChartHeight));
				};

			{
				CPen upperPen(PS_SOLID, 1, COLOR_RED_UP);
				memDC.SelectObject(&upperPen);
				bool first = true;
				for (int i = 0; i < totalPoints; i++)
				{
					double avgP = timelinePoint[i].averagePrice;
					if (avgP <= 0) { first = true; continue; }
					double upperPrice = avgP * (1 + ampRatio);
					int pointX = static_cast<int>(ctx.chartWidth / static_cast<float>(xAxisPts) * i) + static_cast<int>(ctx.chartWidth / static_cast<float>(xAxisPts) / 2);
					int py = priceToY(upperPrice);
					if (first) { memDC.MoveTo(pointX, py); first = false; }
					else { memDC.LineTo(pointX, py); }
				}
			}
			{
				CPen lowerPen(PS_SOLID, 1, COLOR_GREEN_DOWN);
				memDC.SelectObject(&lowerPen);
				bool first = true;
				for (int i = 0; i < totalPoints; i++)
				{
					double avgP = timelinePoint[i].averagePrice;
					if (avgP <= 0) { first = true; continue; }
					double lowerPrice = avgP * (1 - ampRatio);
					int pointX = static_cast<int>(ctx.chartWidth / static_cast<float>(xAxisPts) * i) + static_cast<int>(ctx.chartWidth / static_cast<float>(xAxisPts) / 2);
					int py = priceToY(lowerPrice);
					if (first) { memDC.MoveTo(pointX, py); first = false; }
					else { memDC.LineTo(pointX, py); }
				}
			}

			double lastAvgP = timelinePoint.back().averagePrice;
			if (lastAvgP > 0)
			{
				CString upperLabel, lowerLabel;
				upperLabel.Format(_T("%.2f"), lastAvgP * (1 + ampRatio));
				lowerLabel.Format(_T("%.2f"), lastAvgP * (1 - ampRatio));
				int labelX = ctx.chartLeft + ctx.chartWidth + 2;
				int upperY = priceToY(lastAvgP * (1 + ampRatio));
				int lowerY = priceToY(lastAvgP * (1 - ampRatio));
				memDC.SetTextColor(COLOR_RED_UP);
				memDC.TextOut(labelX, upperY - memDC.GetTextExtent(upperLabel).cy / 2, upperLabel);
				memDC.SetTextColor(COLOR_GREEN_DOWN);
				memDC.TextOut(labelX, lowerY - memDC.GetTextExtent(lowerLabel).cy / 2, lowerLabel);
			}
		}
	}

	// 绘制基金净值曲线
	if (hover.showJZCurve && ctx.realtimeData.IsETF())
	{
		auto priceToY = [&](double price) -> int {
			return ctx.priceChartTop + ctx.priceChartHeight - static_cast<int>(round((price - minPrice) * unitY));
			};

		auto navPoints = g_data.GetDbManager().LoadLatestFundNavCache(hover.stockId);
		if (!navPoints.empty())
		{
			const auto& fullTimeline = *ctx.fullTimeline;
			std::map<std::string, int> fullTimeIndexMap;
			for (int i = 0; i < static_cast<int>(fullTimeline.size()); i++)
			{
				std::string hhmm = fullTimeline[i].time.substr(0, 5);
				fullTimeIndexMap[hhmm] = i;
			}

			std::map<int, double> iopvByIndex;
			for (const auto& nav : navPoints)
			{
				auto it = fullTimeIndexMap.find(nav.time);
				if (it != fullTimeIndexMap.end())
					iopvByIndex[it->second] = nav.iopv;
			}

			if (ctx.realtimeData.iopv > 0 && !fullTimeline.empty())
			{
				int lastIdx = static_cast<int>(fullTimeline.size()) - 1;
				iopvByIndex[lastIdx] = ctx.realtimeData.iopv;
			}

			if (!iopvByIndex.empty())
			{
				const COLORREF navColor = RGB(160, 32, 240);
				CPen navPen(PS_SOLID, 1, navColor);
				CPen* pOldPen = memDC.SelectObject(&navPen);
				bool firstNavPoint = true;

				int startIdx = ctx.startIndex;
				int visCount = ctx.visibleCount;

				for (const auto& kv : iopvByIndex)
				{
					int fullIdx = kv.first;
					double iopvVal = kv.second;

					if (fullIdx < startIdx || fullIdx >= startIdx + visCount)
						continue;

					int relIdx = fullIdx - startIdx;
					int pointX = static_cast<int>(ctx.chartWidth / static_cast<float>(xAxisPts) * relIdx) + static_cast<int>(ctx.chartWidth / static_cast<float>(xAxisPts) / 2);
					int pointY = priceToY(iopvVal);
					if (firstNavPoint)
					{
						memDC.MoveTo(pointX, pointY);
						firstNavPoint = false;
					}
					else
					{
						memDC.LineTo(pointX, pointY);
					}
				}
				memDC.SelectObject(pOldPen);
			}
		}
	}

	// 绘制智能分析买卖点标记
	{
		auto stockData = g_data.GetStockData(hover.stockId);
		if (stockData)
		{
			auto min30KLineObj = stockData->getMin30KLineData();

			if (min30KLineObj && min30KLineObj->data.size() >= 22)
			{
				std::vector<STOCK::Bar> bars30;
				bars30.reserve(min30KLineObj->data.size());
				for (const auto& kp : min30KLineObj->data) bars30.push_back(STOCK::Bar::FromKLinePoint(kp));

				auto buySignals = std::vector<bool>(totalPoints, false);
				auto sellSignals = std::vector<bool>(totalPoints, false);
				auto forbidSignals = std::vector<bool>(totalPoints, false);
				auto buyReasons = std::vector<CString>(totalPoints);
				auto sellReasons = std::vector<CString>(totalPoints);
				auto noLabelSignals = std::vector<bool>(totalPoints, false);

				if (!hover.isKLineMode && ctx.fullTimeline && ctx.fullTimeline->size() >= 120)
				{
					std::vector<STOCK::Bar> bars1m = CSignalAnalyzer::ConvertTimelineToBars(*ctx.fullTimeline);
					auto ar = CSignalAnalyzer::AnalyzeSignalAtFromTimeline(bars1m, bars30, static_cast<int>(bars1m.size()) - 1);
					auto& allSignals = ar.batchSignals;

					int fullToVisibleOffset = ctx.startIndex;

					std::vector<bool> filteredSignals(allSignals.size(), false);
					{
						int lastDrawnBuyBar = -10, lastDrawnSellBar = -10;
						for (size_t i = 0; i < allSignals.size(); i++)
						{
							if (allSignals[i].isForbid) continue;
							int bi = allSignals[i].barIndex;
							if (allSignals[i].isBuy)
							{
								if (bi - lastDrawnBuyBar < 5)
									filteredSignals[i] = true;
								else
									lastDrawnBuyBar = bi;
							}
							else
							{
								if (bi - lastDrawnSellBar < 5)
									filteredSignals[i] = true;
								else
									lastDrawnSellBar = bi;
							}
						}
					}

					for (size_t si = 0; si < allSignals.size(); si++)
					{
						const auto& sig = allSignals[si];
						int k = sig.barIndex - fullToVisibleOffset;
						if (k < 0 || k >= totalPoints) continue;
						if (sig.isForbid)
						{
							forbidSignals[k] = true;
							buySignals[k] = false;
						}
						else if (!forbidSignals[k] || !sig.isBuy)
						{
							if (sig.isBuy)
							{
								if (!forbidSignals[k])
								{
									buySignals[k] = true;
									buyReasons[k] = sig.reason;
									if (filteredSignals[si])
										noLabelSignals[k] = true;
								}
							}
							else
							{
								sellSignals[k] = true;
								sellReasons[k] = sig.reason;
								if (filteredSignals[si])
									noLabelSignals[k] = true;
							}
						}
					}
				}
				else if (hover.isKLineMode)
				{
					auto min5KLineObj = stockData->getMin5KLineData();
					if (min5KLineObj && min5KLineObj->data.size() >= 60)
					{
						std::string latestBarDate;
						for (auto it5 = min5KLineObj->data.rbegin(); it5 != min5KLineObj->data.rend(); ++it5)
						{
							auto sp = it5->day.find(' ');
							if (sp != std::string::npos)
							{
								latestBarDate = it5->day.substr(0, sp);
								break;
							}
						}

						std::vector<STOCK::Bar> bars5;
						bars5.reserve(min5KLineObj->data.size());
						for (const auto& kp : min5KLineObj->data) bars5.push_back(STOCK::Bar::FromKLinePoint(kp));

						auto ar = CSignalAnalyzer::AnalyzeSignalAt(bars5, bars30, static_cast<int>(bars5.size()) - 1);
						auto& allSignals = ar.batchSignals;

						std::vector<CSignalAnalyzer::SmartSignalPoint> signals;
						for (const auto& sig : allSignals)
						{
							if (sig.barIndex < 0 || sig.barIndex >= static_cast<int>(min5KLineObj->data.size()))
								continue;
							if (!latestBarDate.empty())
							{
								const auto& bar5Time = min5KLineObj->data[sig.barIndex].day;
								auto sp = bar5Time.find(' ');
								if (sp != std::string::npos && bar5Time.substr(0, sp) != latestBarDate)
									continue;
							}
							signals.push_back(sig);
						}

						std::map<std::string, int> timeIndexMap;
						for (int k = 0; k < totalPoints; k++)
						{
							const auto& t = timelinePoint[k].time;
							timeIndexMap[t] = k;
							if (t.length() > 5 && t[5] == ':')
								timeIndexMap[t.substr(0, 5)] = k;
						}

						std::set<int> klineFilteredBarIndices;
						{
							bool lastDirIsBuy = false;
							bool hasLastDir = false;
							int lastBarIdx = -1;
							for (const auto& sig : signals)
							{
								if (sig.isForbid) { hasLastDir = false; continue; }
								int bi = sig.barIndex;
								if (bi == lastBarIdx) continue;
								if (hasLastDir && sig.isBuy == lastDirIsBuy)
									klineFilteredBarIndices.insert(bi);
								else
								{
									lastDirIsBuy = sig.isBuy;
									hasLastDir = true;
								}
								lastBarIdx = bi;
							}
						}

						for (const auto& sig : signals)
						{
							const auto& bar5Time = min5KLineObj->data[sig.barIndex].day;
							std::string timeStr;
							auto spacePos = bar5Time.find(' ');
							if (spacePos != std::string::npos && bar5Time.length() > spacePos + 5)
								timeStr = bar5Time.substr(spacePos + 1, 5);
							else if (bar5Time.length() >= 5 && bar5Time[2] == ':')
								timeStr = bar5Time.substr(0, 5);
							else
								timeStr = bar5Time;

							auto it = timeIndexMap.find(timeStr);
							if (it == timeIndexMap.end() && timeStr.length() >= 8)
								it = timeIndexMap.find(timeStr.substr(0, 5));
							if (it != timeIndexMap.end())
							{
								int k = it->second;
								if (sig.isForbid)
								{
									forbidSignals[k] = true;
									buySignals[k] = false;
								}
								else if (!forbidSignals[k] || !sig.isBuy)
								{
									if (sig.isBuy)
									{
										if (!forbidSignals[k])
										{
											buySignals[k] = true;
											buyReasons[k] = sig.reason;
											if (klineFilteredBarIndices.count(sig.barIndex))
												noLabelSignals[k] = true;
										}
									}
									else
									{
										sellSignals[k] = true;
										sellReasons[k] = sig.reason;
										if (klineFilteredBarIndices.count(sig.barIndex))
											noLabelSignals[k] = true;
									}
								}
							}
						}
					}
				}

				const int dotR = g_data.RDPI(3);
				const int labelOff = g_data.RDPI(8);
				int oldBkMode = memDC.SetBkMode(TRANSPARENT);
				auto drawSignalArrow = [&](int x, int fromY, int toY, COLORREF color) {
					CPen pen(PS_SOLID, 1, color);
					CPen* pOldP = memDC.SelectObject(&pen);
					memDC.MoveTo(x, fromY);
					memDC.LineTo(x, toY);

					int dir = (toY >= fromY) ? 1 : -1;
					int arrowLen = g_data.RDPI(4);
					int arrowHalf = g_data.RDPI(3);
					memDC.MoveTo(x, toY);
					memDC.LineTo(x - arrowHalf, toY - dir * arrowLen);
					memDC.MoveTo(x, toY);
					memDC.LineTo(x + arrowHalf, toY - dir * arrowLen);
					memDC.SelectObject(pOldP);
					};

				for (int i = 0; i < totalPoints; i++)
				{
					if (!buySignals[i] && !sellSignals[i] && !forbidSignals[i])
						continue;
					if (i >= static_cast<int>(dataPoints.size()))
						continue;

					int ptX = dataPoints[i].x;
					int ptY = ctx.priceChartTop + ctx.priceChartHeight - dataPoints[i].y;

					if (buySignals[i])
					{
						CBrush brush(COLOR_GREEN_DOWN);
						CPen pen(PS_SOLID, 1, COLOR_GREEN_DOWN);
						CBrush* pOldB = memDC.SelectObject(&brush);
						CPen* pOldP = memDC.SelectObject(&pen);
						memDC.Ellipse(ptX - dotR, ptY - dotR, ptX + dotR, ptY + dotR);
						if (!noLabelSignals[i])
						{
							memDC.SetTextColor(COLOR_GREEN_DOWN);
							CString label = buyReasons[i].IsEmpty() ? _T("B") : buyReasons[i];
							CSize sz = memDC.GetTextExtent(label);
							int labelY = ptY + dotR + labelOff;
							memDC.TextOut(ptX - sz.cx / 2, labelY, label);
							drawSignalArrow(ptX, labelY, ptY + dotR, COLOR_GREEN_DOWN);
						}
						memDC.SelectObject(pOldB);
						memDC.SelectObject(pOldP);
					}
					else if (sellSignals[i])
					{
						CBrush brush(COLOR_RED_UP);
						CPen pen(PS_SOLID, 1, COLOR_RED_UP);
						CBrush* pOldB = memDC.SelectObject(&brush);
						CPen* pOldP = memDC.SelectObject(&pen);
						memDC.Ellipse(ptX - dotR, ptY - dotR, ptX + dotR, ptY + dotR);
						if (!noLabelSignals[i])
						{
							memDC.SetTextColor(COLOR_RED_UP);
							CString label = sellReasons[i].IsEmpty() ? _T("S") : sellReasons[i];
							CSize sz = memDC.GetTextExtent(label);
							int labelY = ptY - dotR - labelOff - sz.cy;
							memDC.TextOut(ptX - sz.cx / 2, labelY, label);
							drawSignalArrow(ptX, labelY + sz.cy, ptY - dotR, COLOR_RED_UP);
						}
						memDC.SelectObject(pOldB);
						memDC.SelectObject(pOldP);
					}
				}
				memDC.SetBkMode(oldBkMode);
			}
		}
	}
}

void CTimelineChart::DrawTimelineHoverOverlay(CDC& memDC, const TimelineDrawContext& ctx, const HoverState& hover)
{
	if (!hover.isHoveringVolume || hover.hoveredBarIndex < 0)
		return;

	const auto& timelinePoint = *ctx.timelinePoint;
	if (hover.hoveredBarIndex >= static_cast<int>(timelinePoint.size()))
		return;

	STOCK::Price maxPrice = ctx.maxPrice;
	STOCK::Price minPrice = ctx.minPrice;
	double unitY = ctx.unitY;
	if (maxPrice <= 0 || minPrice < 0 || maxPrice <= minPrice || unitY <= 0)
	{
		STOCK::Price priceLimit = ctx.realtimeData.priceLimit;
		maxPrice = ctx.realtimeData.prevClosePrice + priceLimit;
		minPrice = ctx.realtimeData.prevClosePrice - priceLimit;
		const int pricePaddingY = g_data.RDPI(10);
		double paddingPrice = (maxPrice - minPrice) * pricePaddingY / ctx.priceChartHeight;
		maxPrice += paddingPrice;
		minPrice -= paddingPrice;
		unitY = ctx.priceChartHeight / (maxPrice - minPrice);
	}

	const int xSlots = ctx.xAxisPoints > 0 ? ctx.xAxisPoints : static_cast<int>(timelinePoint.size());
	const auto& item = timelinePoint[hover.hoveredBarIndex];
	int hoverX = static_cast<int>(ctx.chartWidth / static_cast<float>(xSlots) * hover.hoveredBarIndex + ctx.chartWidth / static_cast<float>(xSlots) / 2);

	int dotY = ctx.priceChartTop + ctx.priceChartHeight - static_cast<int>(round((item.price - minPrice) * unitY));

	COLORREF dotColor = (item.price >= ctx.realtimeData.prevClosePrice) ? COLOR_RED_UP : COLOR_GREEN_DOWN;

	CPen crossPen(PS_DOT, 1, RGB(70, 130, 210));
	memDC.SelectObject(&crossPen);
	memDC.MoveTo(hoverX, ctx.priceChartTop);
	memDC.LineTo(hoverX, ctx.positionY);

	memDC.MoveTo(0, dotY);
	memDC.LineTo(ctx.chartWidth, dotY);

	int yAxisW = g_data.RDPI(50);
	CPoint origOrg = memDC.GetViewportOrg();
	memDC.OffsetViewportOrg(-yAxisW, 0);

	CString hoverPriceStr = ctx.realtimeData.IsETF() ? CCommon::FormatETFPrice(item.price) : CCommon::FormatFloat(item.price);
	CSize hoverPriceSize = memDC.GetTextExtent(hoverPriceStr);
	int priceLabelX = yAxisW - hoverPriceSize.cx - g_data.RDPI(3);
	int priceLabelY = dotY - hoverPriceSize.cy / 2;
	priceLabelY = max(ctx.priceChartTop, min(priceLabelY, ctx.priceChartTop + ctx.priceChartHeight - hoverPriceSize.cy));
	CRect priceBgRect(priceLabelX - g_data.RDPI(2), priceLabelY, priceLabelX + hoverPriceSize.cx + g_data.RDPI(2), priceLabelY + hoverPriceSize.cy);
	memDC.FillSolidRect(priceBgRect, RGB(200, 220, 255));
	memDC.SetTextColor(dotColor);
	memDC.TextOut(priceLabelX, priceLabelY, hoverPriceStr);

	memDC.SetViewportOrg(origOrg);

	{
		STOCK::Volume maxVol = 0;
		for (const auto& tp : timelinePoint)
		{
			if (tp.volume > maxVol)
				maxVol = tp.volume;
		}
		if (maxVol > 0 && item.volume > 0)
		{
			float volRatio = static_cast<float>(item.volume) / static_cast<float>(maxVol);
			int volBarY = ctx.volumeChartTop + ctx.volumeChartHeight - static_cast<int>(volRatio * ctx.volumeChartHeight);
			CPen volCrossPen(PS_DOT, 1, RGB(70, 130, 210));
			memDC.SelectObject(&volCrossPen);
			memDC.MoveTo(0, volBarY);
			memDC.LineTo(ctx.chartWidth, volBarY);

			memDC.OffsetViewportOrg(-yAxisW, 0);
			STOCK::Volume volInLots = item.volume / 100;
			CString volLabel = CCommon::FormatVolumeInt(volInLots);
			CSize volLabelSize = memDC.GetTextExtent(volLabel);
			int volLabelX = yAxisW - volLabelSize.cx - g_data.RDPI(3);
			int volLabelY = volBarY - volLabelSize.cy / 2;
			volLabelY = max(ctx.volumeChartTop, min(volLabelY, ctx.volumeChartTop + ctx.volumeChartHeight - volLabelSize.cy));
			CRect volBgRect(volLabelX - g_data.RDPI(2), volLabelY, volLabelX + volLabelSize.cx + g_data.RDPI(2), volLabelY + volLabelSize.cy);
			memDC.FillSolidRect(volBgRect, RGB(200, 220, 255));
			memDC.SetTextColor(COLOR_GRAY_TEXT);
			memDC.TextOut(volLabelX, volLabelY, volLabel);
			memDC.SetViewportOrg(origOrg);
		}
	}

	CString timeStr;
	if (!item.fullTime.empty() && (hover.isMin5KLineMode || hover.isMin30KLineMode))
	{
		timeStr = CString(item.fullTime.c_str());
		if (timeStr.GetLength() >= 16)
			timeStr = timeStr.Left(16);
	}
	else
	{
		timeStr = CString(item.time.c_str());
		if (timeStr.GetLength() >= 5)
			timeStr = timeStr.Left(5);
	}
	CSize timeSize = memDC.GetTextExtent(timeStr);
	int timeLabelX = hoverX - timeSize.cx / 2;
	timeLabelX = max(g_data.RDPI(2), min(timeLabelX, ctx.chartWidth - timeSize.cx - g_data.RDPI(2)));
	int timeLabelY = ctx.positionY;
	CRect timeBgRect(timeLabelX - g_data.RDPI(3), timeLabelY, timeLabelX + timeSize.cx + g_data.RDPI(3), timeLabelY + timeSize.cy);
	memDC.FillSolidRect(timeBgRect, RGB(220, 235, 250));
	memDC.SetTextColor(COLOR_BLACK);
	memDC.SetBkMode(TRANSPARENT);
	memDC.TextOut(timeLabelX, timeLabelY, timeStr);
}

void CTimelineChart::DrawMin5KLinePriceChart(CDC& memDC, const TimelineDrawContext& ctx, const HoverState& hover)
{
	const auto& timelinePoint = *ctx.timelinePoint;
	if (timelinePoint.empty())
		return;

	const int totalPoints = static_cast<int>(timelinePoint.size());
	const int xAxisPts = ctx.xAxisPoints > 0 ? ctx.xAxisPoints : totalPoints;

	STOCK::Price maxPrice = ctx.maxPrice;
	STOCK::Price minPrice = ctx.minPrice;
	double unitY = ctx.unitY;
	if (maxPrice <= 0 || minPrice < 0 || maxPrice <= minPrice || unitY <= 0)
	{
		STOCK::Price priceLimit = ctx.realtimeData.priceLimit;
		maxPrice = ctx.realtimeData.prevClosePrice + priceLimit;
		minPrice = ctx.realtimeData.prevClosePrice - priceLimit;
		const int pricePaddingY = g_data.RDPI(10);
		double paddingPrice = (maxPrice - minPrice) * pricePaddingY / ctx.priceChartHeight;
		maxPrice += paddingPrice;
		minPrice -= paddingPrice;
		unitY = ctx.priceChartHeight / (maxPrice - minPrice);
	}

	const auto& klineData = *ctx.klineData;
	int klineStartIdx = ctx.startIndex;
	int klineEndIdx = klineStartIdx + totalPoints;
	if (klineEndIdx > static_cast<int>(klineData.size()))
		klineEndIdx = static_cast<int>(klineData.size());

	if (klineStartIdx >= static_cast<int>(klineData.size()))
		return;

	float barTotalWidth = static_cast<float>(ctx.chartWidth) / xAxisPts;
	int barWidth = max(1, static_cast<int>(barTotalWidth * 0.7));
	int gap = static_cast<int>(barTotalWidth) - barWidth;
	if (gap < 1) gap = 1;

	auto priceToY = [&](STOCK::Price price) -> int {
		return ctx.priceChartTop + ctx.priceChartHeight - static_cast<int>((price - minPrice) * unitY);
		};

	// 最新一天K线背景高亮
	{
		if (!klineData.empty() && (hover.isMin5KLineMode || hover.isMin30KLineMode))
		{
			const auto& lastKp = klineData.back();
			std::string lastDate;
			auto spacePos = lastKp.day.find(' ');
			if (spacePos != std::string::npos)
				lastDate = lastKp.day.substr(0, spacePos);
			else
				lastDate = lastKp.day;

			if (!lastDate.empty())
			{
				int firstIdx = -1, lastIdx = -1;
				for (int i = 0; i < totalPoints && (klineStartIdx + i) < klineEndIdx; i++)
				{
					const auto& kp = klineData[klineStartIdx + i];
					std::string kpDate;
					auto sp = kp.day.find(' ');
					if (sp != std::string::npos)
						kpDate = kp.day.substr(0, sp);
					else
						kpDate = kp.day;
					if (kpDate == lastDate)
					{
						if (firstIdx < 0) firstIdx = i;
						lastIdx = i;
					}
				}

				if (firstIdx >= 0 && lastIdx >= 0)
				{
					int xLeft = static_cast<int>(ctx.chartWidth / static_cast<float>(xAxisPts) * firstIdx);
					int xRight = static_cast<int>(ctx.chartWidth / static_cast<float>(xAxisPts) * (lastIdx + 1));

					CBrush highlightBrush(COLOR_LIGHT_BLUE);
					memDC.FillRect(CRect(xLeft, ctx.priceChartTop, xRight, ctx.priceChartTop + ctx.priceChartHeight), &highlightBrush);
					memDC.FillRect(CRect(xLeft, ctx.volumeChartTop, xRight, ctx.volumeChartTop + ctx.volumeChartHeight), &highlightBrush);
					memDC.FillRect(CRect(xLeft, ctx.macdChartTop, xRight, ctx.macdChartTop + ctx.macdChartHeight), &highlightBrush);

					DrawTimelineGridLines(memDC, ctx);
				}
			}
		}
	}

	STOCK::Price prevClose = ctx.realtimeData.prevClosePrice;

	for (int i = 0; i < totalPoints && (klineStartIdx + i) < klineEndIdx; i++)
	{
		const auto& kp = klineData[klineStartIdx + i];
		if (kp.close <= 0) continue;

		int centerX = static_cast<int>(ctx.chartWidth / static_cast<float>(totalPoints) * i) + static_cast<int>(barTotalWidth / 2);
		int leftX = centerX - barWidth / 2;

		bool isUp = (kp.close >= kp.open);
		COLORREF barColor = isUp ? COLOR_RED_UP : COLOR_GREEN_DOWN;

		int openY = priceToY(kp.open);
		int closeY = priceToY(kp.close);
		int highY = priceToY(kp.high);
		int lowY = priceToY(kp.low);

		openY = max(ctx.priceChartTop, min(openY, ctx.priceChartTop + ctx.priceChartHeight));
		closeY = max(ctx.priceChartTop, min(closeY, ctx.priceChartTop + ctx.priceChartHeight));
		highY = max(ctx.priceChartTop, min(highY, ctx.priceChartTop + ctx.priceChartHeight));
		lowY = max(ctx.priceChartTop, min(lowY, ctx.priceChartTop + ctx.priceChartHeight));

		CPen barPen(PS_SOLID, 1, barColor);
		memDC.SelectObject(&barPen);
		memDC.MoveTo(centerX, highY);
		memDC.LineTo(centerX, lowY);

		int bodyTop = min(openY, closeY);
		int bodyBottom = max(openY, closeY);
		int bodyHeight = bodyBottom - bodyTop;
		if (bodyHeight < 1) bodyHeight = 1;

		if (isUp)
		{
			CBrush brush(barColor);
			CBrush* pOldBrush = memDC.SelectObject(&brush);
			memDC.Rectangle(leftX, bodyTop, leftX + barWidth, bodyBottom + 1);
			memDC.SelectObject(pOldBrush);
		}
		else
		{
			CBrush brush(barColor);
			CBrush* pOldBrush = memDC.SelectObject(&brush);
			memDC.Rectangle(leftX, bodyTop, leftX + barWidth, bodyBottom + 1);
			memDC.SelectObject(pOldBrush);
		}
	}

	if (hover.showMA)
	{
		const COLORREF ma5Color = RGB(0, 0, 230);
		const COLORREF ma10Color = RGB(0, 166, 235);
		const COLORREF ma20Color = RGB(169, 102, 186);

		auto drawMALine = [&](int fieldOffset, COLORREF color) {
			CPen maPen(PS_SOLID, 1, color);
			memDC.SelectObject(&maPen);
			bool first = true;
			for (int i = 0; i < totalPoints; i++)
			{
				const auto& item = timelinePoint[i];
				STOCK::Price maVal = 0;
				switch (fieldOffset)
				{
				case 5: maVal = item.ma5; break;
				case 10: maVal = item.ma10; break;
				case 20: maVal = item.ma20; break;
				}
				if (maVal <= 0) { first = true; continue; }
				int pointX = static_cast<int>(ctx.chartWidth / static_cast<float>(totalPoints) * i) + static_cast<int>(ctx.chartWidth / static_cast<float>(totalPoints) / 2);
				double yVal = (maVal - minPrice) * unitY;
				int py = ctx.priceChartTop + ctx.priceChartHeight - static_cast<int>(yVal);
				if (first)
				{
					memDC.MoveTo(pointX, py);
					first = false;
				}
				else
				{
					memDC.LineTo(pointX, py);
				}
			}
			};

		drawMALine(5, ma5Color);
		drawMALine(10, ma10Color);
		drawMALine(20, ma20Color);
	}

	if (hover.showBollBands)
	{
		const int N = 20;
		const int K = 2;

		const auto& fullData = ctx.fullTimeline ? *ctx.fullTimeline : timelinePoint;

		std::vector<double> upperBand(totalPoints, 0);
		std::vector<double> middleBand(totalPoints, 0);
		std::vector<double> lowerBand(totalPoints, 0);

		for (int i = 0; i < totalPoints; i++)
		{
			int globalIdx = ctx.startIndex + i;
			if (globalIdx < N - 1)
			{
				upperBand[i] = middleBand[i] = lowerBand[i] = 0;
				continue;
			}
			double sum = 0;
			for (int j = globalIdx - N + 1; j <= globalIdx; j++)
			{
				sum += fullData[j].price;
			}
			double ma = sum / N;
			double variance = 0;
			for (int j = globalIdx - N + 1; j <= globalIdx; j++)
			{
				double diff = fullData[j].price - ma;
				variance += diff * diff;
			}
			double stddev = std::sqrt(variance / N);
			middleBand[i] = ma;
			upperBand[i] = ma + K * stddev;
			lowerBand[i] = ma - K * stddev;
		}

		auto bandPriceToY = [&](double price) -> int {
			int py = ctx.priceChartTop + ctx.priceChartHeight - static_cast<int>(round((price - minPrice) * unitY));
			return max(ctx.priceChartTop, min(py, ctx.priceChartTop + ctx.priceChartHeight));
			};

		auto drawBandLine = [&](const std::vector<double>& band, COLORREF color) {
			CPen bandPen(PS_SOLID, 1, color);
			memDC.SelectObject(&bandPen);
			bool first = true;
			for (int i = 0; i < totalPoints; i++)
			{
				if (band[i] <= 0) continue;
				int pointX = static_cast<int>(ctx.chartWidth / static_cast<float>(totalPoints) * i) + static_cast<int>(ctx.chartWidth / static_cast<float>(totalPoints) / 2);
				int py = bandPriceToY(band[i]);
				if (first)
				{
					memDC.MoveTo(pointX, py);
					first = false;
				}
				else
				{
					memDC.LineTo(pointX, py);
				}
			}
			};

		drawBandLine(upperBand, COLOR_RED_UP);
		drawBandLine(middleBand, RGB(0, 0, 230));
		drawBandLine(lowerBand, COLOR_GREEN_DOWN);
	}

	if (hover.showAmplitudeBands)
	{
		auto stockData = g_data.GetStockData(hover.stockId);
		auto* dayKLineObj = stockData ? stockData->getKLineData() : nullptr;
		double avgAmplitude = dayKLineObj ? dayKLineObj->CalculateAverageAmplitude(5) : 0;
		if (avgAmplitude > 0)
		{
			double ampRatio = avgAmplitude / 100.0 / 2.0;

			auto priceToY = [&](double price) -> int {
				int py = ctx.priceChartTop + ctx.priceChartHeight - static_cast<int>(round((price - minPrice) * unitY));
				return max(ctx.priceChartTop, min(py, ctx.priceChartTop + ctx.priceChartHeight));
				};

			{
				CPen upperPen(PS_SOLID, 1, COLOR_RED_UP);
				memDC.SelectObject(&upperPen);
				bool first = true;
				for (int i = 0; i < totalPoints; i++)
				{
					double avgP = timelinePoint[i].averagePrice;
					if (avgP <= 0) { first = true; continue; }
					double upperPrice = avgP * (1 + ampRatio);
					int pointX = static_cast<int>(ctx.chartWidth / static_cast<float>(totalPoints) * i) + static_cast<int>(ctx.chartWidth / static_cast<float>(totalPoints) / 2);
					int py = priceToY(upperPrice);
					if (first) { memDC.MoveTo(pointX, py); first = false; }
					else { memDC.LineTo(pointX, py); }
				}
			}
			{
				CPen lowerPen(PS_SOLID, 1, COLOR_GREEN_DOWN);
				memDC.SelectObject(&lowerPen);
				bool first = true;
				for (int i = 0; i < totalPoints; i++)
				{
					double avgP = timelinePoint[i].averagePrice;
					if (avgP <= 0) { first = true; continue; }
					double lowerPrice = avgP * (1 - ampRatio);
					int pointX = static_cast<int>(ctx.chartWidth / static_cast<float>(totalPoints) * i) + static_cast<int>(ctx.chartWidth / static_cast<float>(totalPoints) / 2);
					int py = priceToY(lowerPrice);
					if (first) { memDC.MoveTo(pointX, py); first = false; }
					else { memDC.LineTo(pointX, py); }
				}
			}

			double lastAvgP = timelinePoint.back().averagePrice;
			if (lastAvgP > 0)
			{
				CString upperLabel, lowerLabel;
				upperLabel.Format(_T("%.2f"), lastAvgP * (1 + ampRatio));
				lowerLabel.Format(_T("%.2f"), lastAvgP * (1 - ampRatio));
				int labelX = ctx.chartLeft + ctx.chartWidth + 2;
				int upperY = priceToY(lastAvgP * (1 + ampRatio));
				int lowerY = priceToY(lastAvgP * (1 - ampRatio));
				memDC.SetTextColor(COLOR_RED_UP);
				memDC.TextOut(labelX, upperY - memDC.GetTextExtent(upperLabel).cy / 2, upperLabel);
				memDC.SetTextColor(COLOR_GREEN_DOWN);
				memDC.TextOut(labelX, lowerY - memDC.GetTextExtent(lowerLabel).cy / 2, lowerLabel);
			}
		}
	}

	// 最高/最低价标签
	{
		STOCK::Price hiPrice = 0, loPrice = (std::numeric_limits<STOCK::Price>::max)();
		int hiIdx = -1, loIdx = -1;
		for (int i = 0; i < totalPoints && (klineStartIdx + i) < klineEndIdx; i++)
		{
			const auto& kp = klineData[klineStartIdx + i];
			if (kp.high > 0)
			{
				if (kp.high > hiPrice) { hiPrice = kp.high; hiIdx = i; }
				if (kp.high >= hiPrice) { hiPrice = kp.high; hiIdx = i; }
			}
			if (kp.low > 0)
			{
				if (kp.low < loPrice) { loPrice = kp.low; loIdx = i; }
				if (kp.low <= loPrice) { loPrice = kp.low; loIdx = i; }
			}
		}

		if (hiIdx >= 0 && hiPrice > 0)
		{
			int hiX = static_cast<int>(ctx.chartWidth / static_cast<float>(totalPoints) * hiIdx) + static_cast<int>(barTotalWidth / 2);
			int hiY = priceToY(hiPrice);
			DrawPricePointLabel(memDC, hiX, hiY, 0, ctx.priceChartTop, ctx.chartWidth, ctx.priceChartHeight,
				hiPrice, true, COLOR_RED_UP);
		}

		if (loIdx >= 0 && loPrice > 0 && loIdx != hiIdx)
		{
			int loX = static_cast<int>(ctx.chartWidth / static_cast<float>(totalPoints) * loIdx) + static_cast<int>(barTotalWidth / 2);
			int loY = priceToY(loPrice);
			DrawPricePointLabel(memDC, loX, loY, 0, ctx.priceChartTop, ctx.chartWidth, ctx.priceChartHeight,
				loPrice, false, COLOR_GREEN_DOWN);
		}
	}

	// 智能分析买卖点标记（仅5分钟K线模式）
	if (hover.isMin5KLineMode && !hover.isMin30KLineMode)
	{
		auto stockData = g_data.GetStockData(hover.stockId);
		if (stockData)
		{
			auto min5KLineObj = stockData->getMin5KLineData();
			auto min30KLineObj = stockData->getMin30KLineData();
			if (min5KLineObj && min5KLineObj->data.size() >= 120 &&
				min30KLineObj && min30KLineObj->data.size() >= 22)
			{
				std::vector<STOCK::Bar> bars5, bars30;
				bars5.reserve(min5KLineObj->data.size());
				for (const auto& kp : min5KLineObj->data) bars5.push_back(STOCK::Bar::FromKLinePoint(kp));
				bars30.reserve(min30KLineObj->data.size());
				for (const auto& kp : min30KLineObj->data) bars30.push_back(STOCK::Bar::FromKLinePoint(kp));

				auto ar = CSignalAnalyzer::AnalyzeSignalAt(bars5, bars30, static_cast<int>(bars5.size()) - 1);
				auto& signals = ar.batchSignals;

				std::set<int> kline5FilteredBarIndices;
				{
					bool lastDirIsBuy = false;
					bool hasLastDir = false;
					int lastBarIdx = -1;
					for (const auto& sig : signals)
					{
						if (sig.isForbid) { hasLastDir = false; continue; }
						int bi = sig.barIndex;
						if (bi == lastBarIdx) continue;
						if (hasLastDir && sig.isBuy == lastDirIsBuy)
							kline5FilteredBarIndices.insert(bi);
						else
						{
							lastDirIsBuy = sig.isBuy;
							hasLastDir = true;
						}
						lastBarIdx = bi;
					}
				}

				int oldBkMode = memDC.SetBkMode(TRANSPARENT);
				auto drawSignalArrow = [&](int x, int fromY, int toY, COLORREF color) {
					CPen pen(PS_SOLID, 1, color);
					CPen* pOldP = memDC.SelectObject(&pen);
					memDC.MoveTo(x, fromY);
					memDC.LineTo(x, toY);

					int dir = (toY >= fromY) ? 1 : -1;
					int arrowLen = g_data.RDPI(4);
					int arrowHalf = g_data.RDPI(3);
					memDC.MoveTo(x, toY);
					memDC.LineTo(x - arrowHalf, toY - dir * arrowLen);
					memDC.MoveTo(x, toY);
					memDC.LineTo(x + arrowHalf, toY - dir * arrowLen);
					memDC.SelectObject(pOldP);
					};

				for (const auto& sig : signals)
				{
					int klineIdx = sig.barIndex;
					if (klineIdx < klineStartIdx || klineIdx >= klineEndIdx)
						continue;

					int visibleIdx = klineIdx - klineStartIdx;
					int barX = static_cast<int>(ctx.chartWidth / static_cast<float>(totalPoints) * visibleIdx) + static_cast<int>(barTotalWidth / 2);

					if (sig.isForbid)
					{
						int barY = priceToY(klineData[klineIdx].close);
						CPen pen(PS_SOLID, 2, COLOR_GRAY_TEXT);
						CPen* pOldP = memDC.SelectObject(&pen);
						int r = g_data.RDPI(3);
						memDC.MoveTo(barX - r, barY - r); memDC.LineTo(barX + r, barY + r);
						memDC.MoveTo(barX + r, barY - r); memDC.LineTo(barX - r, barY + r);
						memDC.SelectObject(pOldP);
						continue;
					}

					if (sig.isBuy)
					{
						int barY = priceToY(klineData[klineIdx].low) + g_data.RDPI(2);
						CBrush brush(COLOR_GREEN_DOWN);
						CPen pen(PS_SOLID, 1, COLOR_GREEN_DOWN);
						CBrush* pOldB = memDC.SelectObject(&brush);
						CPen* pOldP = memDC.SelectObject(&pen);
						int r = g_data.RDPI(3);
						int labelOff = g_data.RDPI(8);
						memDC.Ellipse(barX - r, barY, barX + r, barY + 2 * r);
						if (!kline5FilteredBarIndices.count(klineIdx))
						{
							memDC.SetTextColor(COLOR_GREEN_DOWN);
							CSize sz = memDC.GetTextExtent(sig.reason);
							int labelY = barY + 2 * r + labelOff;
							memDC.TextOut(barX - sz.cx / 2, labelY, sig.reason);
							drawSignalArrow(barX, labelY, barY + 2 * r, COLOR_GREEN_DOWN);
						}
						memDC.SelectObject(pOldB);
						memDC.SelectObject(pOldP);
					}
					else
					{
						int barY = priceToY(klineData[klineIdx].high) - g_data.RDPI(2);
						CBrush brush(COLOR_RED_UP);
						CPen pen(PS_SOLID, 1, COLOR_RED_UP);
						CBrush* pOldB = memDC.SelectObject(&brush);
						CPen* pOldP = memDC.SelectObject(&pen);
						int r = g_data.RDPI(3);
						int labelOff = g_data.RDPI(8);
						memDC.Ellipse(barX - r, barY - 2 * r, barX + r, barY);
						if (!kline5FilteredBarIndices.count(klineIdx))
						{
							memDC.SetTextColor(COLOR_RED_UP);
							CSize sz = memDC.GetTextExtent(sig.reason);
							int labelY = barY - 2 * r - sz.cy - labelOff;
							memDC.TextOut(barX - sz.cx / 2, labelY, sig.reason);
							drawSignalArrow(barX, labelY + sz.cy, barY - 2 * r, COLOR_RED_UP);
						}
						memDC.SelectObject(pOldB);
						memDC.SelectObject(pOldP);
					}
				}
				memDC.SetBkMode(oldBkMode);
			}
		}
	}
}

void CTimelineChart::DrawDayKLinePriceChart(CDC& memDC, const TimelineDrawContext& ctx, const HoverState& hover)
{
	const auto& timelinePoint = *ctx.timelinePoint;
	if (timelinePoint.empty())
		return;

	const int totalPoints = static_cast<int>(timelinePoint.size());

	STOCK::Price maxPrice = ctx.maxPrice;
	STOCK::Price minPrice = ctx.minPrice;
	double unitY = ctx.unitY;
	if (maxPrice <= 0 || minPrice < 0 || maxPrice <= minPrice || unitY <= 0)
	{
		STOCK::Price priceLimit = ctx.realtimeData.priceLimit;
		maxPrice = ctx.realtimeData.prevClosePrice + priceLimit;
		minPrice = ctx.realtimeData.prevClosePrice - priceLimit;
		const int pricePaddingY = g_data.RDPI(10);
		double paddingPrice = (maxPrice - minPrice) * pricePaddingY / ctx.priceChartHeight;
		maxPrice += paddingPrice;
		minPrice -= paddingPrice;
		unitY = ctx.priceChartHeight / (maxPrice - minPrice);
	}

	const auto& klineData = *ctx.klineData;
	int klineStartIdx = ctx.startIndex;
	int klineEndIdx = klineStartIdx + totalPoints;
	if (klineEndIdx > static_cast<int>(klineData.size()))
		klineEndIdx = static_cast<int>(klineData.size());

	if (klineStartIdx >= static_cast<int>(klineData.size()))
		return;

	float barTotalWidth = static_cast<float>(ctx.chartWidth) / totalPoints;
	int barWidth = max(1, static_cast<int>(barTotalWidth * 0.7));
	int gap = static_cast<int>(barTotalWidth) - barWidth;
	if (gap < 1) gap = 1;

	auto priceToY = [&](STOCK::Price price) -> int {
		return ctx.priceChartTop + ctx.priceChartHeight - static_cast<int>((price - minPrice) * unitY);
		};

	STOCK::Price prevClose = ctx.realtimeData.prevClosePrice;

	for (int i = 0; i < totalPoints && (klineStartIdx + i) < klineEndIdx; i++)
	{
		const auto& kp = klineData[klineStartIdx + i];
		if (kp.close <= 0) continue;

		int centerX = static_cast<int>(ctx.chartWidth / static_cast<float>(totalPoints) * i) + static_cast<int>(barTotalWidth / 2);
		int leftX = centerX - barWidth / 2;

		bool isUp = (kp.close >= kp.open);
		COLORREF barColor = isUp ? COLOR_RED_UP : COLOR_GREEN_DOWN;

		int openY = priceToY(kp.open);
		int closeY = priceToY(kp.close);
		int highY = priceToY(kp.high);
		int lowY = priceToY(kp.low);

		openY = max(ctx.priceChartTop, min(openY, ctx.priceChartTop + ctx.priceChartHeight));
		closeY = max(ctx.priceChartTop, min(closeY, ctx.priceChartTop + ctx.priceChartHeight));
		highY = max(ctx.priceChartTop, min(highY, ctx.priceChartTop + ctx.priceChartHeight));
		lowY = max(ctx.priceChartTop, min(lowY, ctx.priceChartTop + ctx.priceChartHeight));

		CPen barPen(PS_SOLID, 1, barColor);
		memDC.SelectObject(&barPen);
		memDC.MoveTo(centerX, highY);
		memDC.LineTo(centerX, lowY);

		int bodyTop = min(openY, closeY);
		int bodyBottom = max(openY, closeY);
		int bodyHeight = bodyBottom - bodyTop;
		if (bodyHeight < 1) bodyHeight = 1;

		if (isUp)
		{
			CBrush* pOldBrush = memDC.SelectObject((CBrush*)CBrush::FromHandle((HBRUSH)GetStockObject(NULL_BRUSH)));
			memDC.Rectangle(leftX, bodyTop, leftX + barWidth, bodyBottom + 1);
			memDC.SelectObject(pOldBrush);
		}
		else
		{
			CBrush brush(barColor);
			CBrush* pOldBrush = memDC.SelectObject(&brush);
			memDC.Rectangle(leftX, bodyTop, leftX + barWidth, bodyBottom + 1);
			memDC.SelectObject(pOldBrush);
		}
	}

	if (hover.showMA)
	{
		const COLORREF ma5Color = RGB(0, 0, 230);
		const COLORREF ma10Color = RGB(0, 166, 235);
		const COLORREF ma20Color = RGB(169, 102, 186);

		auto drawMALine = [&](int fieldOffset, COLORREF color) {
			CPen maPen(PS_SOLID, 1, color);
			memDC.SelectObject(&maPen);
			bool first = true;
			for (int i = 0; i < totalPoints; i++)
			{
				const auto& item = timelinePoint[i];
				STOCK::Price maVal = 0;
				switch (fieldOffset)
				{
				case 5: maVal = item.ma5; break;
				case 10: maVal = item.ma10; break;
				case 20: maVal = item.ma20; break;
				}
				if (maVal <= 0) { first = true; continue; }
				int pointX = static_cast<int>(ctx.chartWidth / static_cast<float>(totalPoints) * i) + static_cast<int>(ctx.chartWidth / static_cast<float>(totalPoints) / 2);
				double yVal = (maVal - minPrice) * unitY;
				int py = ctx.priceChartTop + ctx.priceChartHeight - static_cast<int>(yVal);
				if (first)
				{
					memDC.MoveTo(pointX, py);
					first = false;
				}
				else
				{
					memDC.LineTo(pointX, py);
				}
			}
			};

		drawMALine(5, ma5Color);
		drawMALine(10, ma10Color);
		drawMALine(20, ma20Color);
	}

	if (hover.showBollBands)
	{
		const int N = 20;
		const int K = 2;

		const auto& fullData = ctx.fullTimeline ? *ctx.fullTimeline : timelinePoint;

		std::vector<double> upperBand(totalPoints, 0);
		std::vector<double> middleBand(totalPoints, 0);
		std::vector<double> lowerBand(totalPoints, 0);

		for (int i = 0; i < totalPoints; i++)
		{
			int globalIdx = ctx.startIndex + i;
			if (globalIdx < N - 1)
			{
				upperBand[i] = middleBand[i] = lowerBand[i] = 0;
				continue;
			}
			double sum = 0;
			for (int j = globalIdx - N + 1; j <= globalIdx; j++)
			{
				sum += fullData[j].price;
			}
			double ma = sum / N;
			double variance = 0;
			for (int j = globalIdx - N + 1; j <= globalIdx; j++)
			{
				double diff = fullData[j].price - ma;
				variance += diff * diff;
			}
			double stddev = std::sqrt(variance / N);
			middleBand[i] = ma;
			upperBand[i] = ma + K * stddev;
			lowerBand[i] = ma - K * stddev;
		}

		auto bandPriceToY = [&](double price) -> int {
			int py = ctx.priceChartTop + ctx.priceChartHeight - static_cast<int>(round((price - minPrice) * unitY));
			return max(ctx.priceChartTop, min(py, ctx.priceChartTop + ctx.priceChartHeight));
			};

		auto drawBandLine = [&](const std::vector<double>& band, COLORREF color) {
			CPen bandPen(PS_SOLID, 1, color);
			memDC.SelectObject(&bandPen);
			bool first = true;
			for (int i = 0; i < totalPoints; i++)
			{
				if (band[i] <= 0) continue;
				int pointX = static_cast<int>(ctx.chartWidth / static_cast<float>(totalPoints) * i) + static_cast<int>(ctx.chartWidth / static_cast<float>(totalPoints) / 2);
				int py = bandPriceToY(band[i]);
				if (first)
				{
					memDC.MoveTo(pointX, py);
					first = false;
				}
				else
				{
					memDC.LineTo(pointX, py);
				}
			}
			};

		drawBandLine(upperBand, COLOR_RED_UP);
		drawBandLine(middleBand, RGB(0, 0, 230));
		drawBandLine(lowerBand, COLOR_GREEN_DOWN);
	}

	// 最高/最低价标签
	{
		STOCK::Price hiPrice = 0, loPrice = (std::numeric_limits<STOCK::Price>::max)();
		int hiIdx = -1, loIdx = -1;
		for (int i = 0; i < totalPoints && (klineStartIdx + i) < klineEndIdx; i++)
		{
			const auto& kp = klineData[klineStartIdx + i];
			if (kp.high > 0)
			{
				if (kp.high > hiPrice) { hiPrice = kp.high; hiIdx = i; }
			}
			if (kp.low > 0)
			{
				if (kp.low < loPrice) { loPrice = kp.low; loIdx = i; }
			}
		}

		if (hiIdx >= 0 && hiPrice > 0)
		{
			int hiX = static_cast<int>(ctx.chartWidth / static_cast<float>(totalPoints) * hiIdx) + static_cast<int>(barTotalWidth / 2);
			int hiY = priceToY(hiPrice);
			DrawPricePointLabel(memDC, hiX, hiY, 0, ctx.priceChartTop, ctx.chartWidth, ctx.priceChartHeight,
				hiPrice, true, COLOR_RED_UP);
		}

		if (loIdx >= 0 && loPrice > 0 && loIdx != hiIdx)
		{
			int loX = static_cast<int>(ctx.chartWidth / static_cast<float>(totalPoints) * loIdx) + static_cast<int>(barTotalWidth / 2);
			int loY = priceToY(loPrice);
			DrawPricePointLabel(memDC, loX, loY, 0, ctx.priceChartTop, ctx.chartWidth, ctx.priceChartHeight,
				loPrice, false, COLOR_GREEN_DOWN);
		}
	}
}

void CTimelineChart::DrawTimelineVolumeSection(CDC& memDC, const TimelineDrawContext& ctx, const HoverState& hover)
{
	CIndicatorChart indicatorChart;
	indicatorChart.DrawVolumeChart(memDC, 0, ctx.volumeChartTop, ctx.chartWidth, ctx.volumeChartHeight, *ctx.timelinePoint, &ctx.realtimeData, 0, -1, ctx.xAxisPoints, hover.isHoveringVolume, hover.hoveredBarIndex);

	if (hover.isMin5KLineMode && ctx.fullTimeline && !ctx.fullTimeline->empty() && ctx.timelinePoint && !ctx.timelinePoint->empty())
	{
		const auto& fullData = *ctx.fullTimeline;
		const auto& visibleData = *ctx.timelinePoint;
		const int totalPoints = static_cast<int>(visibleData.size());
		const int fullCount = static_cast<int>(fullData.size());
		const int startIndex = ctx.startIndex;
		int endIndex = min(fullCount, startIndex + totalPoints);

		STOCK::Volume maxVolume = 0;
		for (int i = startIndex; i < endIndex; i++)
		{
			maxVolume = max(maxVolume, fullData[i].volume);
		}

		auto calcVolumeMA = [&](int globalIndex, int period) -> double {
			if (globalIndex < period - 1)
				return 0.0;

			double sum = 0.0;
			for (int i = globalIndex - period + 1; i <= globalIndex; i++)
				sum += static_cast<double>(fullData[i].volume);
			return sum / period;
			};

		std::vector<double> ma5(totalPoints, 0.0);
		std::vector<double> ma10(totalPoints, 0.0);
		for (int i = 0; i < totalPoints; i++)
		{
			int globalIndex = startIndex + i;
			if (globalIndex >= 0 && globalIndex < fullCount)
			{
				ma5[i] = calcVolumeMA(globalIndex, 5);
				ma10[i] = calcVolumeMA(globalIndex, 10);
			}
		}

		if (maxVolume > 0)
		{
			auto volumeToY = [&](double volume) -> int {
				int py = ctx.volumeChartTop + ctx.volumeChartHeight - static_cast<int>(volume / static_cast<double>(maxVolume) * ctx.volumeChartHeight);
				return max(ctx.volumeChartTop, min(py, ctx.volumeChartTop + ctx.volumeChartHeight));
				};

			auto drawVolumeMALine = [&](const std::vector<double>& values, COLORREF color) {
				CPen pen(PS_SOLID, 1, color);
				CPen* pOldPen = memDC.SelectObject(&pen);
				bool first = true;
				for (int i = 0; i < totalPoints; i++)
				{
					if (values[i] <= 0)
					{
						first = true;
						continue;
					}

					int pointX = static_cast<int>(ctx.chartWidth / static_cast<float>(totalPoints) * i) + static_cast<int>(ctx.chartWidth / static_cast<float>(totalPoints) / 2);
					int pointY = volumeToY(values[i]);
					if (first)
					{
						memDC.MoveTo(pointX, pointY);
						first = false;
					}
					else
					{
						memDC.LineTo(pointX, pointY);
					}
				}
				memDC.SelectObject(pOldPen);
				};

			drawVolumeMALine(ma5, RGB(0, 0, 230));
			drawVolumeMALine(ma10, RGB(0, 166, 235));
		}
	}

	// 时间竖线和平均值参考线
	CPen pGrid(PS_SOLID, 1, COLOR_GRAY_GRID);
	CPen* pOldVolPen = memDC.SelectObject(&pGrid);

	int volumeY = ctx.volumeChartTop;
	memDC.MoveTo(0, volumeY);
	memDC.LineTo(ctx.chartWidth, volumeY);

	const auto& timelinePoint = *ctx.timelinePoint;
	if (!timelinePoint.empty())
	{
		STOCK::Volume maxVolume = 0;
		for (const auto& item : timelinePoint)
		{
			if (item.volume > maxVolume)
				maxVolume = item.volume;
		}
		if (maxVolume > 0)
		{
			CPen dotPen(PS_DOT, 1, COLOR_GRAY_MIDDLE);
			memDC.SelectObject(&dotPen);
			memDC.SetTextColor(COLOR_GRAY_TEXT);
			int volumeY = ctx.volumeChartTop;
			int yAxisWidth = g_data.RDPI(50);
			for (int i = 1; i <= 2; i++)
			{
				int yPos = volumeY + ctx.volumeChartHeight * i / 3;
				memDC.MoveTo(0, yPos);
				memDC.LineTo(ctx.chartWidth, yPos);

				STOCK::Volume volAtLine = maxVolume * (3 - i) / 3;
				STOCK::Volume volInLots = volAtLine / 100;
				CString volLabel = CCommon::FormatVolumeInt(volInLots);
				CSize labelSize = memDC.GetTextExtent(volLabel);
				memDC.TextOut(-labelSize.cx - g_data.RDPI(3), yPos - labelSize.cy / 2, volLabel);
			}
			memDC.SelectObject(&pGrid);
		}
	}

	if (ctx.timelinePoint && !ctx.timelinePoint->empty())
	{
		const int totalPts = static_cast<int>(ctx.timelinePoint->size());
		const int numVLines = 4;
		for (int i = 0; i <= numVLines; i++)
		{
			int xPos = ctx.chartWidth * i / numVLines;
			memDC.MoveTo(xPos, volumeY);
			memDC.LineTo(xPos, volumeY + ctx.volumeChartHeight);
		}
	}
	memDC.SelectObject(pOldVolPen);
}

void CTimelineChart::DrawPriceChartArea(CDC& memDC, const TimelineDrawContext& ctx, int areaTop, int areaHeight, const HoverState& hover)
{
	const auto& timelinePoint = *ctx.timelinePoint;
	int titleH = g_data.RDPI(16);
	int oldBkMode = memDC.SetBkMode(TRANSPARENT);

	CRect priceTitleRect(0, areaTop, ctx.chartWidth, areaTop + titleH);
	memDC.FillSolidRect(priceTitleRect, RGB(245, 245, 245));

	if (!hover.timelinePriceTitleTip.IsEmpty())
	{
		memDC.SetTextColor(COLOR_BLACK);
		memDC.DrawText(hover.timelinePriceTitleTip, priceTitleRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
	}
	else if (hover.isKLineMode && !hover.isMin5KLineMode && !hover.isMin30KLineMode && ctx.klineData && !ctx.klineData->empty())
	{
		int xPos = g_data.RDPI(4);
		int centerY = areaTop + titleH / 2;

		const auto& klineData = *ctx.klineData;
		int klineIdx = -1;
		if (hover.hoveredBarIndex >= 0 && hover.isHoveringVolume)
		{
			klineIdx = ctx.startIndex + hover.hoveredBarIndex;
		}
		else
		{
			klineIdx = ctx.startIndex + static_cast<int>(timelinePoint.size()) - 1;
		}
		if (klineIdx < 0 || klineIdx >= static_cast<int>(klineData.size()))
			klineIdx = static_cast<int>(klineData.size()) - 1;

		if (klineIdx >= 0 && klineIdx < static_cast<int>(klineData.size()))
		{
			const auto& kp = klineData[klineIdx];
			STOCK::Price prevClose = ctx.realtimeData.prevClosePrice;

			auto drawKLineLabel = [&](const CString& label, STOCK::Price value, COLORREF labelColor, COLORREF valueColor) {
				CString valStr = CCommon::FormatFloat(value);
				memDC.SetTextColor(labelColor);
				CSize ls = memDC.GetTextExtent(label);
				memDC.TextOut(xPos, centerY - ls.cy / 2, label);
				xPos += ls.cx;
				memDC.SetTextColor(valueColor);
				CSize vs = memDC.GetTextExtent(valStr);
				memDC.TextOut(xPos, centerY - vs.cy / 2, valStr);
				xPos += vs.cx + g_data.RDPI(4);
				};

			drawKLineLabel(_T("开:"), kp.open, COLOR_BLACK, (kp.open >= prevClose ? COLOR_RED_UP : COLOR_GREEN_DOWN));
			drawKLineLabel(_T("收:"), kp.close, COLOR_BLACK, (kp.close >= prevClose ? COLOR_RED_UP : COLOR_GREEN_DOWN));
			drawKLineLabel(_T("高:"), kp.high, COLOR_BLACK, (kp.high >= prevClose ? COLOR_RED_UP : COLOR_GREEN_DOWN));
			drawKLineLabel(_T("低:"), kp.low, COLOR_BLACK, (kp.low >= prevClose ? COLOR_RED_UP : COLOR_GREEN_DOWN));
		}
	}
	else if ((hover.isMin5KLineMode || hover.isMin30KLineMode) && ctx.klineData && !ctx.klineData->empty())
	{
		int xPos = g_data.RDPI(4);
		int centerY = areaTop + titleH / 2;

		const auto& klineData = *ctx.klineData;
		int klineIdx = -1;
		bool isHovering = (hover.hoveredBarIndex >= 0 && hover.isHoveringVolume);
		if (isHovering)
		{
			klineIdx = ctx.startIndex + hover.hoveredBarIndex;
		}
		else
		{
			klineIdx = ctx.startIndex + static_cast<int>(timelinePoint.size()) - 1;
		}
		if (klineIdx < 0 || klineIdx >= static_cast<int>(klineData.size()))
			klineIdx = static_cast<int>(klineData.size()) - 1;

		if (klineIdx >= 0 && klineIdx < static_cast<int>(klineData.size()))
		{
			const auto& kp = klineData[klineIdx];
			STOCK::Price prevClose = ctx.realtimeData.prevClosePrice;

			auto drawKLineLabel = [&](const CString& label, STOCK::Price value, COLORREF labelColor, COLORREF valueColor) {
				CString valStr = CCommon::FormatFloat(value);
				memDC.SetTextColor(labelColor);
				CSize ls = memDC.GetTextExtent(label);
				memDC.TextOut(xPos, centerY - ls.cy / 2, label);
				xPos += ls.cx;
				memDC.SetTextColor(valueColor);
				CSize vs = memDC.GetTextExtent(valStr);
				memDC.TextOut(xPos, centerY - vs.cy / 2, valStr);
				xPos += vs.cx + g_data.RDPI(4);
				};

			drawKLineLabel(_T("开:"), kp.open, COLOR_BLACK, (kp.open >= prevClose ? COLOR_RED_UP : COLOR_GREEN_DOWN));
			drawKLineLabel(_T("收:"), kp.close, COLOR_BLACK, (kp.close >= prevClose ? COLOR_RED_UP : COLOR_GREEN_DOWN));
			drawKLineLabel(_T("高:"), kp.high, COLOR_BLACK, (kp.high >= prevClose ? COLOR_RED_UP : COLOR_GREEN_DOWN));
			drawKLineLabel(_T("低:"), kp.low, COLOR_BLACK, (kp.low >= prevClose ? COLOR_RED_UP : COLOR_GREEN_DOWN));
		}
	}
	else if (!timelinePoint.empty())
	{
		STOCK::Price prevClose = ctx.realtimeData.prevClosePrice;

		bool isHovering = (hover.hoveredBarIndex >= 0 && hover.isHoveringVolume);
		STOCK::Price dispAvgPrice = isHovering ? hover.hoveredData.averagePrice : timelinePoint.back().averagePrice;
		if (dispAvgPrice <= 0)
			dispAvgPrice = isHovering ? hover.hoverMa1 : ctx.ma1;

		int xPos = g_data.RDPI(4);
		int centerY = areaTop + titleH / 2;

		auto drawLabelValue = [&](const CString& labelText, STOCK::Price value, COLORREF labelColor, COLORREF valueColor) {
			CString valStr = CCommon::FormatFloat(value);
			memDC.SetTextColor(labelColor);
			CSize ls = memDC.GetTextExtent(labelText);
			memDC.TextOut(xPos, centerY - ls.cy / 2, labelText);
			xPos += ls.cx;
			memDC.SetTextColor(valueColor);
			CSize vs = memDC.GetTextExtent(valStr);
			memDC.TextOut(xPos, centerY - vs.cy / 2, valStr);
			xPos += vs.cx + g_data.RDPI(4);
			};

		auto cmpPrevClose = [prevClose](STOCK::Price p) -> COLORREF {
			if (prevClose <= 0) return COLOR_BLACK;
			if (p > prevClose) return COLOR_RED_UP;
			if (p < prevClose) return COLOR_GREEN_DOWN;
			return COLOR_BLACK;
			};
		drawLabelValue(_T("现:"), ctx.realtimeData.currentPrice, COLOR_BLACK, cmpPrevClose(ctx.realtimeData.currentPrice));

		if (ctx.realtimeData.IsETF() && ctx.realtimeData.iopv > 0)
		{
			COLORREF iopvColor = COLOR_BLACK;
			if (ctx.realtimeData.iopv > ctx.realtimeData.currentPrice)
				iopvColor = COLOR_RED_UP;
			else if (ctx.realtimeData.iopv < ctx.realtimeData.currentPrice)
				iopvColor = COLOR_GREEN_DOWN;

			CString iopvLabel = _T("IOPV:");
			CString iopvVal;
			iopvVal.Format(_T("%.4f"), ctx.realtimeData.iopv);
			memDC.SetTextColor(COLOR_BLACK);
			CSize iopvLs = memDC.GetTextExtent(iopvLabel);
			memDC.TextOut(xPos, centerY - iopvLs.cy / 2, iopvLabel);
			xPos += iopvLs.cx;
			memDC.SetTextColor(iopvColor);
			CSize iopvVs = memDC.GetTextExtent(iopvVal);
			memDC.TextOut(xPos, centerY - iopvVs.cy / 2, iopvVal);
			xPos += iopvVs.cx + g_data.RDPI(4);

			CString premLabel = _T("溢:");
			CString premVal;
			double premRate = ctx.realtimeData.iopvPremiumRate;
			if (premRate >= 0)
				premVal.Format(_T("+%.2f%%"), premRate);
			else
				premVal.Format(_T("%.2f%%"), premRate);
			COLORREF premColor = premRate > 0 ? COLOR_RED_UP : (premRate < 0 ? COLOR_GREEN_DOWN : COLOR_BLACK);
			memDC.SetTextColor(COLOR_BLACK);
			CSize premLs = memDC.GetTextExtent(premLabel);
			memDC.TextOut(xPos, centerY - premLs.cy / 2, premLabel);
			xPos += premLs.cx;
			memDC.SetTextColor(premColor);
			CSize premVs = memDC.GetTextExtent(premVal);
			memDC.TextOut(xPos, centerY - premVs.cy / 2, premVal);
			xPos += premVs.cx + g_data.RDPI(4);

			drawLabelValue(_T("均:"), dispAvgPrice, COLOR_BLACK, cmpPrevClose(dispAvgPrice));
		}
		else
		{
			drawLabelValue(_T("均:"), dispAvgPrice, COLOR_BLACK, cmpPrevClose(dispAvgPrice));
		}

		// 分时模式：在标题栏正中间显示实时指标信号指示器
		{
			const auto& fullData = ctx.fullTimeline ? *ctx.fullTimeline : timelinePoint;
			if (fullData.size() >= 26)
			{
				int signalEndIndex = -1;
				if (isHovering)
				{
					int hoverGlobalIdx = ctx.startIndex + hover.hoveredBarIndex;
					if (hoverGlobalIdx >= 25 && hoverGlobalIdx < static_cast<int>(fullData.size()))
						signalEndIndex = hoverGlobalIdx;
				}

				auto rtSig = CSignalAnalyzer::CalcRealtimeSignalsFromTimeline(fullData, signalEndIndex);

				static const COLORREF BUY_COLORS[] = {
					RGB(40, 240, 40),
					RGB(50, 180, 50),
					RGB(20, 130, 40)
				};
				static const COLORREF SELL_COLORS[] = {
					RGB(240, 40, 40),
					RGB(180, 50, 50),
					RGB(130, 20, 40)
				};

				std::vector<std::pair<CString, COLORREF>> sigItems;
				if (rtSig.macd != 0) sigItems.push_back({ rtSig.macd == -1 ? _T("M\u2193") : _T("M\u2191"), rtSig.macd == -1 ? BUY_COLORS[rtSig.macdStr - 1] : SELL_COLORS[rtSig.macdStr - 1] });
				if (rtSig.boll != 0) sigItems.push_back({ rtSig.boll == -1 ? _T("B\u2193") : _T("B\u2191"), rtSig.boll == -1 ? BUY_COLORS[rtSig.bollStr - 1] : SELL_COLORS[rtSig.bollStr - 1] });
				if (rtSig.kdj != 0) sigItems.push_back({ rtSig.kdj == -1 ? _T("K\u2193") : _T("K\u2191"), rtSig.kdj == -1 ? BUY_COLORS[rtSig.kdjStr - 1] : SELL_COLORS[rtSig.kdjStr - 1] });
				if (rtSig.rsi != 0) sigItems.push_back({ rtSig.rsi == -1 ? _T("R\u2193") : _T("R\u2191"), rtSig.rsi == -1 ? BUY_COLORS[rtSig.rsiStr - 1] : SELL_COLORS[rtSig.rsiStr - 1] });
				if (rtSig.wr != 0) sigItems.push_back({ rtSig.wr == -1 ? _T("W\u2193") : _T("W\u2191"), rtSig.wr == -1 ? BUY_COLORS[rtSig.wrStr - 1] : SELL_COLORS[rtSig.wrStr - 1] });

				if (!sigItems.empty())
				{
					CString riskText;
					COLORREF riskColor = COLOR_BLACK;
					{
						auto stockData = g_data.GetStockData(hover.stockId);
						if (stockData)
						{
							auto min5KLineObj = stockData->getMin5KLineData();
							auto min30KLineObj = stockData->getMin30KLineData();
							if (min5KLineObj && min5KLineObj->data.size() >= 60 && min30KLineObj && min30KLineObj->data.size() >= 2)
							{
								std::vector<STOCK::Bar> bars5;
								bars5.reserve(min5KLineObj->data.size());
								for (const auto& kp : min5KLineObj->data) bars5.push_back(STOCK::Bar::FromKLinePoint(kp));
								std::vector<STOCK::Bar> bars30;
								bars30.reserve(min30KLineObj->data.size());
								for (const auto& kp : min30KLineObj->data) bars30.push_back(STOCK::Bar::FromKLinePoint(kp));
								STOCK::TrendState30m trendState = CSignalAnalyzer::Get30mTrendState(bars30).state;
								CString reason = CSignalAnalyzer::CalcForbidResult(bars5, trendState).forbidBuyReason;
								if (reason.IsEmpty())
								{
									riskText = _T("\u221A");
									bool hasBuy = (rtSig.boll == -1 || rtSig.macd == -1 || rtSig.rsi == -1 || rtSig.kdj == -1 || rtSig.wr == -1);
									riskColor = hasBuy ? BUY_COLORS[0] : SELL_COLORS[0];
								}
								else
								{
									riskText = _T("\u00D7") + reason;
									riskColor = RGB(180, 80, 0);
								}
							}
						}
					}

					int totalW = 0;
					for (const auto& item : sigItems)
						totalW += memDC.GetTextExtent(item.first).cx;
					if (!riskText.IsEmpty())
						totalW += memDC.GetTextExtent(riskText).cx;
					int sigX = ctx.chartWidth - totalW - g_data.RDPI(4);

					for (const auto& item : sigItems)
					{
						memDC.SetTextColor(item.second);
						CSize sz = memDC.GetTextExtent(item.first);
						memDC.TextOut(sigX, centerY - sz.cy / 2, item.first);
						sigX += sz.cx;
					}
					if (!riskText.IsEmpty())
					{
						memDC.SetTextColor(riskColor);
						CSize sz = memDC.GetTextExtent(riskText);
						memDC.TextOut(sigX, centerY - sz.cy / 2, riskText);
					}
				}
			}
		}
	}

	if (hover.isKLineMode && !timelinePoint.empty())
	{
		bool isHovering = (hover.hoveredBarIndex >= 0 && hover.isHoveringVolume);
		int displayIdx = isHovering ? hover.hoveredBarIndex : static_cast<int>(timelinePoint.size()) - 1;
		displayIdx = max(0, min(displayIdx, static_cast<int>(timelinePoint.size()) - 1));

		const int rightPadding = g_data.RDPI(4);
		const int itemGap = g_data.RDPI(6);
		int centerY = areaTop + titleH / 2;

		auto formatPrice = [](STOCK::Price value) -> CString {
			return CCommon::FormatFloat(value);
			};
		auto drawRightLabelValues = [&](const std::vector<std::pair<CString, COLORREF>>& items) {
			if (items.empty())
				return;

			int totalWidth = 0;
			for (const auto& item : items)
				totalWidth += memDC.GetTextExtent(item.first).cx + itemGap;
			totalWidth -= itemGap;

			int xPos = ctx.chartWidth - rightPadding - totalWidth;
			xPos = max(g_data.RDPI(4), xPos);
			for (const auto& item : items)
			{
				memDC.SetTextColor(item.second);
				CSize sz = memDC.GetTextExtent(item.first);
				memDC.TextOut(xPos, centerY - sz.cy / 2, item.first);
				xPos += sz.cx + itemGap;
			}
			};

		if (hover.isMin5KLineMode)
		{
			auto stockData = g_data.GetStockData(hover.stockId);
			if (stockData)
			{
				auto min5KLineObj = stockData->getMin5KLineData();
				if (min5KLineObj && min5KLineObj->data.size() >= 26)
				{
					std::vector<STOCK::Bar> bars5;
					bars5.reserve(min5KLineObj->data.size());
					for (const auto& kp : min5KLineObj->data) bars5.push_back(STOCK::Bar::FromKLinePoint(kp));

					int signalEndIndex = -1;
					if (isHovering)
					{
						int hoverKlineIdx = ctx.startIndex + hover.hoveredBarIndex;
						if (hoverKlineIdx >= 25 && hoverKlineIdx < static_cast<int>(bars5.size()))
							signalEndIndex = hoverKlineIdx;
					}

					auto rtSig = CSignalAnalyzer::CalcRealtimeSignals(bars5, signalEndIndex);

					static const COLORREF BUY_COLORS[] = {
						RGB(40, 240, 40),
						RGB(50, 180, 50),
						RGB(20, 130, 40)
					};
					static const COLORREF SELL_COLORS[] = {
						RGB(240, 40, 40),
						RGB(180, 50, 50),
						RGB(130, 20, 40)
					};

					std::vector<std::pair<CString, COLORREF>> sigItems;
					if (rtSig.macd != 0) sigItems.push_back({ rtSig.macd == -1 ? _T("M\u2193") : _T("M\u2191"), rtSig.macd == -1 ? BUY_COLORS[rtSig.macdStr - 1] : SELL_COLORS[rtSig.macdStr - 1] });
					if (rtSig.boll != 0) sigItems.push_back({ rtSig.boll == -1 ? _T("B\u2193") : _T("B\u2191"), rtSig.boll == -1 ? BUY_COLORS[rtSig.bollStr - 1] : SELL_COLORS[rtSig.bollStr - 1] });
					if (rtSig.kdj != 0) sigItems.push_back({ rtSig.kdj == -1 ? _T("K\u2193") : _T("K\u2191"), rtSig.kdj == -1 ? BUY_COLORS[rtSig.kdjStr - 1] : SELL_COLORS[rtSig.kdjStr - 1] });
					if (rtSig.rsi != 0) sigItems.push_back({ rtSig.rsi == -1 ? _T("R\u2193") : _T("R\u2191"), rtSig.rsi == -1 ? BUY_COLORS[rtSig.rsiStr - 1] : SELL_COLORS[rtSig.rsiStr - 1] });
					if (rtSig.wr != 0) sigItems.push_back({ rtSig.wr == -1 ? _T("W\u2193") : _T("W\u2191"), rtSig.wr == -1 ? BUY_COLORS[rtSig.wrStr - 1] : SELL_COLORS[rtSig.wrStr - 1] });

					if (!sigItems.empty())
					{
						CString riskText;
						COLORREF riskColor = COLOR_BLACK;
						{
							auto min30KLineObj = stockData->getMin30KLineData();
							if (min30KLineObj && min30KLineObj->data.size() >= 2 && bars5.size() >= 60)
							{
								std::vector<STOCK::Bar> bars30;
								bars30.reserve(min30KLineObj->data.size());
								for (const auto& kp : min30KLineObj->data) bars30.push_back(STOCK::Bar::FromKLinePoint(kp));
								STOCK::TrendState30m trendState = CSignalAnalyzer::Get30mTrendState(bars30).state;
								CString reason = CSignalAnalyzer::CalcForbidResult(bars5, trendState).forbidBuyReason;
								if (reason.IsEmpty())
								{
									riskText = _T("\u221A");
									bool hasBuy = (rtSig.boll == -1 || rtSig.macd == -1 || rtSig.rsi == -1 || rtSig.kdj == -1 || rtSig.wr == -1);
									riskColor = hasBuy ? BUY_COLORS[0] : SELL_COLORS[0];
								}
								else
								{
									riskText = _T("\u00D7") + reason;
									riskColor = RGB(180, 80, 0);
								}
							}
						}

						int totalW = 0;
						for (const auto& item : sigItems)
							totalW += memDC.GetTextExtent(item.first).cx;
						if (!riskText.IsEmpty())
							totalW += memDC.GetTextExtent(riskText).cx;
						int xPos = ctx.chartWidth - totalW - g_data.RDPI(4);

						for (const auto& item : sigItems)
						{
							memDC.SetTextColor(item.second);
							CSize sz = memDC.GetTextExtent(item.first);
							memDC.TextOut(xPos, centerY - sz.cy / 2, item.first);
							xPos += sz.cx;
						}
						if (!riskText.IsEmpty())
						{
							memDC.SetTextColor(riskColor);
							CSize sz = memDC.GetTextExtent(riskText);
							memDC.TextOut(xPos, centerY - sz.cy / 2, riskText);
						}
					}
				}
			}
		}
		else
		{
			if (hover.showMA)
			{
				STOCK::Price dispMa5 = isHovering ? hover.hoverMa5 : ctx.ma5;
				STOCK::Price dispMa10 = isHovering ? hover.hoverMa10 : ctx.ma10;
				STOCK::Price dispMa20 = isHovering ? hover.hoverMa20 : ctx.ma20;
				std::vector<std::pair<CString, COLORREF>> items;
				if (dispMa5 > 0) items.push_back({ _T("MA5:") + formatPrice(dispMa5), RGB(0, 0, 230) });
				if (dispMa10 > 0) items.push_back({ _T("MA10:") + formatPrice(dispMa10), RGB(0, 166, 235) });
				if (dispMa20 > 0) items.push_back({ _T("MA20:") + formatPrice(dispMa20), RGB(169, 102, 186) });
				drawRightLabelValues(items);
			}
			else if (hover.showBollBands)
			{
				const int N = 20;
				const int K = 2;
				const auto& fullData = ctx.fullTimeline ? *ctx.fullTimeline : timelinePoint;
				int globalIdx = ctx.startIndex + displayIdx;
				if (globalIdx >= N - 1 && globalIdx < static_cast<int>(fullData.size()))
				{
					double sum = 0;
					for (int i = globalIdx - N + 1; i <= globalIdx; i++)
						sum += fullData[i].price;
					double mid = sum / N;

					double variance = 0;
					for (int i = globalIdx - N + 1; i <= globalIdx; i++)
					{
						double diff = fullData[i].price - mid;
						variance += diff * diff;
					}
					double stddev = std::sqrt(variance / N);
					double upper = mid + K * stddev;
					double lower = mid - K * stddev;

					std::vector<std::pair<CString, COLORREF>> items;
					items.push_back({ _T("上:") + formatPrice(static_cast<STOCK::Price>(upper)), COLOR_RED_UP });
					items.push_back({ _T("中:") + formatPrice(static_cast<STOCK::Price>(mid)), RGB(0, 0, 230) });
					items.push_back({ _T("下:") + formatPrice(static_cast<STOCK::Price>(lower)), COLOR_GREEN_DOWN });
					drawRightLabelValues(items);
				}
			}
		}
	}

	memDC.SetBkMode(oldBkMode);

	TimelineDrawContext tmpCtx = ctx;
	tmpCtx.priceChartTop = areaTop + titleH;
	tmpCtx.priceChartHeight = areaHeight - titleH;

	if (hover.isKLineMode && !hover.isMin5KLineMode && !hover.isMin30KLineMode)
	{
		if (hover.showTrendView)
			DrawTimelinePriceCurve(memDC, tmpCtx, hover);
		else
			DrawDayKLinePriceChart(memDC, tmpCtx, hover);
	}
	else if (hover.isMin5KLineMode)
		DrawMin5KLinePriceChart(memDC, tmpCtx, hover);
	else if (hover.isMin30KLineMode)
		DrawMin5KLinePriceChart(memDC, tmpCtx, hover);
	else
		DrawTimelinePriceCurve(memDC, tmpCtx, hover);
}