cmake_minimum_required(VERSION 3.10)
project(pwnat C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_definitions(-D_GNU_SOURCE)

# Find dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(NICE nice)
pkg_check_modules(GLIB glib-2.0)
pkg_check_modules(GOBJECT gobject-2.0)

if(NICE_FOUND)
    message(STATUS "Found libnice, enabling ICE support")
    add_definitions(-DHAVE_NICE)
    include_directories(${NICE_INCLUDE_DIRS} ${GLIB_INCLUDE_DIRS} ${GOBJECT_INCLUDE_DIRS})
    set(EXTRA_LIBS ${NICE_LIBRARIES} ${GLIB_LIBRARIES} ${GOBJECT_LIBRARIES})
else()
    message(WARNING "libnice not found, ICE support disabled")
endif()

if(WIN32)
    add_definitions(-D_WIN32)
    set(OS_SOURCES windoze.c)
    set(OS_LIBS ws2_32)
elseif(APPLE)
    add_definitions(-DAPPLE)
elseif(SOLARIS)
    add_definitions(-DSOLARIS)
    set(OS_LIBS nsl socket resolv)
else()
    add_definitions(-DLINUX)
endif()

set(SOURCES
    pwnat.c
    socket.c
    message.c
    strlcpy.c
    client.c
    packet.c
    list.c
    destination.c
    udpserver.c
    udpclient.c
    ice_transport.c
    relay.c
    ${OS_SOURCES}
)

add_executable(pwnat ${SOURCES})

target_link_libraries(pwnat ${OS_LIBS} ${EXTRA_LIBS})
