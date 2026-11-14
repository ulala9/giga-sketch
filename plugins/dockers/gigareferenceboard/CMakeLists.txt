# Giga Sketch: Reference Board panel
set(KRITA_GIGAREFERENCEBOARD_SOURCES
    GigaReferenceBoardPlugin.cpp
    GigaReferenceBoardDock.cpp
    GigaReferenceBoardView.cpp)
kis_add_library(kritagigareferenceboard MODULE ${KRITA_GIGAREFERENCEBOARD_SOURCES})
target_link_libraries(kritagigareferenceboard kritaui)
install(TARGETS kritagigareferenceboard DESTINATION ${KRITA_PLUGIN_INSTALL_DIR})
