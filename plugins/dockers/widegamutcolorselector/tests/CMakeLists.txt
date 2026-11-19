# Giga Sketch: Harmony Wheel color math (header-only, no plugin library needed)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)

kis_add_test(TestWGHarmony.cpp
    TEST_NAME TestWGHarmony
    LINK_LIBRARIES Qt::Gui Qt::Test)
