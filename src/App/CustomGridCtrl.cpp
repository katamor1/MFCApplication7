#include "CustomGridCtrl.h"

#include "resource.h"

#include <algorithm>
#include <vector>

namespace {

/**
 * @brief Convert wide string to an MFC CString.
 */
CString ToCString(const std::wstring& value)
{
    return CString(value.c_str());
}

std::wstring ToWString(const CString& value)
{
    return std::wstring(value.GetString());
}

bool SameBinding(const GridRowBinding& left, const GridRowBinding& right)
{
    return left.containerNo == right.containerNo &&
        left.itemNo == right.itemNo &&
        left.dataId == right.dataId &&
        left.externalAppId == right.externalAppId;
}

bool HasStableBinding(const GridRowBinding& binding)
{
    return binding.containerNo != 0 ||
        binding.itemNo != 0 ||
        binding.dataId != 0 ||
        !binding.externalAppId.empty();
}

} // namespace

/**
 * @file CustomGridCtrl.cpp
 * @brief Implements grid control rendering and cell/binding query helpers.
 */

class CGridInPlaceEdit final : public CEdit
{
public:
    explicit CGridInPlaceEdit(CCustomGridCtrl& owner)
        : owner_(&owner)
    {
    }

protected:
    afx_msg void OnKeyDown(UINT charCode, UINT repeatCount, UINT flags)
    {
        if (charCode == VK_RETURN) {
            CString text;
            GetWindowText(text);
            owner_->CommitEditValue(ToWString(text));
            return;
        }
        if (charCode == VK_ESCAPE) {
            owner_->CancelEdit();
            return;
        }
        CEdit::OnKeyDown(charCode, repeatCount, flags);
    }

    afx_msg void OnKillFocus(CWnd* newWindow)
    {
        CEdit::OnKillFocus(newWindow);
        if (owner_ != nullptr && !owner_->destroyingEditors_) {
            CString text;
            GetWindowText(text);
            owner_->CommitEditValue(ToWString(text));
        }
    }

    DECLARE_MESSAGE_MAP()

private:
    CCustomGridCtrl* owner_{};
};

BEGIN_MESSAGE_MAP(CGridInPlaceEdit, CEdit)
    ON_WM_KEYDOWN()
    ON_WM_KILLFOCUS()
END_MESSAGE_MAP()

class CGridInPlaceCombo final : public CComboBox
{
public:
    explicit CGridInPlaceCombo(CCustomGridCtrl& owner)
        : owner_(&owner)
    {
    }

protected:
    afx_msg void OnKeyDown(UINT charCode, UINT repeatCount, UINT flags)
    {
        if (charCode == VK_RETURN) {
            CString text;
            GetWindowText(text);
            owner_->CommitEditValue(ToWString(text));
            return;
        }
        if (charCode == VK_ESCAPE) {
            owner_->CancelEdit();
            return;
        }
        CComboBox::OnKeyDown(charCode, repeatCount, flags);
    }

    afx_msg void OnKillFocus(CWnd* newWindow)
    {
        CComboBox::OnKillFocus(newWindow);
        if (owner_ != nullptr && !owner_->destroyingEditors_) {
            CString text;
            GetWindowText(text);
            owner_->CommitEditValue(ToWString(text));
        }
    }

    DECLARE_MESSAGE_MAP()

private:
    CCustomGridCtrl* owner_{};
};

BEGIN_MESSAGE_MAP(CGridInPlaceCombo, CComboBox)
    ON_WM_KEYDOWN()
    ON_WM_KILLFOCUS()
END_MESSAGE_MAP()

BEGIN_MESSAGE_MAP(CCustomGridCtrl, CListCtrl)
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONDBLCLK()
    ON_WM_KEYDOWN()
END_MESSAGE_MAP()

CCustomGridCtrl::CCustomGridCtrl() = default;

CCustomGridCtrl::~CCustomGridCtrl()
{
    DestroyEditors();
}

