# SPDX-License-Identifier: GPL-3.0-or-later
# Keep raster outputs in the build tree. CMake regenerates missing or stale files.
find_program(RSVG_CONVERT_EXECUTABLE rsvg-convert REQUIRED)
set(COWTYPE_ICON_SVG "${CMAKE_SOURCE_DIR}/assets/icons/hicolor/scalable/apps/cowtype.svg")
set(COWTYPE_ICON_OUTPUTS)
foreach(ICON_SIZE IN ITEMS 16 22 24 32 48 64 128 256)
    set(ICON_DIRECTORY "${CMAKE_BINARY_DIR}/icons/hicolor/${ICON_SIZE}x${ICON_SIZE}/apps")
    set(ICON_PNG "${ICON_DIRECTORY}/cowtype.png")
    add_custom_command(OUTPUT "${ICON_PNG}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${ICON_DIRECTORY}"
        COMMAND "${RSVG_CONVERT_EXECUTABLE}" --width "${ICON_SIZE}" --height "${ICON_SIZE}"
            --output "${ICON_PNG}" "${COWTYPE_ICON_SVG}"
        DEPENDS "${COWTYPE_ICON_SVG}" VERBATIM)
    list(APPEND COWTYPE_ICON_OUTPUTS "${ICON_PNG}")
    install(FILES "${ICON_PNG}"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${ICON_SIZE}x${ICON_SIZE}/apps"
        COMPONENT icons)
endforeach()
add_custom_target(cowtype-icons ALL DEPENDS ${COWTYPE_ICON_OUTPUTS})
install(FILES "${COWTYPE_ICON_SVG}"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
    COMPONENT icons)
