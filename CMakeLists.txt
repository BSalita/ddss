cmake_minimum_required(VERSION 3.21)

project(dds_bridge LANGUAGES CXX)

set(DDS_THREADING "STL" CACHE STRING "DDS threading backend: NONE, STL, OPENMP, WINAPI")
set_property(CACHE DDS_THREADING PROPERTY STRINGS NONE STL OPENMP WINAPI)

option(DDS_AGGRESSIVE_OPT "Enable /Ox /GL /LTCG for MSVC (aggressive optimisation)" OFF)


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(DDS_SRC_FILES
  src/dds.cpp
  src/dump.cpp
  src/ABsearch.cpp
  src/ABstats.cpp
  src/CalcTables.cpp
  src/DealerPar.cpp
  src/File.cpp
  src/Init.cpp
  src/LaterTricks.cpp
  src/Memory.cpp
  src/Moves.cpp
  src/Par.cpp
  src/PlayAnalyser.cpp
  src/PBN.cpp
  src/QuickTricks.cpp
  src/Scheduler.cpp
  src/SolveBoard.cpp
  src/SolverIF.cpp
  src/System.cpp
  src/ThreadMgr.cpp
  src/Timer.cpp
  src/TimerGroup.cpp
  src/TimerList.cpp
  src/TimeStat.cpp
  src/TimeStatList.cpp
  src/TransTableS.cpp
  src/TransTableL.cpp
)

add_library(dds SHARED ${DDS_SRC_FILES})

target_include_directories(dds
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(MSVC)
  # Keep Windows builds warning-clean without legacy CRT warnings.
  target_compile_definitions(dds PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(DDS_THREADING STREQUAL "STL")
  target_compile_definitions(dds PRIVATE DDS_THREADS_STL)
  message(STATUS "DDS threading: STL (std::thread)")
elseif(DDS_THREADING STREQUAL "OPENMP")
  find_package(OpenMP REQUIRED)
  target_compile_definitions(dds PRIVATE DDS_THREADS_OPENMP)
  target_link_libraries(dds PRIVATE OpenMP::OpenMP_CXX)
  message(STATUS "DDS threading: OpenMP")
elseif(DDS_THREADING STREQUAL "WINAPI")
  target_compile_definitions(dds PRIVATE DDS_THREADS_WINAPI)
  message(STATUS "DDS threading: WinAPI")
else()
  target_compile_definitions(dds PRIVATE DDS_THREADS_NONE)
  message(STATUS "DDS threading: NONE (single-threaded)")
endif()

set(DTEST_FILES
  test/dtest.cpp
  test/args.cpp
  test/backend_eval.cpp
  test/oob_dds.cpp
  test/compare.cpp
  test/loop.cpp
  test/parse.cpp
  test/print.cpp
  test/testcommon.cpp
  test/TestTimer.cpp
)

add_executable(dtest ${DTEST_FILES})

target_include_directories(dtest
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/test
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# On Linux/macOS: std::thread requires pthreads; dlopen requires libdl.
if(NOT WIN32)
  find_package(Threads REQUIRED)
  target_link_libraries(dds PRIVATE Threads::Threads)
  target_link_libraries(dtest PRIVATE ${CMAKE_DL_LIBS})
endif()

target_link_libraries(dtest PRIVATE dds)

# Linux/macOS: resolve libdds.so next to dtest without LD_LIBRARY_PATH.
if(NOT WIN32)
  set_target_properties(dtest PROPERTIES BUILD_RPATH "$ORIGIN")
endif()

if(MSVC)
  target_compile_definitions(dtest PRIVATE
    _CRT_SECURE_NO_WARNINGS
    _HAS_STD_BYTE=0
  )
endif()

if(DDS_AGGRESSIVE_OPT)
  if(MSVC)
    target_compile_options(dds PRIVATE /Ox /GL /Oi /Ot)
    target_link_options(dds PRIVATE /LTCG)
    target_compile_options(dtest PRIVATE /Ox /GL /Oi /Ot)
    target_link_options(dtest PRIVATE /LTCG)
    message(STATUS "DDS aggressive optimisation: ON (/Ox /GL /Oi /Ot /LTCG)")
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(dds PRIVATE -O3 -flto -march=native)
    target_link_options(dds PRIVATE -flto)
    target_compile_options(dtest PRIVATE -O3 -flto -march=native)
    target_link_options(dtest PRIVATE -flto)
    message(STATUS "DDS aggressive optimisation: ON (-O3 -flto -march=native)")
  endif()
endif()

# NOTE: The new CalcAllTablesPBNx API handles any number of deals
# dynamically.  MAXNOOFTABLES / MAXNOOFBOARDS remain as internal
# chunk sizes and do not need to be overridden by the caller.

