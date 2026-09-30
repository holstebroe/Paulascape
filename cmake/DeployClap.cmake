# DeployClap.cmake - Optional deployment to %CLAPTEST% or standard local CLAP user directory

if(DEFINED ENV{CLAPTEST})
    set(CLAP_INSTALL_DIR "$ENV{CLAPTEST}")
elseif(WIN32)
    set(CLAP_INSTALL_DIR "$ENV{COMMONPROGRAMFILES}/CLAP")
elseif(APPLE)
    set(CLAP_INSTALL_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/CLAP")
else()
    set(CLAP_INSTALL_DIR "$ENV{HOME}/.clap")
endif()

add_custom_target(deploy_clap
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CLAP_INSTALL_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:Paulascape>" "${CLAP_INSTALL_DIR}/Paulascape.clap"
    COMMENT "Deploying Paulascape.clap to ${CLAP_INSTALL_DIR}"
    DEPENDS Paulascape
)
