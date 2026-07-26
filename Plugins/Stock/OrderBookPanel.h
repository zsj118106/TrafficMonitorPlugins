#pragma once

#include <StockDef.h>
#include <string>
#include <vector>

// 盘口面板绘制
// 职责：在指定矩形区域内绘制五档买卖盘、委比、趋势判定、净比(1/5/10/20/99)、
//       振幅、换手率等盘口信息
// 数据来源：stockInfo（实时行情）、klineData（K线数据，用于振幅计算）
// 说明：trendSection 依赖当前视图模式（分时/5分钟/30分钟/日K）来判定趋势方向
class COrderBookPanel
{
public:
	// 绘制盘口面板
	// left, right: 面板左右边界（含主标题栏下方的盘口标题栏区域）
	// height: 面板总高度（含盘口标题栏）
	// stockId: 当前股票代码（用于获取累加挂单量、趋势数据等）
	// isKLineMode, isMin5KLineMode, isMin30KLineMode: 当前视图模式（用于趋势判定）
	void Draw(CDC& memDC, int left, int right, int height, const STOCK::StockInfo& stockInfo,
		const std::vector<STOCK::KLinePoint>& klineData,
		const std::wstring& stockId, bool isKLineMode, bool isMin5KLineMode, bool isMin30KLineMode);
};
