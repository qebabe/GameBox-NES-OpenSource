// ================ 游戏列表 ================
void drawMainMenu() {
    int maxScrollY = menuMaxScrollY();
    if (libraryScreen.scrollPosition < 0.0f) libraryScreen.scrollPosition = 0.0f;
    if (libraryScreen.scrollPosition > maxScrollY) libraryScreen.scrollPosition = maxScrollY;
    libraryScreen.scrollVelocity = 0.0f;
    libraryScreen.backPressedMs = 0;
    libraryScreen.backLongHandled = false;
    libraryScreen.scrollLastAnimationMs = millis();
    tft.fillScreen(MENU_BG_COLOR);

    // ===== 头部标题 =====
    tft.fillRect(0, 0, 320, 36, MENU_HEADER_COLOR);
    tft.drawRect(0, 0, 320, 36, MENU_BORDER_COLOR);

    drawMenuHeaderLogo();
    tft.drawRoundRect(104, 1, 112, 34, 6, MENU_BORDER_COLOR);
    drawGamepadStatusBadges(true);

    // 装饰线
    tft.drawFastHLine(20, 34, 280, MENU_ACCENT_COLOR);

    // ===== 游戏列表区域 =====
    drawSelectedRomCover();

    if (romList.empty()) {
        tft.setTextColor(MENU_HINT_COLOR);
        tft.setTextSize(1);
        tft.setFont(&fonts::efontCN_14);
        if (!sdCardAvailable && !builtinStorageAvailable) {
            tft.setCursor(138, MENU_LIST_Y + 22);
            tft.print("未检测到存储");
            tft.setCursor(128, MENU_LIST_Y + 44);
            tft.print("请插入 SD 卡");
            tft.setCursor(126, MENU_LIST_Y + 66);
            tft.print("或刷入内置游戏");
            tft.setCursor(138, MENU_LIST_Y + 100);
            tft.setTextColor(MENU_ARROW_COLOR);
            tft.print("按 A 键重试");
        } else {
            tft.setCursor(138, MENU_LIST_Y + 44);
            tft.print("没有找到游戏");
            tft.setCursor(130, MENU_LIST_Y + 68);
            tft.print("请添加 .nes 文件");
        }
        tft.setFont(&fonts::Font0);
    } else {
        drawMenuRows();
    }

    // ===== 底部操作提示 =====
    drawMenuSoftbar();
}
