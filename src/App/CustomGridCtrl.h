#pragma once

#include "GridModel.h"

#include <afxcmn.h>
#include <afxwin.h>

constexpr UINT WM_GRID_EDIT_COMMITTED = WM_APP + 40;

/**
 * @brief セル編集確定時に親へ通知される最後の編集内容。
 */
struct GridEditCommit
{
    int row{-1};
    int column{-1};
    std::wstring oldText;
    std::wstring newText;
    CellKind kind{CellKind::ReadOnlyText};
    GridRowBinding binding;
};

class CGridInPlaceEdit;
class CGridInPlaceCombo;

/**
 * @brief GridModel を表示するための CListCtrl ラッパ。
 */
class CCustomGridCtrl final : public CListCtrl
{
public:
    CCustomGridCtrl();
    ~CCustomGridCtrl() override;

    /**
     * @brief 受け取ったモデルを画面に反映する。
     */
    void ApplyModel(const GridModel& model);
    /**
     * @brief セル編集UIの有効/無効を切り替える。
     */
    void SetEditingEnabled(bool enabled);
    /**
     * @brief 指定セルの編集を開始する。
     */
    bool BeginEditCell(int row, int column);
    /**
     * @brief 現在の編集を破棄する。
     */
    void CancelEdit();
    /**
     * @brief 直近の編集確定内容を返す。
     */
    GridEditCommit LastEditCommit() const;
    /**
     * @brief 指定セルの種別を返す（編集可否判定に使う）。
     */
    CellKind CellKindAt(int row, int column) const;
    /**
     * @brief 指定行のバインド情報を返す。
     */
    GridRowBinding RowBindingAt(int row) const;
    /**
     * @brief 現在表示中のモデルを返す。
     */
    const GridModel& Model() const noexcept;

protected:
    afx_msg void OnLButtonDown(UINT flags, CPoint point);
    afx_msg void OnLButtonDblClk(UINT flags, CPoint point);
    afx_msg void OnKeyDown(UINT charCode, UINT repeatCount, UINT flags);

    DECLARE_MESSAGE_MAP()

private:
    friend class CGridInPlaceEdit;
    friend class CGridInPlaceCombo;

    bool CommitEditValue(const std::wstring& value);
    void DestroyEditors();
    void NotifyEditCommit();
    bool IsValidCell(int row, int column) const;
    int HitTestColumn(CPoint point, int& row) const;
    CRect CellRectFor(int row, int column);
    int FirstEditableColumn(int row) const;

    GridModel model_;
    bool editingEnabled_{false};
    int editingRow_{-1};
    int editingColumn_{-1};
    int currentColumn_{0};
    bool destroyingEditors_{false};
    GridEditCommit lastEditCommit_;
    CGridInPlaceEdit* editCtrl_{};
    CGridInPlaceCombo* comboCtrl_{};
};
