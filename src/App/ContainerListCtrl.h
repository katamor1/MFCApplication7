#pragma once

#include "ScreenModels.h"

#include <afxwin.h>

#include <vector>

/**
 * @brief コンテナ一覧画面の3列カード表示を描画するカスタムコントロール。
 */
class CContainerListCtrl final : public CWnd
{
public:
    /**
     * @brief コントロールウィンドウを生成する。
     */
    BOOL Create(DWORD style, const RECT& rect, CWnd* parent, UINT id);
    /**
     * @brief 最新の3列一覧モデルを描画対象へ反映する。
     */
    void ApplyModel(const ContainerListLayoutModel& model);
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
    afx_msg void OnVScroll(UINT code, UINT position, CScrollBar* scrollBar);
    afx_msg BOOL OnMouseWheel(UINT flags, short delta, CPoint point);
    afx_msg void OnSize(UINT type, int cx, int cy);

    DECLARE_MESSAGE_MAP()

private:
    struct HitCell
    {
        CRect rect;
        int containerNo{};
    };

    int VisibleRowCount(const CRect& client) const noexcept;
    int MaxTopRow(const CRect& client) const noexcept;
    void ScrollToTopRow(int topRow);
    void UpdateScrollBar();
    std::vector<HitCell> BuildHitCells(const CRect& client) const;
    void ApplySelectionToModel();

    ContainerListLayoutModel model_;
    int selectedContainerNo_{1};
    int topRow_{0};
};
