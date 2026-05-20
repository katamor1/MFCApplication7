#pragma once

#include <afxwin.h>

/**
 * @brief MFC アプリケーション本体。起動時のモード切替とメイン画面起動を担う。
 */
class CMFCApplication7App final : public CWinApp
{
public:
    /**
     * @brief アプリ初期化時に Com/SelfTest/Mock 選択と MainDialog 起動を行う。
     */
    BOOL InitInstance() override;
};

extern CMFCApplication7App theApp;
