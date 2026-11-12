install(FILES
    clipstudiopaintcompatible.profile
    kritadefault.profile
    painttoolsaicompatible.profile
    photoshopcompatible.profile
    gigasketch.profile
    DESTINATION  ${KDE_INSTALL_DATADIR}/krita/input
)
if (WIN32)
   install(FILES
        tabletpro.profile
        DESTINATION  ${KDE_INSTALL_DATADIR}/krita/input
    )
endif()

