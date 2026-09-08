#pragma once

#include <StockDef.h>
#include "Common.h"
#include <string>
#include <vector>

// 盘口面板绘制
// 职责：在指定矩形区域内绘制五档买卖盘、委比、趋势判定、净比(1/5/10/20/99)、
//       振幅、换手率等盘口信息
// 数据来源：stockInfo（实时行情，其code字段作为数据缓存key）、klineData（K线数据，用于振幅计算）
// 说明：DrawTrend 依赖当前视图模式（分时/5分钟/30分钟/日K）来判定趋势方向
class COrderBookPanel
{
public:
	// 绘制盘口面板
	// left, right: 面板左右边界（含主标题栏下方的盘口标题栏区域）
	// height: 面板总高度（含盘口标题栏）
	// viewMode: 当前视图模式（用于趋势判定）
	void Draw(CDC& memDC, int left, int right, int height, const STOCK::StockInfo& stockInfo,
		const std::vector<STOCK::KLinePoint>& klineData,
		UIViewMode viewMode);

	// 绘制成交明细界面（MX模式）：显示最近20条成交，每隔2秒从数据库刷新
	// left,right: 面板左右边界；height: 面板总高度（含盘口标题栏）
	void DrawTickDetail(CDC& memDC, int left, int right, int height, const STOCK::StockInfo& stockInfo);

private:
	// 盘口行数据
	struct OrderBookRow
	{
		STOCK::Price IOPV{ 0.0 };  // 净值
		CString strPrice;	//价格
		CString strVolume;	//挂单量
		COLORREF volumeColor{ RGB(0,0,0) };  // 挂单量颜色
		CString diffVol;  // 右对齐的瞬时变化量（+N/-N）
		COLORREF diffVolColor{ RGB(0,0,0) };  // 右对齐后缀颜色
		CString sumVol;      // 累计成交量后缀（显示在瞬时变化量前面）
		COLORREF sumVolColor{ RGB(0,0,0) };  // 累计成交量后缀颜色
		COLORREF priceColor;
		bool fillBackground{ false };
		COLORREF backgroundColor;
		bool darkBackground{ false };  // 深色背景时文字改白色
		bool bold{ false };   // 粗体
		double orderRatio{ 0.0 };  // 卖一/买一后方挂单量占比（0~1），用于绘制背景色
		double sumVolRatio{ 0.0 };  // 累计买入与累计卖出成交量占比（0~1），用于绘制背景色
	};

	// 布局上下文（由Draw计算，传递给各子函数）
	struct LayoutContext
	{
		int left;
		int right;
		int height;
		int headerHeight;
		int obTitleH;
		int topOffset;
		int panelW;
		int totalRows;
		int rowHeight;
		int contentH;
		int rem;
		int textX;
		// 计算第i行(0-based)的Y坐标
		int RowY(int i) const
		{
			if (i < rem) return topOffset + i * (rowHeight + 1);
			else return topOffset + rem * (rowHeight + 1) + (i - rem) * rowHeight;
		}
		// 计算第i行的高度
		int RowH(int i) const
		{
			return (i < rem) ? (rowHeight + 1) : rowHeight;
		}
	};

	// 绘制净比99（行15）
	void DrawNetRatio99(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo);

	// 绘制振幅（行16）
	void DrawAmplitude(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo,
		const std::vector<STOCK::KLinePoint>& klineData);

	// 绘制换手率（行17）
	void DrawTurnoverRate(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo);

	// ===== 精简版盘口（新版Draw）用到的子项 =====
	// 绘制最高/最低行
	void DrawHighLow(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo);
	// 绘制选线分割（净比00柱状图，barY为绘制行的Y坐标）
	void DrawNetRatio00Ex(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo, int barY);
	// 绘制净流入汇总行（label在左、金额右对齐，正红负绿）
	void DrawNetInflowRow(CDC& memDC, const LayoutContext& lc, int rowIndex, const CString& label, double netInflow);
	// 绘制行明细（从startRow开始，最多10行，最新在下）
	void DrawTickMini(CDC& memDC, const LayoutContext& lc, int startRow);
	// 刷新行明细缓存（limit条）并计算整日累计净流入
	void RefreshTickMini(const STOCK::StockInfo& stockInfo, int limit);

