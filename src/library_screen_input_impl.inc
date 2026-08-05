// ================ 菜单输入处理 ================
static void drawMenuRowsForScroll(bool force = false) {
    uint32_t now = millis();
    if (!force && (uint32_t)(now - libraryScreen.scrollLastDrawMs) < 33) return;
    libraryScreen.scrollLastDrawMs = now;
    drawMenuRows();
}

static void updateMenuScrollAnimation() {
    uint32_t now = millis();
    if (libraryScreen.scrollLastAnimationMs == 0) libraryScreen.scrollLastAnimationMs = now;
    float dt = (now - libraryScreen.scrollLastAnimationMs) / 1000.0f;
    if (dt > 0.05f) dt = 0.05f;
    libraryScreen.scrollLastAnimationMs = now;
    if (libraryScreen.touchTracking || dt <= 0.0f) return;

    bool moved = false;
    if (fabsf(libraryScreen.scrollVelocity) > 4.0f) {
        libraryScreen.scrollPosition += libraryScreen.scrollVelocity * dt;
        libraryScreen.scrollVelocity *= powf(0.90f, dt * 60.0f);
        moved = true;
    } else {
        libraryScreen.scrollVelocity = 0.0f;
    }

    int maxScrollY = menuMaxScrollY();
    if (libraryScreen.scrollPosition < 0.0f) {
        libraryScreen.scrollPosition += (0.0f - libraryScreen.scrollPosition) * fminf(1.0f, dt * 18.0f);
        libraryScreen.scrollVelocity *= 0.55f;
        if (fabsf(libraryScreen.scrollPosition) < 0.5f && fabsf(libraryScreen.scrollVelocity) < 5.0f) {
            libraryScreen.scrollPosition = 0.0f;
            libraryScreen.scrollVelocity = 0.0f;
        }
        moved = true;
    } else if (libraryScreen.scrollPosition > maxScrollY) {
        libraryScreen.scrollPosition += (maxScrollY - libraryScreen.scrollPosition) * fminf(1.0f, dt * 18.0f);
        libraryScreen.scrollVelocity *= 0.55f;
        if (fabsf(libraryScreen.scrollPosition - maxScrollY) < 0.5f &&
            fabsf(libraryScreen.scrollVelocity) < 5.0f) {
            libraryScreen.scrollPosition = maxScrollY;
            libraryScreen.scrollVelocity = 0.0f;
        }
        moved = true;
    }
    if (moved) drawMenuRowsForScroll();
}

