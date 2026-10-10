cmake_minimum_required(VERSION 3.21)

project(PocketHello VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Quick
    QuickControls2
    Network
)

qt_standard_project_setup(REQUIRES 6.5)

if(NOT DEFINED FIREBASE_CPP_SDK_DIR)
    if(DEFINED ENV{FIREBASE_CPP_SDK_DIR})
        set(FIREBASE_CPP_SDK_DIR
            "$ENV{FIREBASE_CPP_SDK_DIR}")
    else()
        message(FATAL_ERROR
            "FIREBASE_CPP_SDK_DIR is not set")
    endif()
endif()

if(MSVC)
    set(MSVC_RUNTIME_MODE "MD")
endif()

add_subdirectory(
    "${FIREBASE_CPP_SDK_DIR}"
    firebase_cpp_sdk
    EXCLUDE_FROM_ALL
)

qt_add_executable(appPocketHello
    main.cpp
)

qt_add_qml_module(appPocketHello
    URI PocketHello
    VERSION 1.0
    QML_FILES
        Main.qml
        ShoppingListPage.qml
        SavedItemsPage.qml

    SOURCES
        storagemanager.h
        storagemanager.cpp

        shoppinglistmodel.h
        shoppinglistmodel.cpp

        saveditemsmodel.h
        saveditemsmodel.cpp

        firebasemanager.h
        firebasemanager.cpp
)

set_target_properties(appPocketHello PROPERTIES
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appPocketHello PRIVATE
    Qt6::Core
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network

    firebase_database
    firebase_app
)

if(WIN32)
    target_link_libraries(appPocketHello PRIVATE
        advapi32
        ws2_32
        crypt32
        iphlpapi
        psapi
        userenv
    )
endif()