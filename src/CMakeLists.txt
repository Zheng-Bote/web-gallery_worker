cmake_minimum_required(VERSION 3.25)

# cmake -S ../src -B . -DCMAKE_BUILD_TYPE=Debug
# make -j$(nproc)



project(CrowWorker
    VERSION 0.1.0
    LANGUAGES CXX)

# --- Compiler Einstellungen (C++23) ---
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wall -Wextra)
endif()

# --- Dependencies via FetchContent ---
include(FetchContent)

# 1. Asio (Standalone)
# Crow benötigt Asio. Wir laden es separat, um Versionkonflikte zu vermeiden
# und den Include-Pfad korrekt setzen zu können.
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG        asio-1-30-2
)
FetchContent_MakeAvailable(asio)

# Variable für Crow sichtbar machen
set(ASIO_INCLUDE_DIR ${asio_SOURCE_DIR}/asio/include)

# 2. Crow (Microframework)
FetchContent_Declare(
    crow
    GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(crow)

# --- System Dependencies ---

# Qt6 (Core für Strings/Datum, Sql für DB-Treiber Logik)
find_package(Qt6 REQUIRED COMPONENTS Core Sql)

# PostgreSQL Client Library (libpq)
find_package(PostgreSQL REQUIRED)

# Exiv2 (Via PkgConfig, da Standard-CMake-Config oft fehlt)
find_package(PkgConfig REQUIRED)
pkg_check_modules(EXIV2 REQUIRED IMPORTED_TARGET exiv2)

# --- Executable Definition ---
add_executable(${PROJECT_NAME}
    main.cpp
    includes/DbManager.cpp
    includes/DbManager.h
    includes/MetadataExtractor.cpp
    includes/MetadataExtractor.h
)

# --- Include Directories ---
target_include_directories(${PROJECT_NAME} PRIVATE
    includes
    ${ASIO_INCLUDE_DIR}           # Asio Header
    ${EXIV2_INCLUDE_DIRS}         # Exiv2 Header
    ${PostgreSQL_INCLUDE_DIRS}    # Postgres Header
)

# --- Linking ---
target_link_libraries(${PROJECT_NAME} PRIVATE
    Crow::Crow
    Qt6::Core
    Qt6::Sql
    PostgreSQL::PostgreSQL
    PkgConfig::EXIV2
)

# Wichtig für Linux Deployment
set_property(TARGET ${PROJECT_NAME} PROPERTY POSITION_INDEPENDENT_CODE ON)
