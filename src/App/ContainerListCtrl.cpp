#include "ContainerListCtrl.h"

#include "resource.h"

#include <algorithm>
#include <vector>

/**
 * @file ContainerListCtrl.cpp
 * @brief Row-major 3-column container list drawing, scrolling, and hit testing.
 */

namespace {

constexpr int kCardHeight = 78;
constexpr int kGap = 8;

CString ToCString(const std::wstring& value)
{
    return CString(value.c_str());
}

COLORREF CardColor(const ContainerListCell& cell)
{
    if (cell.selected) {
        return RGB(205, 225, 255);
    }
    if (cell.missing) {
        return RGB(225, 225, 225);
    }
    if (cell.state == L"異常検知") {
        return RGB(255, 210, 210);
    }
    if (cell.state == L"満載") {
        return RGB(255, 245, 190);
    }
    if (cell.state == L"追加可能") {
        return RGB(215, 245, 215);
    }
    return RGB(255, 255, 255);
}

CRect CardRectForCell(const CRect& client, int columnCount, const ContainerListCell& cell, int topRow)
{
    const int columns = std::max(1, columnCount);
    const int cardWidth = std::max(1, (client.Width() - kGap * (columns + 1)) / columns);
    const int left = client.left + kGap + cell.column * (cardWidth + kGap);
    const int top = client.top + kGap + (cell.row - topRow) * (kCardHeight + kGap);
    return CRect(left, top, left + cardWidth, top + kCardHeight);
}

void DrawTextLine(CDC& dc, const CRect& card, int lineIndex, const std::wstring& text, UINT flags)
{
    CRect line(card.left + 8, card.top + 8 + lineIndex * 20, card.right - 8, card.top + 28 + lineIndex * 20);
    dc.DrawText(ToCString(text), line, flags | DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER);
}

} // namespace

BEGIN_MESSAGE_MAP(CContainerListCtrl, CWnd)
    ON_WM_PAINT()
    ON_WM_LBUTTONDOWN()
    ON_WM_VSCROLL()
    ON_WM_MOUSEWHEEL()
    ON_WM_SIZE()
END_MESSAGE_MAP()

/**
 * @brief Register and create the custom container list window.
 */
BOOL CContainerListCtrl::Create(DWORD style, const RECT& rect, CWnd* parent, UINT id)
{
    const CString className = AfxRegisterWndClass(CS_DBLCLKS,
                                                  ::LoadCursor(nullptr, IDC_ARROW),
                                                  reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
                                                  nullptr);
    return CWnd::Create(className, L"", style | WS_CHILD | WS_VSCROLL, rect, parent, id);
}

/**
 * @brief Replace rendered model and preserve explicit selection.
 */
void CContainerListCtrl::ApplyModel(const ContainerListLayoutModel& model)
{
    model_ = model;
    const auto selected = std::find_if(model_.cells.begin(), model_.cells.end(), [](const ContainerListCell& cell) {
        return cell.selected;
    });
    if (selected != model_.cells.end()) {
        selectedContainerNo_ = selected->containerNo;
    }
    ApplySelectionToModel();
    UpdateScrollBar();
    Invalidate();
}

/**
 * @brief Update selected cell without replacing model data.
 */
void CContainerListCtrl::SetSelectedContainer(int containerNo)
{
    selectedContainerNo_ = std::max(1, std::min(100, containerNo));
    ApplySelectionToModel();
    Invalidate();
}

/**
 * @brief Return selected container number for parent command handling.
 */
int CContainerListCtrl::SelectedContainerNo() const noexcept
{
    return selectedContainerNo_;
}

/**
 * @brief Paint visible row-major 3-column container cards.
 */
void CContainerListCtrl::OnPaint()
{
    CPaintDC dc(this);
    CRect client;
    GetClientRect(&client);
    dc.FillSolidRect(client, RGB(245, 247, 250));
    dc.SetBkMode(TRANSPARENT);

    if (model_.cells.empty() || model_.columnCount <= 0) {
        dc.DrawText(L"コンテナ一覧未取得", &client, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    CPen borderPen(PS_SOLID, 1, RGB(115, 125, 135));
    CPen selectedPen(PS_SOLID, 2, RGB(35, 105, 210));
    CPen* oldPen = dc.SelectObject(&borderPen);

    const int bottomLimit = client.bottom - kGap;
    for (const auto& cell : model_.cells) {
        if (cell.row < topRow_) {
            continue;
        }
        CRect card = CardRectForCell(client, model_.columnCount, cell, topRow_);
        if (card.top >= bottomLimit) {
            continue;
        }

        CBrush brush(CardColor(cell));
        dc.FillRect(card, &brush);
        dc.SelectObject(cell.selected ? &selectedPen : &borderPen);
        dc.Rectangle(card);

        DrawTextLine(dc, card, 0, L"No. " + cell.displayText, DT_LEFT);
        DrawTextLine(dc, card, 1, cell.containerName, DT_LEFT);
        DrawTextLine(dc, card, 2, cell.state, DT_LEFT);
    }

    dc.SelectObject(oldPen);
}

/**
 * @brief Hit-test clicked card, update selection, and notify parent dialog.
 */
void CContainerListCtrl::OnLButtonDown(UINT flags, CPoint point)
{
    CRect client;
    GetClientRect(&client);
    for (const auto& hit : BuildHitCells(client)) {
        if (hit.rect.PtInRect(point)) {
            SetSelectedContainer(hit.containerNo);
            if (CWnd* parent = GetParent()) {
                parent->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_CONTAINER_LIST_LAYOUT, BN_CLICKED), reinterpret_cast<LPARAM>(m_hWnd));
            }
            break;
        }
    }
    CWnd::OnLButtonDown(flags, point);
}

