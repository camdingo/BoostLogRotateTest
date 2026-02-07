cmake_minimum_required(VERSION 3.15)
project(BoostLogExample)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(Boost_ROOT /home/crich/CODE/COTS/Boost/1.84.0/install)
if(DEFINED Boost_ROOT)
    set(Boost_ROOT ${Boost_ROOT})
    set(BOOST_INCLUDEDIR ${Boost_ROOT}/include)
    set(BOOST_LIBRARYDIR ${Boost_ROOT}/lib)
    set(Boost_NO_SYSTEM_PATHS ON)
    set(Boost_NO_BOOST_CMAKE OFF)
    list(APPEND CMAKE_PREFIX_PATH ${Boost_ROOT})
endif()

message(STATUS "Boost_ROOT: ${Boost_ROOT}")
message(STATUS "BOOST_INCLUDEDIR: ${BOOST_INCLUDEDIR}")
message(STATUS "BOOST_LIBRARYDIR: ${BOOST_LIBRARYDIR}")

# For Boost 1.84+, first try CONFIG mode
find_package(Boost 1.84.0 CONFIG QUIET 
    COMPONENTS log log_setup thread filesystem atomic chrono
    PATHS ${Boost_ROOT}
    NO_DEFAULT_PATH
)

if(Boost_FOUND)
    message(STATUS "Found Boost ${Boost_VERSION} using CONFIG mode")
else()
    message(STATUS "CONFIG mode failed, trying MODULE mode")
    # Fall back to MODULE mode
    set(Boost_USE_STATIC_LIBS OFF)
    set(Boost_USE_MULTITHREADED ON)
    set(Boost_USE_STATIC_RUNTIME OFF)
    
    find_package(Boost REQUIRED 
        COMPONENTS log log_setup thread filesystem
    )
    message(STATUS "Found Boost ${Boost_VERSION} using MODULE mode")
endif()

# Create executable
add_executable(log_app main.cpp)

# Link Boost libraries
target_link_libraries(log_app 
    Boost::log 
    Boost::log_setup 
    Boost::thread 
    Boost::filesystem
)

# Try to link additional components if they exist
if(TARGET Boost::atomic)
    target_link_libraries(log_app Boost::atomic)
endif()

if(TARGET Boost::chrono)
    target_link_libraries(log_app Boost::chrono)
endif()

if(TARGET Boost::system)
    target_link_libraries(log_app Boost::system)
endif()

# Add definitions for Boost.Log
target_compile_definitions(log_app PRIVATE 
    BOOST_LOG_DYN_LINK
)

# Link pthread on Linux
find_package(Threads REQUIRED)
target_link_libraries(log_app Threads::Threads)

# Print what we found
message(STATUS "Boost include dirs: ${Boost_INCLUDE_DIRS}")
message(STATUS "Boost library dirs: ${Boost_LIBRARY_DIRS}")
message(STATUS "Boost libraries: ${Boost_LIBRARIES}")