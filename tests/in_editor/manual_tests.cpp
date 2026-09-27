#include <gtest/gtest.h>
// Manual checks are visible but never counted as automatic passes.
TEST(Manual, ObjectPropertiesQuestActionEntry) {
    GTEST_SKIP() << "Open object properties and insert both quest actions through their outer entry.";
}
TEST(Manual, VisualLayout) {
    GTEST_SKIP() << "Inspect layout, text clipping and scaling in the editor.";
}
TEST(Manual, WindowFlashingAndFocus) {
    GTEST_SKIP() << "Confirm game/editor startup does not flash or steal foreground focus.";
}