/**
 * @brief Handle vertical scrollbar commands as row-based scrolling.
 */
void CContainerListCtrl::OnVScroll(UINT code, UINT position, CScrollBar* scrollBar)
{
    int target = topRow_;
    CRect client;
    GetClientRect(&client);
    const int page = std::max(1, VisibleRowCount(client) - 1);

    switch (code) {
    case SB_LINEUP:
        --target;
        break;
    case SB_LINEDOWN:
        ++target;
        break;
    case SB_PAGEUP:
        target -= page;
        break;
    case SB_PAGEDOWN:
        target += page;
        break;
    case SB_THUMBPOSITION:
    case SB_THUMBTRACK:
        target = static_cast<int>(position);
        break;
    case SB_TOP:
        target = 0;
        break;
    case SB_BOTTOM:
        target = MaxTopRow(client);
        break;
    default:
        break;
    }

    ScrollToTopRow(target);
    CWnd::OnVScroll(code, position, scrollBar);
}

/**
 * @brief Scroll rows with mouse wheel.
 */
BOOL CContainerListCtrl::OnMouseWheel(UINT flags, short delta, CPoint point)
{
    const int rows = delta > 0 ? -3 : 3;
    ScrollToTopRow(topRow_ + rows);
    return CWnd::OnMouseWheel(flags, delta, point);
}

/**
 * @brief Keep scrollbar range consistent when resized.
 */
void CContainerListCtrl::OnSize(UINT type, int cx, int cy)
{
    CWnd::OnSize(type, cx, cy);
    UpdateScrollBar();
}

/**
 * @brief Return number of full rows visible in current client area.
 */
int CContainerListCtrl::VisibleRowCount(const CRect& client) const noexcept
{
    return std::max(1, (client.Height() - kGap) / (kCardHeight + kGap));
}

/**
 * @brief Return maximum top row allowed for the current client area.
 */
int CContainerListCtrl::MaxTopRow(const CRect& client) const noexcept
{
    return std::max(0, model_.rowCount - VisibleRowCount(client));
}

/**
 * @brief Clamp scroll row, update scrollbar and redraw.
 */
void CContainerListCtrl::ScrollToTopRow(int topRow)
{
    CRect client;
    GetClientRect(&client);
    topRow_ = std::max(0, std::min(topRow, MaxTopRow(client)));
    UpdateScrollBar();
    Invalidate();
}

/**
 * @brief Sync native vertical scrollbar with model row count.
 */
void CContainerListCtrl::UpdateScrollBar()
{
    if (GetSafeHwnd() == nullptr) {
        return;
    }

    CRect client;
    GetClientRect(&client);
    topRow_ = std::max(0, std::min(topRow_, MaxTopRow(client)));

    SCROLLINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    info.nMin = 0;
    info.nMax = std::max(0, model_.rowCount - 1);
    info.nPage = static_cast<UINT>(VisibleRowCount(client));
    info.nPos = topRow_;
    SetScrollInfo(SB_VERT, &info, TRUE);
}

/**
 * @brief Build hit rectangles from current layout geometry.
 */
std::vector<CContainerListCtrl::HitCell> CContainerListCtrl::BuildHitCells(const CRect& client) const
{
    std::vector<HitCell> hits;
    if (model_.cells.empty() || model_.columnCount <= 0) {
        return hits;
    }

    const int bottomLimit = client.bottom - kGap;
    hits.reserve(model_.cells.size());
    for (const auto& cell : model_.cells) {
        if (cell.row < topRow_) {
            continue;
        }
        CRect rect = CardRectForCell(client, model_.columnCount, cell, topRow_);
        if (rect.top >= bottomLimit) {
            continue;
        }
        hits.push_back({rect, cell.containerNo});
    }
    return hits;
}

/**
 * @brief Keep model cell flags consistent with selectedContainerNo_.
 */
void CContainerListCtrl::ApplySelectionToModel()
{
    for (auto& cell : model_.cells) {
        cell.selected = cell.containerNo == selectedContainerNo_;
    }
}
