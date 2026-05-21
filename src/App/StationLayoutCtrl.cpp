#include "StationLayoutCtrl.h"

#include "resource.h"

#include <algorithm>
#include <cmath>
#include <vector>

/**
 * @file StationLayoutCtrl.cpp
 * @brief Fixed 5x20 station layout drawing and hit testing.
 */

namespace {

struct LayoutMetrics
{
    CRect header;
    CRect grid;
    int columnWidth{};
    int rowHeight{};
    int columnGap{8};
    int rowGap{2};
};

CString ToCString(const std::wstring& value)
{
    return CString(value.c_str());
}

const wchar_t* KindLabel(StationLayoutKind kind)
{
    switch (kind) {
    case StationLayoutKind::LeftSemiCircle:
        return L"左半円";
    case StationLayoutKind::BottomSemiCircle:
        return L"下半円";
    case StationLayoutKind::RightSemiCircle:
        return L"右半円";
    case StationLayoutKind::Straight:
    default:
        return L"直線";
    }
}

LayoutMetrics ComputeLayout(const CRect& client, int columnCount, int rowsPerColumn)
{
    LayoutMetrics metrics;
    CRect inner = client;
    inner.DeflateRect(10, 8, 10, 10);

    const int headerHeight = 26;
    metrics.header = inner;
    metrics.header.bottom = std::min(inner.bottom, inner.top + headerHeight);
    metrics.grid = inner;
    metrics.grid.top = metrics.header.bottom + 4;

    columnCount = std::max(1, columnCount);
    rowsPerColumn = std::max(1, rowsPerColumn);
    metrics.columnWidth = std::max(1, (metrics.grid.Width() - metrics.columnGap * (columnCount - 1)) / columnCount);
    metrics.rowHeight = std::max(1, (metrics.grid.Height() - metrics.rowGap * (rowsPerColumn - 1)) / rowsPerColumn);
    return metrics;
}

CRect ColumnRect(const LayoutMetrics& metrics, int column, int rowsPerColumn)
{
    const int left = metrics.grid.left + column * (metrics.columnWidth + metrics.columnGap);
    const int right = left + metrics.columnWidth;
    const int bottom = metrics.grid.top + rowsPerColumn * metrics.rowHeight + (rowsPerColumn - 1) * metrics.rowGap;
    return CRect(left, metrics.grid.top, right, std::min<LONG>(bottom, metrics.grid.bottom));
}

CRect CellRect(const LayoutMetrics& metrics, const StationLayoutCell& cell)
{
    const int left = metrics.grid.left + cell.column * (metrics.columnWidth + metrics.columnGap);
    const int top = metrics.grid.top + cell.row * (metrics.rowHeight + metrics.rowGap);
    return CRect(left, top, left + metrics.columnWidth, std::min<LONG>(top + metrics.rowHeight, metrics.grid.bottom));
}

COLORREF CellColor(const StationLayoutCell& cell)
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

std::wstring ShortStateText(const StationLayoutCell& cell)
{
    if (cell.missing) {
        return L"なし";
    }
    if (cell.state == L"異常検知") {
        return L"異常";
    }
    if (cell.state == L"満載") {
        return L"満";
    }
    if (cell.state == L"追加可能") {
        return L"追";
    }
    if (cell.state == L"空") {
        return L"空";
    }
    return L"";
}

std::wstring CellText(const StationLayoutCell& cell, int width)
{
    if (width < 52) {
        return cell.displayText;
    }
    const auto state = ShortStateText(cell);
    if (state.empty()) {
        return cell.displayText;
    }
    return cell.displayText + L" " + state;
}

std::vector<CPoint> ArcPoints(StationLayoutKind kind, const CRect& rect)
{
    std::vector<CPoint> points;
    points.reserve(25);

    const double pi = 3.14159265358979323846;
    const double centerX = rect.left + rect.Width() / 2.0;
    const double centerY = rect.top + rect.Height() / 2.0;
    const double radiusX = std::max(1.0, rect.Width() / 2.0 - 4.0);
    const double radiusY = std::max(1.0, rect.Height() / 2.0 - 4.0);

    double start = 0.0;
    double end = 0.0;
    switch (kind) {
    case StationLayoutKind::LeftSemiCircle:
        start = pi / 2.0;
        end = pi * 1.5;
        break;
    case StationLayoutKind::RightSemiCircle:
        start = -pi / 2.0;
        end = pi / 2.0;
        break;
    case StationLayoutKind::BottomSemiCircle:
        start = 0.0;
        end = pi;
        break;
    case StationLayoutKind::Straight:
    default:
        return points;
    }

    constexpr int kSegments = 24;
    for (int segment = 0; segment <= kSegments; ++segment) {
        const double ratio = static_cast<double>(segment) / kSegments;
        const double angle = start + (end - start) * ratio;
        const int x = static_cast<int>(centerX + radiusX * std::cos(angle));
        const int y = static_cast<int>(centerY + radiusY * std::sin(angle));
        points.emplace_back(x, y);
    }
    return points;
}

void DrawGuide(CDC& dc, const LayoutMetrics& metrics, const StationLayoutModel& model)
{
    CPen guidePen(PS_SOLID, 2, RGB(125, 135, 145));
    CPen* oldPen = dc.SelectObject(&guidePen);
    const int rowsPerColumn = std::max(1, model.rowsPerColumn);

    for (int column = 0; column < model.columnCount; ++column) {
        const auto kind = column < 0 || column * rowsPerColumn >= static_cast<int>(model.cells.size())
                              ? StationLayoutKind::Straight
                              : model.cells[static_cast<size_t>(column * rowsPerColumn)].kind;
        CRect columnRect = ColumnRect(metrics, column, rowsPerColumn);
        CRect labelRect(columnRect.left, metrics.header.top, columnRect.right, metrics.header.bottom);
        dc.DrawText(KindLabel(kind), &labelRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        CRect guideRect = columnRect;
        guideRect.DeflateRect(4, 3, 4, 3);
        if (kind == StationLayoutKind::Straight) {
            const int centerX = guideRect.CenterPoint().x;
            dc.MoveTo(centerX, guideRect.top);
            dc.LineTo(centerX, guideRect.bottom);
        } else {
            auto points = ArcPoints(kind, guideRect);
            if (points.size() > 1) {
                dc.Polyline(points.data(), static_cast<int>(points.size()));
            }
        }
    }

    dc.SelectObject(oldPen);
}

} // namespace

BEGIN_MESSAGE_MAP(CStationLayoutCtrl, CWnd)
    ON_WM_PAINT()
    ON_WM_LBUTTONDOWN()
END_MESSAGE_MAP()

/**
 * @brief Register and create the custom station layout window.
 */
BOOL CStationLayoutCtrl::Create(DWORD style, const RECT& rect, CWnd* parent, UINT id)
{
    const CString className = AfxRegisterWndClass(CS_DBLCLKS,
                                                  ::LoadCursor(nullptr, IDC_ARROW),
                                                  reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
                                                  nullptr);
    return CWnd::Create(className, L"", style | WS_CHILD, rect, parent, id);
}

/**
 * @brief Replace rendered station model and preserve explicit selection.
 */
void CStationLayoutCtrl::ApplyModel(const StationLayoutModel& model)
{
    model_ = model;
    const auto selected = std::find_if(model_.cells.begin(), model_.cells.end(), [](const StationLayoutCell& cell) {
        return cell.selected;
    });
    if (selected != model_.cells.end()) {
        selectedContainerNo_ = selected->containerNo;
    }
    ApplySelectionToModel();
    Invalidate();
}

/**
 * @brief Update selected cell without replacing model data.
 */
void CStationLayoutCtrl::SetSelectedContainer(int containerNo)
{
    selectedContainerNo_ = std::max(1, std::min(100, containerNo));
    ApplySelectionToModel();
    Invalidate();
}

/**
 * @brief Return selected container number for parent command handling.
 */
int CStationLayoutCtrl::SelectedContainerNo() const noexcept
{
    return selectedContainerNo_;
}

/**
 * @brief Paint fixed guide shape labels and individual container cells.
 */
void CStationLayoutCtrl::OnPaint()
{
    CPaintDC dc(this);
    CRect client;
    GetClientRect(&client);
    dc.FillSolidRect(client, RGB(245, 247, 250));
    dc.SetBkMode(TRANSPARENT);

    if (model_.cells.empty() || model_.columnCount <= 0 || model_.rowsPerColumn <= 0) {
        dc.DrawText(L"配置未取得", &client, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    const auto metrics = ComputeLayout(client, model_.columnCount, model_.rowsPerColumn);
    DrawGuide(dc, metrics, model_);

    CPen borderPen(PS_SOLID, 1, RGB(115, 125, 135));
    CPen selectedPen(PS_SOLID, 2, RGB(35, 105, 210));
    CPen* oldPen = dc.SelectObject(&borderPen);

    for (const auto& cell : model_.cells) {
        CRect rect = CellRect(metrics, cell);
        if (rect.IsRectEmpty()) {
            continue;
        }

        CBrush brush(CellColor(cell));
        dc.FillRect(rect, &brush);
        dc.SelectObject(cell.selected ? &selectedPen : &borderPen);
        dc.Rectangle(rect);

        rect.DeflateRect(2, 0, 2, 0);
        const auto text = CellText(cell, rect.Width());
        dc.DrawText(ToCString(text), rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    dc.SelectObject(oldPen);
}

/**
 * @brief Hit-test clicked cell, update selection, and notify parent dialog.
 */
void CStationLayoutCtrl::OnLButtonDown(UINT flags, CPoint point)
{
    CRect client;
    GetClientRect(&client);
    for (const auto& hit : BuildHitCells(client)) {
        if (hit.rect.PtInRect(point)) {
            SetSelectedContainer(hit.containerNo);
            if (CWnd* parent = GetParent()) {
                parent->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_STATION_LAYOUT, BN_CLICKED), reinterpret_cast<LPARAM>(m_hWnd));
            }
            break;
        }
    }
    CWnd::OnLButtonDown(flags, point);
}

/**
 * @brief Build hit rectangles from current layout geometry.
 */
std::vector<CStationLayoutCtrl::HitCell> CStationLayoutCtrl::BuildHitCells(const CRect& client) const
{
    std::vector<HitCell> hits;
    if (model_.cells.empty() || model_.columnCount <= 0 || model_.rowsPerColumn <= 0) {
        return hits;
    }

    const auto metrics = ComputeLayout(client, model_.columnCount, model_.rowsPerColumn);
    hits.reserve(model_.cells.size());
    for (const auto& cell : model_.cells) {
        hits.push_back({CellRect(metrics, cell), cell.containerNo});
    }
    return hits;
}

/**
 * @brief Keep model cell flags consistent with selectedContainerNo_.
 */
void CStationLayoutCtrl::ApplySelectionToModel()
{
    for (auto& cell : model_.cells) {
        cell.selected = cell.containerNo == selectedContainerNo_;
    }
}