	// 辅助：绘制单行盘口文本（含小号后缀、右对齐后缀）
	// x, y: 文本绘制坐标；rowWidth: 文本区宽度
	// rowLeft, rowTop, rowHeight: 当前行的左边界、顶部、高度（基金净值紫色横线据此定位）
	void DrawOrderBookRowText(CDC& memDC, const OrderBookRow& row, int x, int y, int rowWidth,
		int rowLeft, int rowTop, int rowHeight);

	// 辅助：绘制净比条形图
	void DrawRatioBar(CDC& memDC, int x, int y, int w, int h, double ratio);

	// 辅助：在净比条形图上绘制文本
	void DrawNetRatioBarText(CDC& memDC, int x, int y, int w, int h, const CString& ratioText, const CString& diffText);

	// 辅助：获取净比颜色索引（0=0~30, 1=30~60, 2=60+）
	static int GetNetRatioColorIndex(double ratio);

	// 辅助：获取卖一/买一后方累计主动成交量的瞬时变化量（手）
	// isAskSide=true(卖一)返回累计主动买变化量，isAskSide=false(买一)返回累计主动卖变化量
	STOCK::Volume GetOrderDeltaLots(STOCK::Price price, bool isAskSide) const;

	// 辅助：获取盘口累计成交量（手）
	// 返回该价格的主动买,主动卖累计量
	std::pair<STOCK::Volume, STOCK::Volume> GetOrderBookCumVol(STOCK::Price price) const;

	// 辅助：每隔5秒从数据库 tick_trade 聚合刷新各价格档位的真实累计成交量
	// ETF 的成交价格在DB中被Python放大了10倍，此处除以10还原，使key与真实盘口价格一致
	void RefreshPriceCumVol(const STOCK::StockInfo& stockInfo);

	// 辅助：计算净比趋势箭头
	static CString CalcNetRatioTrend(double ratio, double previousRatio);

	// 辅助：构建卖盘行数据
	OrderBookRow BuildAskRow(const STOCK::StockInfo& stockInfo, int idx, STOCK::Volume delta) const;

	// 辅助：构建买盘行数据
	OrderBookRow BuildBidRow(const STOCK::StockInfo& stockInfo, int idx, STOCK::Volume delta) const;

	// 辅助：绘制一组盘口行
	void DrawPriceRows(CDC& memDC, const LayoutContext& lc, const std::vector<OrderBookRow>& rows, int startRow);

private:
	// 缓存数据（原Draw中的static变量）
	static const COLORREF NET_RATIO_RED_COLORS[3];
	static const COLORREF NET_RATIO_GREEN_COLORS[3];
	// 净比99趋势缓存
	static std::map<std::wstring, double> m_lastNetRatioMap;
	static std::map<std::wstring, CString> m_lastNetRatioTrendMap;

	static std::map<std::wstring, std::map<int, CString>> m_lastPeriodRatioTrendMap;

	// 各价格档位的真实累计成交量（来自数据库 tick_trade 聚合）
	// key 为价格量化到0.0001精度后的整数，避免浮点相等比较误差
	struct PriceCumVol
	{
		STOCK::Volume activeBuyVol{ 0 };   // 主动买(buyOrSell=0)累计手
		STOCK::Volume activeSellVol{ 0 };  // 主动卖(buyOrSell=1)累计手
	};
	std::map<long long, PriceCumVol> m_priceCumVolMap;   // 当前累计成交量
	std::map<long long, PriceCumVol> m_priceCumVolPrev;  // 上次采样的累计成交量（用于算瞬时变化）
	std::wstring m_priceCumVolCode;   // 当前已加载累计成交量的股票代码
	DWORD m_lastCumVolRefreshTick{ 0 };  // 上次刷新累计成交量的时机(ms)

	// 成交明细缓存（MX模式，最近20条）
	std::vector<STOCK::Transaction> m_tickDetails;
	std::wstring m_tickDetailCode;   // 当前已加载明细的股票代码
	DWORD m_lastTickRefreshTick{ 0 };  // 上次刷新明细的时机(ms)
	double m_cumNetInflow{ 0.0 };      // 整日累计净流入额（元）
	double m_intervalNetInflow{ 0.0 }; // 区间（当前10条）净流入额（元）

	// 精简版盘口内嵌的10行明细缓存（与MX模式的DrawTickDetail独立刷新）
	std::vector<STOCK::Transaction> m_tickMiniDetails;
	std::wstring m_tickMiniCode;
	DWORD m_lastTickMiniRefreshTick{ 0 };
};
