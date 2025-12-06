cmake_minimum_required(VERSION 3.10)
project(WindowsProxyServer)

set(CMAKE_CXX_STANDARD 17)

if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0601)
    # Use static runtime library to avoid missing DLLs on target machines
    if(MSVC)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        foreach(flag_var
            CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
            CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
            if(${flag_var} MATCHES "/MD")
                string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
            endif()
        endforeach()
    endif()
endif()

include_directories(src)

file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.h")

# Add resource file for icon
if(WIN32)
    set(SOURCES ${SOURCES} src/resource.rc)
endif()

add_executable(ProxyServer ${SOURCES})

if(WIN32)
    target_link_libraries(ProxyServer ws2_32 mswsock)
endif()

# Add C# GUI project
include_external_msproject(ProxyGui src/gui/ProxyGui.csproj)