static bool handleMenuTouch() {
    if (touchInputJustPressed()) {
        TouchPoint point = touchInputPoint();
        libraryScreen.touchTracking = touchPointInRect(point, MENU_LIST_X, MENU_LIST_Y,
                                                 MENU_LIST_WIDTH, MENU_LIST_HEIGHT);
        if (libraryScreen.touchTracking) {
            libraryScreen.touchStart = point;
            libraryScreen.touchStartMs = millis();
            libraryScreen.longPressCanceled = false;
            libraryScreen.touchMoved = fabsf(libraryScreen.scrollVelocity) > 30.0f ||
                                 libraryScreen.scrollPosition < 0.0f ||
                                 libraryScreen.scrollPosition > menuMaxScrollY();
            libraryScreen.touchLastY = point.y;
            libraryScreen.touchStartScroll = libraryScreen.scrollPosition;
            libraryScreen.touchLastSampleMs = millis();
            libraryScreen.scrollVelocity = 0.0f;
            int contentY = (int)libraryScreen.scrollPosition + point.y - MENU_LIST_Y;
            libraryScreen.touchStartIndex = contentY / MENU_ITEM_HEIGHT;
        } else {
            libraryScreen.touchStartIndex = -1;
            libraryScreen.longPressCanceled = false;
        }
    }

    if (libraryScreen.touchTracking && touchInputTouched()) {
        TouchPoint point = touchInputPoint();
        int dy = point.y - libraryScreen.touchStart.y;
        int dx = point.x - libraryScreen.touchStart.x;
        if (abs(dy) > MENU_DELETE_MOVE_TOLERANCE ||
            abs(dx) > MENU_DELETE_MOVE_TOLERANCE) {
            libraryScreen.longPressCanceled = true;
            libraryScreen.touchMoved = true;
        }

        int maxScrollY = menuMaxScrollY();
        float target = libraryScreen.touchStartScroll - dy;
        if (target < 0.0f) target *= 0.35f;
        else if (target > maxScrollY) target = maxScrollY + (target - maxScrollY) * 0.35f;
        uint32_t now = millis();
        uint32_t sampleDt = now - libraryScreen.touchLastSampleMs;
        if (sampleDt > 0) {
            float instant = (float)(libraryScreen.touchLastY - point.y) * 1000.0f / sampleDt;
            libraryScreen.scrollVelocity = libraryScreen.scrollVelocity * 0.55f + instant * 0.45f;
            if (libraryScreen.scrollVelocity > 1200.0f) libraryScreen.scrollVelocity = 1200.0f;
            if (libraryScreen.scrollVelocity < -1200.0f) libraryScreen.scrollVelocity = -1200.0f;
            libraryScreen.touchLastY = point.y;
            libraryScreen.touchLastSampleMs = now;
        }
        if (fabsf(target - libraryScreen.scrollPosition) >= 0.5f) {
            libraryScreen.scrollPosition = target;
            drawMenuRowsForScroll();
        }
        if (!libraryScreen.longPressCanceled &&
            (uint32_t)(millis() - libraryScreen.touchStartMs) >= MENU_DELETE_LONG_PRESS_MS &&
            libraryScreen.touchStartIndex >= 0 &&
            libraryScreen.touchStartIndex < (int)romList.size()) {
            libraryScreen.touchTracking = false;
            libraryScreen.selectedIndex = libraryScreen.touchStartIndex;
            libraryScreen.scrollVelocity = 0.0f;
            deleteSelectedRom();
            return true;
        }
        return true;
    }

    if (!touchInputJustReleased()) {
        return false;
    }

    TouchPoint point = touchInputReleasePoint();
    if (touchPointInRect(point, 100, 0, 120, 38)) {
        returnToHomeClock();
        return true;
    }
    if (libraryScreen.touchTracking) {
        libraryScreen.touchTracking = false;
        libraryScreen.scrollLastAnimationMs = millis();
        if (libraryScreen.touchMoved) {
            return true;
        }
        int bounded = (int)libraryScreen.scrollPosition;
        if (bounded < 0) bounded = 0;
        int maxScrollY = menuMaxScrollY();
        if (bounded > maxScrollY) bounded = maxScrollY;
        int romIndex = (bounded + point.y - MENU_LIST_Y) / MENU_ITEM_HEIGHT;
        if (romIndex >= 0 && romIndex < (int)romList.size()) {
            if (romIndex == libraryScreen.selectedIndex) {
                if (loadSelectedROM()) screenController.show(AppScreen::Playing);
            } else {
                libraryScreen.selectedIndex = romIndex;
                drawSelectedRomCover();
                drawMenuRowsForScroll(true);
            }
        }
        return true;
    }

    if (touchPointInRect(point, MENU_P1_BADGE_HIT_X, MENU_GAMEPAD_BADGE_HIT_Y,
                         MENU_GAMEPAD_BADGE_HIT_W, MENU_GAMEPAD_BADGE_HIT_H) ||
        touchPointInRect(point, MENU_P2_BADGE_HIT_X, MENU_GAMEPAD_BADGE_HIT_Y,
                         MENU_GAMEPAD_BADGE_HIT_W, MENU_GAMEPAD_BADGE_HIT_H)) {
        runGamepadPairingMenu();
        drawMainMenu();
        return true;
    }

    if (touchPointInRect(point, 0, MENU_SOFTBAR_Y,
                         MENU_SOFTBAR_ITEM_WIDTH, MENU_SOFTBAR_HEIGHT)) {
        libraryScreen.softbarFocused = true;
        libraryScreen.softbarIndex = 0;
        returnToHomeClock();
        return true;
    }

    if (touchPointInRect(point, MENU_SOFTBAR_ITEM_WIDTH, MENU_SOFTBAR_Y,
                         MENU_SOFTBAR_ITEM_WIDTH, MENU_SOFTBAR_HEIGHT)) {
        libraryScreen.softbarFocused = true;
        libraryScreen.softbarIndex = 1;
        runSettingsMenu();
        drawMainMenu();
        return true;
    }

    if (touchPointInRect(point, MENU_SOFTBAR_ITEM_WIDTH * 2, MENU_SOFTBAR_Y,
                         MENU_SOFTBAR_ITEM_WIDTH, MENU_SOFTBAR_HEIGHT)) {
        libraryScreen.softbarFocused = true;
        libraryScreen.softbarIndex = 2;
        runGameStoreMenu();
        drawMainMenu();
        return true;
    }

    if (touchPointInRect(point, MENU_SOFTBAR_ITEM_WIDTH * 4, MENU_SOFTBAR_Y,
                         MENU_SOFTBAR_ITEM_WIDTH, MENU_SOFTBAR_HEIGHT)) {
        libraryScreen.softbarFocused = true;
        libraryScreen.softbarIndex = 4;
        runPowerSettingsMenu();
        drawMainMenu();
        return true;
    }

    if (romList.empty()) {
        if (touchPointInRect(point, MENU_SOFTBAR_ITEM_WIDTH * 3, MENU_SOFTBAR_Y,
                             MENU_SOFTBAR_ITEM_WIDTH, MENU_SOFTBAR_HEIGHT)) {
            DIJI_SD.end();
            delay(100);
            tryInitSD();
            scanROMFiles();
            drawMainMenu();
            return true;
        }
        return false;
    }

    if (touchPointInRect(point, MENU_SOFTBAR_ITEM_WIDTH * 3, MENU_SOFTBAR_Y,
                         MENU_SOFTBAR_ITEM_WIDTH, MENU_SOFTBAR_HEIGHT)) {
        libraryScreen.softbarFocused = true;
        libraryScreen.softbarIndex = 3;
        if (loadSelectedROM()) {
            screenController.show(AppScreen::Playing);
        }
        return true;
    }

    if (touchPointInRect(point, MENU_COVER_X - 8, MENU_COVER_Y - 8,
                         MENU_COVER_W + 16, MENU_COVER_H + 16)) {
        if (loadSelectedROM()) {
            screenController.show(AppScreen::Playing);
        }
        return true;
    }

    return false;
}

