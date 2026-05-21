#pragma once

#include "ScreenModels.h"

#include <afxwin.h>

#include <vector>

/**
 * @brief コンテナステーション固定配置図を描画するカスタムコントロール。
 */
class CStationLayoutCtrl final : public CWnd
{
public:
    /**
     * @brief コントロールウィンドウを生成する。
     */
    BOOL Create(DWORD style, const RECT& rect, CWnd* parent, UINT id);
    /**
     * @brief 最新の配置モデルを描画対象へ反映する。
     */
    void ApplyModel(const StationLayoutModel& model);
    /**
     * @brief 選択コンテナ番号だけを更新する。
     */
    void SetSelectedContainer(int containerNo);
    /**
     * @brief 現在選択中のコンテナ番号を返す。
     */
    int SelectedContainerNo() const noexcept;

protected:
    afx_msg void OnPaint();
    afx_msg void OnLButtonDown(UINT flags, CPoint point);

    DECLARE_MESSAGE_MAP()

private:
    struct HitCell
    {
        CRect rect;
        int containerNo{};
    };

    std::vector<HitCell> BuildHitCells(const CRect& client) const;
    void ApplySelectionToModel();

    StationLayoutModel model_;
    int selectedContainerNo_{1};
};
