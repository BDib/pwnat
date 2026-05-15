cmake_minimum_required(VERSION 3.10)
project(pwnat C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_definitions(-D_GNU_SOURCE)

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
    ${OS_SOURCES}
)

add_executable(pwnat ${SOURCES})

if(OS_LIBS)
    target_link_libraries(pwnat ${OS_LIBS})
endif()