static bool activateLibrarySoftbarItem() {
    switch (libraryScreen.softbarIndex) {
        case 0:
            returnToHomeClock();
            return true;
        case 1:
            runSettingsMenu();
            drawMainMenu();
            return true;
        case 2:
            runGameStoreMenu();
            drawMainMenu();
            return true;
        case 3:
            if (romList.empty()) {
                DIJI_SD.end();
                delay(100);
                tryInitSD();
                scanROMFiles();
                drawMainMenu();
                return true;
            }
            if (loadSelectedROM()) screenController.show(AppScreen::Playing);
            return true;
        case 4:
            runPowerSettingsMenu();
            drawMainMenu();
            return true;
        default:
            return false;
    }
}

void handleMenuInput() {
    touchInputUpdate();
    unsigned long now = millis();
    updateButtons();
    handlePendingCommands();
    if (handleMenuTouch()) {
        lastButtonTime = now;
        return;
    }
    updateMenuScrollAnimation();
    if (buttons.B) {
        if (libraryScreen.backPressedMs == 0) {
            libraryScreen.backPressedMs = now;
            libraryScreen.backLongHandled = false;
        } else if (!libraryScreen.backLongHandled &&
                   !libraryScreen.softbarFocused &&
                   (uint32_t)(now - libraryScreen.backPressedMs) >=
                       MENU_DELETE_LONG_PRESS_MS) {
            libraryScreen.backLongHandled = true;
            waitForButtonsReleased(DIJI_BTN_B);
            deleteSelectedRom();
        }
        return;
    }
    if (libraryScreen.backPressedMs != 0) {
        bool wasLongHandled = libraryScreen.backLongHandled;
        libraryScreen.backPressedMs = 0;
        libraryScreen.backLongHandled = false;
        if (!wasLongHandled) {
            if (libraryScreen.softbarFocused) {
                libraryScreen.softbarFocused = false;
                drawMenuSoftbar();
            } else {
                returnToHomeClock();
            }
        }
        lastButtonTime = now;
        return;
    }

    if (now - lastButtonTime < BUTTON_DEBOUNCE) return;

    if (buttons.SELECT) {
        waitForButtonsReleased(DIJI_BTN_SELECT);
        libraryScreen.softbarFocused = !libraryScreen.softbarFocused;
        drawMenuSoftbar();
        lastButtonTime = millis();
        return;
    }

    if (libraryScreen.softbarFocused) {
        if (buttons.LEFT && libraryScreen.softbarIndex > 0) {
            libraryScreen.softbarIndex--;
            drawMenuSoftbar();
            lastButtonTime = now;
        } else if (buttons.RIGHT && libraryScreen.softbarIndex < 4) {
            libraryScreen.softbarIndex++;
            drawMenuSoftbar();
            lastButtonTime = now;
        } else if (buttons.UP) {
            libraryScreen.softbarFocused = false;
            drawMenuSoftbar();
            lastButtonTime = now;
        } else if (buttons.A || buttons.START) {
            waitForButtonsReleased(DIJI_BTN_A | DIJI_BTN_START);
            activateLibrarySoftbarItem();
            lastButtonTime = millis();
        }
        return;
    }

    if (romList.empty()) {
        // 无 ROM 或无 SD 卡时，A 键重试 SD 初始化
        if (buttons.A) {
            lastButtonTime = now;
            DIJI_SD.end();
            delay(100);
            tryInitSD();
            scanROMFiles();
            drawMainMenu();
        }
        return;
    }

    bool buttonPressed = false;

    // 上移选择
    if (buttons.UP) {
        if (moveMenuSelection(-1)) {
            buttonPressed = true;
        }
    }

    // 下移选择
    if (buttons.DOWN) {
        if (moveMenuSelection(1)) {
            buttonPressed = true;
        }
    }

    // 确认选择
    if (buttons.START || buttons.A) {
        waitForButtonsReleased(DIJI_BTN_A | DIJI_BTN_START);
        if (loadSelectedROM()) {
            screenController.show(AppScreen::Playing);
        }
        buttonPressed = true;
    }

    if (buttonPressed) {
        lastButtonTime = now;
        // 使用局部刷新减少闪烁
        drawMenuList();
    }
}

// ================ 绘制菜单列表区域（局部刷新） ================
void drawMenuList() {
    if (romList.empty()) return;

    drawSelectedRomCover();
    drawMenuRowsForScroll(true);
}