/**
 * @brief Apply a full model snapshot into the list control view.
 * @param model Display model to render.
 */
void CCustomGridCtrl::ApplyModel(const GridModel& model)
{
    std::vector<GridRowBinding> selectedBindings;
    std::vector<int> selectedRows;
    POSITION position = GetFirstSelectedItemPosition();
    while (position != nullptr) {
        const int selectedRow = GetNextSelectedItem(position);
        selectedRows.push_back(selectedRow);
        if (selectedRow >= 0 && selectedRow < static_cast<int>(model_.Rows().size())) {
            const auto& binding = model_.Rows()[static_cast<size_t>(selectedRow)].binding;
            if (HasStableBinding(binding)) {
                selectedBindings.push_back(binding);
            }
        }
    }

    CancelEdit();
    model_ = model;

    DeleteAllItems();
    while (DeleteColumn(0)) {
    }

    for (int column = 0; column < static_cast<int>(model_.Columns().size()); ++column) {
        InsertColumn(column, ToCString(model_.Columns()[column]), LVCFMT_LEFT, 180);
    }

    for (int row = 0; row < static_cast<int>(model_.Rows().size()); ++row) {
        const auto& cells = model_.Rows()[row].cells;
        if (cells.empty()) {
            continue;
        }
        InsertItem(row, ToCString(cells[0].text));
        for (int column = 1; column < static_cast<int>(cells.size()); ++column) {
            SetItemText(row, column, ToCString(cells[column].text));
        }
    }

    for (int row = 0; row < static_cast<int>(model_.Rows().size()); ++row) {
        const auto& binding = model_.Rows()[static_cast<size_t>(row)].binding;
        const bool bindingMatched = HasStableBinding(binding) &&
            std::any_of(selectedBindings.begin(), selectedBindings.end(), [&](const GridRowBinding& selected) {
                return SameBinding(selected, binding);
            });
        const bool rowMatched = selectedBindings.empty() &&
            std::find(selectedRows.begin(), selectedRows.end(), row) != selectedRows.end();
        if (bindingMatched || rowMatched) {
            SetItemState(row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        }
    }
}

/**
 * @brief Enable or disable in-place editing.
 */
void CCustomGridCtrl::SetEditingEnabled(bool enabled)
{
    editingEnabled_ = enabled;
    if (!editingEnabled_) {
        CancelEdit();
    }
}

/**
 * @brief Start editing for a model cell when policy allows it.
 */
bool CCustomGridCtrl::BeginEditCell(int row, int column)
{
    if (!editingEnabled_ || !IsValidCell(row, column)) {
        return false;
    }

    const auto& cell = model_.Rows()[static_cast<size_t>(row)].cells[static_cast<size_t>(column)];
    if (!IsEditableCellKind(cell.kind)) {
        return false;
    }

    DestroyEditors();
    editingRow_ = row;
    editingColumn_ = column;
    currentColumn_ = column;

    if (cell.kind == CellKind::CheckBox) {
        const std::wstring nextValue = cell.text == L"true" ? L"false" : L"true";
        return CommitEditValue(nextValue);
    }

    const CRect cellRect = CellRectFor(row, column);
    if (cellRect.IsRectEmpty()) {
        CancelEdit();
        return false;
    }

    if (cell.kind == CellKind::ComboBox || cell.kind == CellKind::RadioButton) {
        if (cell.options.empty()) {
            CancelEdit();
            return false;
        }

        comboCtrl_ = new CGridInPlaceCombo(*this);
        CRect comboRect = cellRect;
        comboRect.bottom += 160;
        if (!comboCtrl_->Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_HASSTRINGS,
                                comboRect,
                                this,
                                IDC_GRID_INPLACE_COMBO)) {
            delete comboCtrl_;
            comboCtrl_ = nullptr;
            CancelEdit();
            return false;
        }
        for (const auto& option : cell.options) {
            comboCtrl_->AddString(ToCString(option));
        }
        const int selected = comboCtrl_->FindStringExact(-1, ToCString(cell.text));
        comboCtrl_->SetCurSel(selected >= 0 ? selected : 0);
        comboCtrl_->SetFocus();
        comboCtrl_->ShowDropDown(TRUE);
        return true;
    }

    editCtrl_ = new CGridInPlaceEdit(*this);
    if (!editCtrl_->Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                           cellRect,
                           this,
                           IDC_GRID_INPLACE_EDIT)) {
        delete editCtrl_;
        editCtrl_ = nullptr;
        CancelEdit();
        return false;
    }
    editCtrl_->SetWindowText(ToCString(cell.text));
    editCtrl_->SetFocus();
    editCtrl_->SetSel(0, -1);
    return true;
}

