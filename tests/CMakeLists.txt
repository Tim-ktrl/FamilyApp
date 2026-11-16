cmake_minimum_required(VERSION 3.21)
project(FamilyAppModelTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Qml Test)
enable_testing()
add_executable(model_sync_tests
    model_sync_tests.cpp
    ../shoppinglistmodel.cpp ../shoppinglistmodel.h
    ../saveditemsmodel.cpp ../saveditemsmodel.h)
target_include_directories(model_sync_tests PRIVATE ..)
target_link_libraries(model_sync_tests PRIVATE Qt6::Core Qt6::Qml Qt6::Test)
add_test(NAME model_sync_tests COMMAND model_sync_tests)

add_executable(pending_write_tests
    pending_write_tests.cpp
    ../itemsynccoordinator.cpp ../itemsynccoordinator.h ../qtcallbackgate.h
    ../shoppinglistmodel.cpp ../shoppinglistmodel.h
    ../saveditemsmodel.cpp ../saveditemsmodel.h)
target_include_directories(pending_write_tests PRIVATE ..)
target_link_libraries(pending_write_tests PRIVATE Qt6::Core Qt6::Qml Qt6::Test)
add_test(NAME pending_write_tests COMMAND pending_write_tests)

add_executable(initial_sync_tests
    initial_sync_tests.cpp
    ../initialsynccoordinator.cpp ../initialsynccoordinator.h
    ../familydatavalidation.cpp ../familydatavalidation.h
    ../itemsynccoordinator.cpp ../itemsynccoordinator.h
    ../shoppinglistmodel.cpp ../shoppinglistmodel.h
    ../saveditemsmodel.cpp ../saveditemsmodel.h)
target_include_directories(initial_sync_tests PRIVATE ..)
target_link_libraries(initial_sync_tests PRIVATE Qt6::Core Qt6::Qml Qt6::Test)
add_test(NAME initial_sync_tests COMMAND initial_sync_tests)

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2)
qt_add_executable(startup_ui_tests startup_ui_tests.cpp)
set_source_files_properties(../Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
set_source_files_properties(../ShoppingListPage.qml PROPERTIES QT_RESOURCE_ALIAS ShoppingListPage.qml)
set_source_files_properties(../SavedItemsPage.qml PROPERTIES QT_RESOURCE_ALIAS SavedItemsPage.qml)
qt_add_qml_module(startup_ui_tests
    URI FamilyAppStartupTests
    NO_CACHEGEN
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES ../Main.qml ../ShoppingListPage.qml ../SavedItemsPage.qml
    SOURCES startup_ui_mocks.h
        ../shoppinglistmodel.cpp ../shoppinglistmodel.h
        ../saveditemsmodel.cpp ../saveditemsmodel.h)
target_include_directories(startup_ui_tests PRIVATE ..)
target_link_libraries(startup_ui_tests PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Test)
add_test(NAME startup_ui_tests COMMAND startup_ui_tests)

# Explicitly opt in; live network checks never run as part of ordinary CTest.
option(FAMILYAPP_BUILD_LIVE_LISTENER_TEST "Build read-only Windows Firebase listener probe" OFF)
if(FAMILYAPP_BUILD_LIVE_LISTENER_TEST)
    if(NOT WIN32)
        message(FATAL_ERROR "The live listener probe currently targets Windows")
    endif()
    find_package(Qt6 REQUIRED COMPONENTS Network)
    if(NOT DEFINED FIREBASE_CPP_SDK_DIR)
        set(FIREBASE_CPP_SDK_DIR "$ENV{FIREBASE_CPP_SDK_DIR}")
    endif()
    set(MSVC_RUNTIME_MODE MD)
    add_subdirectory("${FIREBASE_CPP_SDK_DIR}" firebase_cpp_sdk EXCLUDE_FROM_ALL)
    add_executable(firebase_listener_live firebase_listener_live.cpp
        ../firebasemanager.cpp ../firebasemanager.h ../qtcallbackgate.h
        ../initialsynccoordinator.cpp ../initialsynccoordinator.h
        ../itemsynccoordinator.cpp ../itemsynccoordinator.h
        ../familydatavalidation.cpp ../familydatavalidation.h)
    target_include_directories(firebase_listener_live PRIVATE ..)
    target_link_libraries(firebase_listener_live PRIVATE Qt6::Core Qt6::Network Qt6::Qml
        firebase_database firebase_app advapi32 ws2_32 crypt32 iphlpapi psapi userenv)
endif()
