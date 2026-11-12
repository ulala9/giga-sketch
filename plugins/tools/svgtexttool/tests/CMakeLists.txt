########### next target ###############

kis_add_test_maybe_broken(SvgTextCursorTest.cpp
    TEST_NAME SvgTextCursorTest
    LINK_LIBRARIES krita_tool_svgtext_static kritaflake kritaui kritatestsdk
    BROKEN_IF ${WIN32})

kis_add_test(GigaFontPickerTest.cpp
    TEST_NAME GigaFontPickerTest
    LINK_LIBRARIES krita_tool_svgtext_static kritaui kritatestsdk Qt::Test)

kis_add_test(GigaTextPanelsTest.cpp
    TEST_NAME GigaTextPanelsTest
    LINK_LIBRARIES krita_tool_svgtext_static kritaui kritatestsdk Qt::Test)
