# Install icons if we're on most *nix systems.
if(UNIX AND NOT APPLE)
    set(ICON_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX}/share/icons/hicolor
        CACHE PATH "Prefix to use for installing icons.")
    # From contrib/icon: the smaller sizes are drawn for Windows, the
    # larger ones for Linux.
    foreach(size 16 24 32 48)
        install(FILES icon/Windows/${size}.png
            DESTINATION ${ICON_INSTALL_PREFIX}/${size}x${size}/apps
            RENAME freedv.png)
    endforeach(size)
    foreach(size 64 96 128 256 512)
        install(FILES icon/Linux/${size}.png
            DESTINATION ${ICON_INSTALL_PREFIX}/${size}x${size}/apps
            RENAME freedv.png)
    endforeach(size)

    set(DESKTOP_INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/share/applications
        CACHE PATH "Location to install desktop files.")
    install(FILES freedv.desktop
        DESTINATION ${DESKTOP_INSTALL_DIR})
endif(UNIX AND NOT APPLE)
