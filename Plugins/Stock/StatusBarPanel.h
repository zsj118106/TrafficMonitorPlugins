#pragma once

#include "ChartContext.h"
#include <StockDef.h>

// 状态栏/标题栏/持仓信息面板绘制
// 职责：绘制主标题栏、分时持仓信息栏、K线持仓信息栏、K线信息面板
class CStatusBarPanel
{
public:
    // 绘制主标题栏（股票名称+现价+涨跌幅+MACD信号）
    void DrawHeader(CDC& memDC, const STOCK::StockInfo& realtimeData, int windowWidth, int headerHeight, const CString& macdTrendSignal = CString());

    // 绘制分时模式持仓信息栏
    void DrawTimelinePositionInfo(CDC& memDC, const TimelineDrawContext& ctx, const std::wstring& stockId);

    // 绘制K线模式持仓信息栏
    void DrawKLinePositionInfo(CDC& memDC, int x, int y, int chartWidth, const STOCK::StockInfo& realtimeData, const std::wstring& stockId);

    // 绘制K线信息面板（买入日期、持有天数、盈亏、年化收益、周期高低价等）
    void DrawKLineInfoPanel(CDC& memDC, int left, int right, int bottomY, const STOCK::StockInfo& stockInfo, const std::vector<STOCK::KLinePoint>& klineData, const std::wstring& stockId);

    // 绘制底部两行状态栏（上行：当前股票关联股票，下行：系统配置的状态栏股票）
    void DrawBottomStatusBar(CDC& memDC, int w, int h, int indexBarHeight, const std::wstring& stockId, int viewMode);

private:
    // 绘制上行关联股票状态栏
    void DrawRelatedStockBar(CDC& memDC, int w, int topBarY, int singleBarHeight, const std::wstring& stockId, int viewMode);

    // 绘制下行系统状态栏
    void DrawSystemStatusBar(CDC& memDC, int w, int bottomBarY, int singleBarHeight);
};