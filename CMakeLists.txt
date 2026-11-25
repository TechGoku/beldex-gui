cmake_minimum_required(VERSION 3.21)

project(beldex-gui VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)          # Beldex core's public headers need C++17
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BELDEX_GUI_MOCK "Use the simulated demo engine instead of linking Beldex core" OFF)
set(BELDEX_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/beldex" CACHE PATH "Beldex core source tree")
set(BELDEX_BUILD_DIR "${BELDEX_SOURCE_DIR}/build/release" CACHE PATH "Beldex core build tree (see scripts/build-beldex-core.sh)")

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Concurrent Svg Network)
qt_standard_project_setup(REQUIRES 6.8)

# --------------------------------------------------------------------------------------------
# App
# --------------------------------------------------------------------------------------------
qt_add_executable(beldex-wallet-gui
    src/main/main.cpp
    src/main/ScreenshotTour.h
    src/qt/QRCodeImageProvider.cpp
    src/daemon/DaemonManager.cpp
    src/daemon/DaemonManager.h
    src/qt/QRCodeImageProvider.h
    src/model/TransactionHistoryModel.cpp
    src/model/TransactionHistoryModel.h
    src/libwalletqt/Amount.h
    src/libwalletqt/PendingTransaction.cpp
    src/libwalletqt/PendingTransaction.h
    src/libwalletqt/Wallet.cpp
    src/libwalletqt/Wallet.h
    src/libwalletqt/WalletManager.cpp
    src/libwalletqt/WalletManager.h
    src/engine/WalletEngine.h
    src/engine/EngineCommon.h
    external/qrcodegen/qrcodegen.cpp
)

target_include_directories(beldex-wallet-gui PRIVATE src src/libwalletqt src/model src/daemon external/qrcodegen)
target_compile_definitions(beldex-wallet-gui PRIVATE BELDEX_GUI_VERSION="${PROJECT_VERSION}"
    BELDEX_GUI_DAEMON_HINT="${BELDEX_BUILD_DIR}/bin/beldexd")   # beldexd built by scripts/build-beldex-core.sh

