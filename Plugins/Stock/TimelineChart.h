#pragma once

#include "ChartContext.h"
#include "Common.h"
#include <StockDef.h>

// 走势图绘制模块
// 职责：绘制分时走势图、5分钟/30分钟/日K价格图、悬停覆盖层、价格区域标题栏等
class CTimelineChart
{
public:
	// 走势图悬停/交互状态
	struct HoverState {
		UIViewMode viewMode{ UI_VIEW_TIMELINE };
		bool isHoveringVolume{ false };
		int hoveredBarIndex{ -1 };
		STOCK::TimelinePoint hoveredData;
		STOCK::Price hoverMa1{ 0 }, hoverMa5{ 0 }, hoverMa10{ 0 }, hoverMa20{ 0 };
		STOCK::Price hoverPrevMa1{ 0 }, hoverPrevMa5{ 0 }, hoverPrevMa10{ 0 }, hoverPrevMa20{ 0 };
		CString hoverTip;
		CString timelinePriceTitleTip;
		CString timelineVolumeTitleTip;
		CString timelineMacdTitleTip;
		CString timelineKdjTitleTip;
		CString timelineWrTitleTip;
		CString timelineRsiTitleTip;
		bool showJZCurve{ false };
		bool showMA{ false };
		bool showBollBands{ true };
		bool showTrendView{ false };
		bool showChipPeak{ false };
		bool expandedMode{ false };
		int klinePeriodDays{ 250 };
		int scrollOffset{ 0 };
		int timelineScrollOffset{ -1 };
		int timelineVisibleCount{ 40 };
		int timelineLastTotalPoints{ 0 };
		std::wstring stockId;
		CPoint mousePos;
	};

	// 坐标转换：分时数据点→屏幕坐标
	static CPoint Stock2Point(int x, int y, int w, int h, double unitY, const STOCK::TimelinePoint& item, const STOCK::Price prevClosePrice);

	// 分时图绘制
	void DrawTimelineHeader(CDC& memDC, const TimelineDrawContext& ctx, const HoverState& hover);
	void DrawTimelineMAIndicators(CDC& memDC, const TimelineDrawContext& ctx);
	void DrawTimelineBackgroundHighlights(CDC& memDC, const TimelineDrawContext& ctx);
	void DrawTimelineGridLines(CDC& memDC, const TimelineDrawContext& ctx);
	void DrawTimelinePriceLabels(CDC& memDC, const TimelineDrawContext& ctx);
	void DrawTimelineCostAndProfitLines(CDC& memDC, const TimelineDrawContext& ctx, const HoverState& hover);
	void DrawTimelineGridAndLines(CDC& memDC, const TimelineDrawContext& ctx, const HoverState& hover);
	void DrawTimelinePriceCurve(CDC& memDC, const TimelineDrawContext& ctx, const HoverState& hover);
	void DrawTimelineHoverOverlay(CDC& memDC, const TimelineDrawContext& ctx, const HoverState& hover);

	// K线模式价格图
	void DrawMin5KLinePriceChart(CDC& memDC, const TimelineDrawContext& ctx, const HoverState& hover);
	void DrawDayKLinePriceChart(CDC& memDC, const TimelineDrawContext& ctx, const HoverState& hover);

	// 分时量柱区域
	void DrawTimelineVolumeSection(CDC& memDC, const TimelineDrawContext& ctx, const HoverState& hover);

	// 价格区域（标题栏+走势图一体化）
	void DrawPriceChartArea(CDC& memDC, const TimelineDrawContext& ctx, int areaTop, int areaHeight, const HoverState& hover);
};
