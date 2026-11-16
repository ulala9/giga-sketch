# Giga Sketch: Reference Board panel
set(KRITA_GIGAREFERENCEBOARD_SOURCES
    GigaReferenceBoardPlugin.cpp
    GigaReferenceBoardDock.cpp
    GigaReferenceBoardView.cpp)

# Image search browser (optional: needs Qt WebEngine, e.g. Fedora's
# qt6-qtwebengine-devel). Without it the Board simply has no Search button.
find_package(Qt${QT_MAJOR_VERSION}WebEngineWidgets QUIET)
if (Qt${QT_MAJOR_VERSION}WebEngineWidgets_FOUND)
    message(STATUS "Giga Sketch: Reference Board image search enabled (Qt WebEngine found)")
    list(APPEND KRITA_GIGAREFERENCEBOARD_SOURCES GigaImageSearchView.cpp)
    qt_add_resources(KRITA_GIGAREFERENCEBOARD_SOURCES gigaimagesearch.qrc)
else()
    message(STATUS "Giga Sketch: Reference Board image search disabled (Qt WebEngine not found)")
endif()

kis_add_library(kritagigareferenceboard MODULE ${KRITA_GIGAREFERENCEBOARD_SOURCES})
target_link_libraries(kritagigareferenceboard kritaui Qt${QT_MAJOR_VERSION}::Network Qt${QT_MAJOR_VERSION}::Concurrent)
if (Qt${QT_MAJOR_VERSION}WebEngineWidgets_FOUND)
    target_compile_definitions(kritagigareferenceboard PRIVATE GIGA_HAVE_WEBENGINE)
    target_link_libraries(kritagigareferenceboard Qt${QT_MAJOR_VERSION}::WebEngineWidgets)
endif()
install(TARGETS kritagigareferenceboard DESTINATION ${KRITA_PLUGIN_INSTALL_DIR})