file(GLOB_RECURSE BELDEX_GUI_QML RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS qml/*.qml)
# QML files live under qml/; strip that prefix so they sit at the module root.
foreach(f IN LISTS BELDEX_GUI_QML)
    string(REGEX REPLACE "^qml/" "" alias "${f}")
    set_source_files_properties(${f} PROPERTIES QT_RESOURCE_ALIAS ${alias})
endforeach()
set_source_files_properties(qml/theme/Theme.qml qml/theme/App.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(beldex-wallet-gui
    URI BeldexGui
    VERSION 1.0
    QML_FILES ${BELDEX_GUI_QML}
    RESOURCES
        fonts/Michroma-Regular.ttf
        fonts/SpaceMono-Regular.ttf
        fonts/SpaceMono-Bold.ttf
        images/logo-icon.png
        images/logo-wordmark.png
        images/dot-tile.png
        images/dot-tile@2x.png
)

target_link_libraries(beldex-wallet-gui PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Concurrent Qt6::Svg Qt6::Network)

# --------------------------------------------------------------------------------------------
# Wallet engine
# --------------------------------------------------------------------------------------------
set(BELDEX_WALLET_API_HEADER "${BELDEX_SOURCE_DIR}/src/wallet/api/wallet2_api.h")

if(BELDEX_GUI_MOCK)
    message(STATUS "beldex-gui: DEMO engine (simulated wallet, no network)")
    target_sources(beldex-wallet-gui PRIVATE src/engine/MockEngine.cpp)
    target_compile_definitions(beldex-wallet-gui PRIVATE BELDEX_GUI_MOCK)

    # Still compile the real adapter (header only, not linked) so API drift in Beldex core is
    # caught even in demo builds.
    if(EXISTS "${BELDEX_WALLET_API_HEADER}")
        add_library(beldex_engine_compile_check OBJECT src/engine/BeldexEngine.cpp)
        target_include_directories(beldex_engine_compile_check PRIVATE src "${BELDEX_SOURCE_DIR}/src/wallet/api")
        target_link_libraries(beldex_engine_compile_check PRIVATE Qt6::Core)
        add_dependencies(beldex-wallet-gui beldex_engine_compile_check)
    endif()
else()
    message(STATUS "beldex-gui: Beldex core engine (${BELDEX_BUILD_DIR})")
    # NO_CMAKE_FIND_ROOT_PATH: when cross-compiling (Windows), the toolchain would otherwise re-root these paths.
    find_library(BELDEX_WALLET_MERGED NAMES wallet_merged
        PATHS "${BELDEX_BUILD_DIR}/src/wallet/api" "${BELDEX_BUILD_DIR}/lib" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    if(NOT EXISTS "${BELDEX_WALLET_API_HEADER}" OR NOT BELDEX_WALLET_MERGED)
        message(FATAL_ERROR
            "Beldex core wallet library not found.\n"
            "  1. git submodule update --init --recursive\n"
            "  2. ./scripts/build-beldex-core.sh       (builds libwallet_merged.a)\n"
            "Or configure with -DBELDEX_GUI_MOCK=ON to build the UI with the demo engine.")
    endif()
    target_sources(beldex-wallet-gui PRIVATE src/engine/BeldexEngine.cpp)
    target_include_directories(beldex-wallet-gui PRIVATE "${BELDEX_SOURCE_DIR}/src/wallet/api")
    find_package(Threads REQUIRED)
    target_link_libraries(beldex-wallet-gui PRIVATE ${BELDEX_WALLET_MERGED} Threads::Threads ${CMAKE_DL_LIBS})
    if(UNIX AND NOT APPLE)
        target_link_libraries(beldex-wallet-gui PRIVATE rt)
    endif()
    # Hardware-wallet (Ledger) code in wallet_merged needs hidapi, which Beldex builds into static-deps
    # but does not merge. Order matters: hidapi -> libusb -> libudev.
    # The hidapi library is named per OS: hidapi-libusb (Linux), hidapi (Windows, macOS).
    find_library(BELDEX_GUI_DEP_hidapi NAMES libhidapi-libusb.a libhidapi.a libhidapi-hidraw.a
        PATHS "${BELDEX_BUILD_DIR}/static-deps/lib" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    foreach(dep usb-1.0 udev)
        find_library(BELDEX_GUI_DEP_${dep} NAMES lib${dep}.a
            PATHS "${BELDEX_BUILD_DIR}/static-deps/lib" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    endforeach()
    foreach(lib BELDEX_GUI_DEP_hidapi BELDEX_GUI_DEP_usb-1.0 BELDEX_GUI_DEP_udev)
        if(${lib})
            target_link_libraries(beldex-wallet-gui PRIVATE ${${lib}})
        endif()
    endforeach()
    # Beldex's static curl auto-detects the host's zstd for content-encoding; link it (statically if possible).
    if(UNIX)
        find_library(BELDEX_GUI_ZSTD NAMES libzstd.a zstd)
    endif()
    if(BELDEX_GUI_ZSTD)
        target_link_libraries(beldex-wallet-gui PRIVATE ${BELDEX_GUI_ZSTD})
    endif()
    if(APPLE)
        target_link_libraries(beldex-wallet-gui PRIVATE "-framework IOKit" "-framework CoreFoundation" "-framework Security")
    endif()
    if(WIN32)
        target_link_libraries(beldex-wallet-gui PRIVATE ws2_32 wsock32 iphlpapi crypt32 bcrypt setupapi)
    endif()
endif()

# Engine smoke test: same engine sources and link line as the app, no UI.
#   ./build/engine_smoke [host:port]
get_target_property(_gui_sources beldex-wallet-gui SOURCES)
list(FILTER _gui_sources INCLUDE REGEX "src/engine/(Beldex|Mock)Engine\\.cpp$")
add_executable(engine_smoke tests/engine_smoke.cpp ${_gui_sources})
target_include_directories(engine_smoke PRIVATE src "${BELDEX_SOURCE_DIR}/src/wallet/api")
get_target_property(_gui_libs beldex-wallet-gui LINK_LIBRARIES)
list(FILTER _gui_libs EXCLUDE REGEX "^Qt6::(Gui|Qml|Quick|QuickControls2|Svg|Network)$")
target_link_libraries(engine_smoke PRIVATE ${_gui_libs})

# --------------------------------------------------------------------------------------------
# Platform integration: icons, version info, bundle
# --------------------------------------------------------------------------------------------
set(BELDEX_GUI_APP_ID io.beldex.WalletGui)
set(BELDEX_GUI_ICO "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/beldex-wallet.ico")
set(BELDEX_GUI_ICNS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/beldex-wallet.icns")

if(WIN32)
    # Icon + version info shown by Explorer and the taskbar.
    configure_file(packaging/windows/beldex-wallet-gui.rc.in ${CMAKE_CURRENT_BINARY_DIR}/beldex-wallet-gui.rc @ONLY)
    target_sources(beldex-wallet-gui PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/beldex-wallet-gui.rc)
endif()

if(APPLE)
    configure_file(packaging/macos/Info.plist.in ${CMAKE_CURRENT_BINARY_DIR}/Info.plist @ONLY)
    set_source_files_properties(${BELDEX_GUI_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(beldex-wallet-gui PRIVATE ${BELDEX_GUI_ICNS})
endif()

set_target_properties(beldex-wallet-gui PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_BINARY_DIR}/Info.plist
    OUTPUT_NAME beldex-wallet-gui
    INSTALL_RPATH "$ORIGIN/../lib"   # Linux: bundled Qt libraries next to the app
)

# --------------------------------------------------------------------------------------------
# Install: app, bundled Qt (Qt's deploy tool), local node, desktop integration
#   cmake --install build --prefix <dir>      then package with CPack or scripts/package.sh
# --------------------------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS beldex-wallet-gui
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Copies the Qt libraries, plugins and QML modules the app uses next to it (windeployqt on Windows,
# macdeployqt on macOS, Qt's own deployer on Linux), so packages run without a system Qt.
option(BELDEX_GUI_DEPLOY_QT "Bundle Qt into the install tree" ON)
if(BELDEX_GUI_DEPLOY_QT)
    qt_generate_deploy_qml_app_script(
        TARGET beldex-wallet-gui
        OUTPUT_SCRIPT BELDEX_GUI_DEPLOY_SCRIPT
        NO_TRANSLATIONS
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT ${BELDEX_GUI_DEPLOY_SCRIPT})
endif()

# Windows built on Linux (cross-compile): windeployqt can't run here, so bundle Qt and the MinGW runtime
# directly. Layout matches windeployqt: DLLs next to the exe, plugins/ and qml/ beside it, qt.conf to find them.
if(WIN32 AND CMAKE_CROSSCOMPILING AND BELDEX_GUI_DEPLOY_QT)
    set(_qt "${QT6_INSTALL_PREFIX}")
    file(GLOB _qt_dlls "${_qt}/bin/Qt6*.dll")
    # Leave out only developer tooling; anything a QML plugin might load stays in.
    list(FILTER _qt_dlls EXCLUDE REGEX "(Test|Designer|Help|UiTools|QmlLS|QmlDom|QmlCompiler|QmlToolingSettings)[^/]*\\.dll$")
    install(FILES ${_qt_dlls} DESTINATION ${CMAKE_INSTALL_BINDIR})
    foreach(_rt libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
        execute_process(COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=${_rt} OUTPUT_VARIABLE _p OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(NOT EXISTS "${_p}")
            file(GLOB _p "/usr/x86_64-w64-mingw32/lib/${_rt}" "${_qt}/bin/${_rt}")
        endif()
        if(_p)
            list(GET _p 0 _p)
            install(FILES "${_p}" DESTINATION ${CMAKE_INSTALL_BINDIR})
        else()
            message(WARNING "beldex-gui: MinGW runtime ${_rt} not found; the Windows package may not start")
        endif()
    endforeach()
    foreach(_dir platforms styles imageformats iconengines tls networkinformation generic)
        if(EXISTS "${_qt}/plugins/${_dir}")
            install(DIRECTORY "${_qt}/plugins/${_dir}" DESTINATION ${CMAKE_INSTALL_BINDIR}/plugins FILES_MATCHING PATTERN "*.dll" PATTERN "*d.dll" EXCLUDE)
        endif()
    endforeach()
    foreach(_mod QtQuick QtQml QtCore Qt)
        install(DIRECTORY "${_qt}/qml/${_mod}" DESTINATION ${CMAKE_INSTALL_BINDIR}/qml
            PATTERN "*.debug" EXCLUDE PATTERN "*.pdb" EXCLUDE PATTERN "designer" EXCLUDE)
    endforeach()
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/qt.conf "[Paths]\nPrefix = .\nPlugins = plugins\nQmlImports = qml\n")
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/qt.conf DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# The local node (Settings › Node › Local) ships next to the app so DaemonManager finds it.
set(BELDEX_GUI_DAEMON_BIN "${BELDEX_BUILD_DIR}/bin/beldexd${CMAKE_EXECUTABLE_SUFFIX}")
if(NOT BELDEX_GUI_MOCK AND EXISTS "${BELDEX_GUI_DAEMON_BIN}")
    if(APPLE)
        install(PROGRAMS "${BELDEX_GUI_DAEMON_BIN}" DESTINATION beldex-wallet-gui.app/Contents/MacOS)
    else()
        install(PROGRAMS "${BELDEX_GUI_DAEMON_BIN}" DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
else()
    message(STATUS "beldex-gui: beldexd not found at ${BELDEX_GUI_DAEMON_BIN}; packages will not include a local node")
endif()

if(UNIX AND NOT APPLE)
    install(FILES packaging/linux/${BELDEX_GUI_APP_ID}.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    configure_file(packaging/linux/${BELDEX_GUI_APP_ID}.metainfo.xml ${CMAKE_CURRENT_BINARY_DIR}/${BELDEX_GUI_APP_ID}.metainfo.xml @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${BELDEX_GUI_APP_ID}.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
    foreach(size 16 24 32 48 64 128 256 512)
        install(FILES packaging/icons/beldex-wallet-${size}.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
            RENAME ${BELDEX_GUI_APP_ID}.png)
    endforeach()
endif()

install(FILES LICENSE README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})

# --------------------------------------------------------------------------------------------
# Packages (cpack -G <generator> in the build dir)
#   Linux:   DEB, RPM, TGZ  (installs to /opt/beldex-wallet, desktop entry + icons in /usr/share)
#   Windows: NSIS installer, ZIP
#   macOS:   DragNDrop (.dmg)
#   AppImage: scripts/package.sh
# --------------------------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "beldex-wallet")
set(CPACK_PACKAGE_VENDOR "The Beldex Project")
set(CPACK_PACKAGE_CONTACT "The Beldex Project <https://beldex.io>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Desktop wallet for Beldex (BDX)")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://beldex.io")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Beldex Wallet")
set(CPACK_PACKAGE_EXECUTABLES "beldex-wallet-gui" "Beldex Wallet")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_STRIP_FILES ON)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    set(BELDEX_GUI_ARCH arm64)
else()
    set(BELDEX_GUI_ARCH x86_64)
endif()
string(TOLOWER "${CMAKE_SYSTEM_NAME}" _os)
set(CPACK_PACKAGE_FILE_NAME "beldex-wallet-${PROJECT_VERSION}-${_os}-${BELDEX_GUI_ARCH}")

# Linux packages: self-contained tree under /opt (it carries its own Qt, so it can't live in /usr),
# with the launcher, desktop entry and icons linked into the usual places by the post-install script.
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
# The package carries its own Qt, so dpkg-shlibdeps adds little and needs every X11 dev lib on the build host.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
# The binary needs at least the glibc it was built against (CI builds on Ubuntu 22.04 = 2.35 for wide reach).
set(BELDEX_GUI_GLIBC "2.35")
if(UNIX AND NOT APPLE)
    execute_process(COMMAND ldd --version OUTPUT_VARIABLE _ldd ERROR_QUIET)
    if(_ldd MATCHES "([0-9]+\\.[0-9]+)")
        set(BELDEX_GUI_GLIBC "${CMAKE_MATCH_1}")
    endif()
endif()
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= ${BELDEX_GUI_GLIBC}), libxcb-cursor0, libxkbcommon-x11-0, libegl1, libgl1, libfontconfig1, libdbus-1-3")
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_LICENSE "BSD-3-Clause")
set(CPACK_RPM_PACKAGE_AUTOREQ OFF)
set(CPACK_RPM_PACKAGE_REQUIRES "xcb-util-cursor, libxkbcommon-x11, mesa-libEGL, fontconfig")
if(UNIX AND NOT APPLE)
    set(CPACK_PACKAGING_INSTALL_PREFIX "/opt/beldex-wallet")
    configure_file(packaging/linux/postinst.in ${CMAKE_CURRENT_BINARY_DIR}/postinst @ONLY)
    configure_file(packaging/linux/prerm.in ${CMAKE_CURRENT_BINARY_DIR}/prerm @ONLY)
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_CURRENT_BINARY_DIR}/postinst;${CMAKE_CURRENT_BINARY_DIR}/prerm")
    set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${CMAKE_CURRENT_BINARY_DIR}/postinst")
    set(CPACK_RPM_PRE_UNINSTALL_SCRIPT_FILE "${CMAKE_CURRENT_BINARY_DIR}/prerm")
endif()

# Windows installer: Start menu + desktop shortcut, uninstaller.
set(CPACK_NSIS_MUI_ICON "${BELDEX_GUI_ICO}")
set(CPACK_NSIS_MUI_UNIICON "${BELDEX_GUI_ICO}")
set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\beldex-wallet-gui.exe")
set(CPACK_NSIS_DISPLAY_NAME "Beldex Wallet")
set(CPACK_NSIS_PACKAGE_NAME "Beldex Wallet")
set(CPACK_NSIS_URL_INFO_ABOUT "https://beldex.io")
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_CREATE_ICONS_EXTRA "CreateShortCut '$DESKTOP\\\\Beldex Wallet.lnk' '$INSTDIR\\\\bin\\\\beldex-wallet-gui.exe'")
set(CPACK_NSIS_DELETE_ICONS_EXTRA "Delete '$DESKTOP\\\\Beldex Wallet.lnk'")

# macOS disk image
set(CPACK_DMG_VOLUME_NAME "Beldex Wallet")

include(CPack)