/**
 * @brief Cancel current edit without changing model text.
 */
void CCustomGridCtrl::CancelEdit()
{
    DestroyEditors();
}

/**
 * @brief Return the last committed cell edit.
 */
GridEditCommit CCustomGridCtrl::LastEditCommit() const
{
    return lastEditCommit_;
}

/**
 * @brief Return editable kind for one cell coordinate.
 * @param row Row index.
 * @param column Column index.
 * @return Cell kind for rendering and edit policy.
 */
CellKind CCustomGridCtrl::CellKindAt(int row, int column) const
{
    if (row < 0 || column < 0 || row >= static_cast<int>(model_.Rows().size())) {
        return CellKind::ReadOnlyText;
    }
    const auto& cells = model_.Rows()[row].cells;
    if (column >= static_cast<int>(cells.size())) {
        return CellKind::ReadOnlyText;
    }
    return cells[column].kind;
}

/**
 * @brief Return row binding for command handlers.
 * @param row Row index.
 * @return Bound payload (container/item mapping) or default values if out of range.
 */
GridRowBinding CCustomGridCtrl::RowBindingAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(model_.Rows().size())) {
        return {};
    }
    return model_.Rows()[row].binding;
}

/**
 * @brief Return current model for command helpers that need visible-row order.
 */
const GridModel& CCustomGridCtrl::Model() const noexcept
{
    return model_;
}

/**
 * @brief Remember clicked column and toggle checkbox edits when enabled.
 */
