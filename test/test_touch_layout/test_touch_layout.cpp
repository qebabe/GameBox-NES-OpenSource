#include <unity.h>
#include "touch_layout.h"
#include "touch_controls.h"
#include "serial_controller.h"

void test_maps_rotation_3_portrait_raw_to_landscape_screen() {
    TouchPoint point = mapFt6336PointToScreen(0, 0, 3, 320, 240);
    TEST_ASSERT_EQUAL_INT16(319, point.x);
    TEST_ASSERT_EQUAL_INT16(0, point.y);

    point = mapFt6336PointToScreen(239, 319, 3, 320, 240);
    TEST_ASSERT_EQUAL_INT16(0, point.x);
    TEST_ASSERT_EQUAL_INT16(239, point.y);
}

void test_maps_rotation_1_as_180_degree_landscape_flip() {
    TouchPoint point = mapFt6336PointToScreen(0, 0, 1, 320, 240);
    TEST_ASSERT_EQUAL_INT16(0, point.x);
    TEST_ASSERT_EQUAL_INT16(239, point.y);

    point = mapFt6336PointToScreen(239, 319, 1, 320, 240);
    TEST_ASSERT_EQUAL_INT16(319, point.x);
    TEST_ASSERT_EQUAL_INT16(0, point.y);
}

void test_clamps_mapped_touch_to_screen_bounds() {
    TouchPoint point = mapFt6336PointToScreen(300, 400, 0, 240, 320);
    TEST_ASSERT_EQUAL_INT16(239, point.x);
    TEST_ASSERT_EQUAL_INT16(319, point.y);
}

void test_detects_points_inside_rect() {
    TouchPoint point{20, 44};
    TEST_ASSERT_TRUE(touchPointInRect(point, 20, 44, 280, 24));

    point = TouchPoint{300, 44};
    TEST_ASSERT_FALSE(touchPointInRect(point, 20, 44, 280, 24));
}

void test_builds_three_point_touch_calibration() {
    TouchCalibration calibration;
    bool ok = buildTouchCalibration(
        TouchPoint{100, 200}, TouchPoint{220, 200}, TouchPoint{100, 320},
        TouchPoint{20, 20}, TouchPoint{300, 20}, TouchPoint{20, 220},
        &calibration);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(calibration.enabled);

    TouchPoint point = applyTouchCalibration(160, 260, calibration, 320, 240);
    TEST_ASSERT_EQUAL_INT16(160, point.x);
    TEST_ASSERT_EQUAL_INT16(120, point.y);
}

void test_rejects_degenerate_touch_calibration() {
    TouchCalibration calibration;
    bool ok = buildTouchCalibration(
        TouchPoint{100, 200}, TouchPoint{100, 200}, TouchPoint{100, 320},
        TouchPoint{20, 20}, TouchPoint{300, 20}, TouchPoint{20, 220},
        &calibration);

    TEST_ASSERT_FALSE(ok);
}

void test_maps_touch_control_buttons_to_nes_mask() {
    TouchPoint points[] = {
        TouchPoint{286, 180},
        TouchPoint{66, 146},
    };

    uint8_t mask = touchControlsMaskForPoints(points, 2);
    TEST_ASSERT_BITS_HIGH(DIJI_BTN_A | DIJI_BTN_UP, mask);
    TEST_ASSERT_BITS_LOW(DIJI_BTN_B | DIJI_BTN_DOWN | DIJI_BTN_LEFT | DIJI_BTN_RIGHT, mask);
}

void test_maps_single_dpad_touch_to_diagonal_mask() {
    TouchPoint points[] = {
        TouchPoint{96, 150},
    };

    uint8_t mask = touchControlsMaskForPoints(points, 1);
    TEST_ASSERT_BITS_HIGH(DIJI_BTN_UP | DIJI_BTN_RIGHT, mask);
    TEST_ASSERT_BITS_LOW(DIJI_BTN_DOWN | DIJI_BTN_LEFT, mask);
}

void test_ignores_dpad_deadzone_center() {
    TouchPoint points[] = {
        TouchPoint{66, 180},
    };

    uint8_t mask = touchControlsMaskForPoints(points, 1);
    TEST_ASSERT_BITS_LOW(DIJI_BTN_UP | DIJI_BTN_DOWN | DIJI_BTN_LEFT | DIJI_BTN_RIGHT, mask);
}

void test_pause_hotspot_only_accepts_upper_right_non_control_area() {
    TEST_ASSERT_TRUE(touchControlsPointCanOpenPause(TouchPoint{250, 40}));
    TEST_ASSERT_FALSE(touchControlsPointCanOpenPause(TouchPoint{96, 150}));
    TEST_ASSERT_FALSE(touchControlsPointCanOpenPause(TouchPoint{286, 180}));
    TEST_ASSERT_FALSE(touchControlsPointCanOpenPause(TouchPoint{250, 150}));
}

void test_ignores_opposing_touch_directions() {
    TouchPoint points[] = {
        TouchPoint{30, 180},
        TouchPoint{98, 180},
    };

    uint8_t mask = touchControlsMaskForPoints(points, 2);
    TEST_ASSERT_BITS_LOW(DIJI_BTN_LEFT | DIJI_BTN_RIGHT, mask);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_maps_rotation_3_portrait_raw_to_landscape_screen);
    RUN_TEST(test_maps_rotation_1_as_180_degree_landscape_flip);
    RUN_TEST(test_clamps_mapped_touch_to_screen_bounds);
    RUN_TEST(test_detects_points_inside_rect);
    RUN_TEST(test_builds_three_point_touch_calibration);
    RUN_TEST(test_rejects_degenerate_touch_calibration);
    RUN_TEST(test_maps_touch_control_buttons_to_nes_mask);
    RUN_TEST(test_maps_single_dpad_touch_to_diagonal_mask);
    RUN_TEST(test_ignores_dpad_deadzone_center);
    RUN_TEST(test_pause_hotspot_only_accepts_upper_right_non_control_area);
    RUN_TEST(test_ignores_opposing_touch_directions);
    return UNITY_END();
}