void CCustomGridCtrl::OnLButtonDown(UINT flags, CPoint point)
{
    int row = -1;
    const int column = HitTestColumn(point, row);
    if (row >= 0 && column >= 0) {
        currentColumn_ = column;
        if (editingEnabled_ && CellKindAt(row, column) == CellKind::CheckBox) {
            SetItemState(row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            BeginEditCell(row, column);
            return;
        }
    }
    CListCtrl::OnLButtonDown(flags, point);
}

/**
 * @brief Double-click starts editing for the clicked editable cell.
 */
void CCustomGridCtrl::OnLButtonDblClk(UINT flags, CPoint point)
{
    int row = -1;
    const int column = HitTestColumn(point, row);
    if (row >= 0 && column >= 0 && BeginEditCell(row, column)) {
        return;
    }
    CListCtrl::OnLButtonDblClk(flags, point);
}

/**
 * @brief Enter starts editing the current selected cell/row.
 */
void CCustomGridCtrl::OnKeyDown(UINT charCode, UINT repeatCount, UINT flags)
{
    if (charCode == VK_RETURN && editingEnabled_) {
        POSITION position = GetFirstSelectedItemPosition();
        const int row = position == nullptr ? -1 : GetNextSelectedItem(position);
        const int column = IsValidCell(row, currentColumn_) ? currentColumn_ : FirstEditableColumn(row);
        if (row >= 0 && column >= 0 && BeginEditCell(row, column)) {
            return;
        }
    }
    CListCtrl::OnKeyDown(charCode, repeatCount, flags);
}

/**
 * @brief Validate and commit the current editor value into the model and list view.
 */
bool CCustomGridCtrl::CommitEditValue(const std::wstring& value)
{
    if (!IsValidCell(editingRow_, editingColumn_)) {
        return false;
    }

    const auto& row = model_.Rows()[static_cast<size_t>(editingRow_)];
    const auto& cell = row.cells[static_cast<size_t>(editingColumn_)];
    const auto validation = ValidateGridEditValue(cell, value);
    if (!validation.valid) {
        return false;
    }

    lastEditCommit_.row = editingRow_;
    lastEditCommit_.column = editingColumn_;
    lastEditCommit_.oldText = cell.text;
    lastEditCommit_.newText = value;
    lastEditCommit_.kind = cell.kind;
    lastEditCommit_.binding = row.binding;

    if (!model_.SetCellText(editingRow_, editingColumn_, value)) {
        return false;
    }
    SetItemText(editingRow_, editingColumn_, ToCString(value));

    DestroyEditors();
    NotifyEditCommit();
    return true;
}

/**
 * @brief Destroy active child editor controls and clear edit coordinates.
 */
void CCustomGridCtrl::DestroyEditors()
{
    destroyingEditors_ = true;
    if (editCtrl_) {
        if (editCtrl_->GetSafeHwnd() != nullptr) {
            editCtrl_->DestroyWindow();
        }
        delete editCtrl_;
        editCtrl_ = nullptr;
    }
    if (comboCtrl_) {
        if (comboCtrl_->GetSafeHwnd() != nullptr) {
            comboCtrl_->DestroyWindow();
        }
        delete comboCtrl_;
        comboCtrl_ = nullptr;
    }
    destroyingEditors_ = false;
    editingRow_ = -1;
    editingColumn_ = -1;
}

/**
 * @brief Notify parent window that LastEditCommit() has new data.
 */
void CCustomGridCtrl::NotifyEditCommit()
{
    CWnd* parent = GetParent();
    if (parent != nullptr && parent->GetSafeHwnd() != nullptr) {
        parent->SendMessage(WM_GRID_EDIT_COMMITTED, static_cast<WPARAM>(GetDlgCtrlID()), reinterpret_cast<LPARAM>(this));
    }
}

/**
 * @brief Check whether a row/column pair points to a real model cell.
 */
bool CCustomGridCtrl::IsValidCell(int row, int column) const
{
    if (row < 0 || column < 0 || row >= static_cast<int>(model_.Rows().size())) {
        return false;
    }
    return column < static_cast<int>(model_.Rows()[static_cast<size_t>(row)].cells.size());
}

/**
 * @brief Resolve list row and subitem column from a mouse coordinate.
 */
int CCustomGridCtrl::HitTestColumn(CPoint point, int& row) const
{
    LVHITTESTINFO hit{};
    hit.pt = point;
    row = ListView_SubItemHitTest(m_hWnd, &hit);
    if (row < 0) {
        return -1;
    }
    return hit.iSubItem;
}

/**
 * @brief Return client rectangle for a cell, adjusted for editor controls.
 */
CRect CCustomGridCtrl::CellRectFor(int row, int column)
{
    CRect rect;
    if (!GetSubItemRect(row, column, LVIR_BOUNDS, rect)) {
        return {};
    }
    rect.DeflateRect(1, 1);
    return rect;
}

/**
 * @brief Find the first editable cell in a row for keyboard edit start.
 */
int CCustomGridCtrl::FirstEditableColumn(int row) const
{
    if (row < 0 || row >= static_cast<int>(model_.Rows().size())) {
        return -1;
    }
    const auto& cells = model_.Rows()[static_cast<size_t>(row)].cells;
    for (int column = 0; column < static_cast<int>(cells.size()); ++column) {
        if (IsEditableCellKind(cells[static_cast<size_t>(column)].kind)) {
            return column;
        }
    }
    return -1;
}
