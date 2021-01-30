# La version requise minimale de CMake est 3.12, car c'est dans cette version
# que sont apparus les fonctions de la famille FetchContent*
cmake_minimum_required(VERSION 3.12)

# Déclaration du projet
project(DVCSUS
    VERSION 1.0
    LANGUAGES C CXX
	DESCRIPTION "Système de gestion de sources distribué")

# Oui, on va tester ce projet
enable_testing()

# Exportation des commandes de compilation pour aider les outils de navigation de code
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# L'ensemble du projet nécessite un compilateur supportant C++17
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ajout de bibliothèques de tiers partis
include(FetchContent)

# Bibliothèque de formattage offrant des alternatives performantes et sécuritaires 
# à <stdio.h> et aux flux de C++. Pourra être supplanté par std::format lorsque les 
# implémentations de la bibliothèque standard le supporteront.
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt
    GIT_TAG        6.2.1
)
FetchContent_MakeAvailable(fmt)

# Base de données supportant l'implémentation du système de gestion de sources
FetchContent_Declare(
	sqlite3
	URL https://sqlite.org/2020/sqlite-amalgamation-3340000.zip
)
FetchContent_MakeAvailable(SQLite3)

# Bibliothèque de compression. Servira à réduire l'espace occupé par les données
# du système de gestion de sources dans la base de données.
FetchContent_Declare(
	zlib
	GIT_REPOSITORY https://github.com/madler/zlib
	GIT_TAG        v1.2.11
)
FetchContent_MakeAvailable(zlib)

# Ajout du répertoire des sources
add_subdirectory(dvcs)

# Ajout du répertoire des tests
add_subdirectory(tests)

# Ajout d'un client ligne de commande pour le système de gestion des sources.
add_executable(dvcsus dvcsus.cpp)

# Indique au client où se trouve les fichiers du système de gestion des sources
# qu'il peut inclure.
target_include_directories(dvcsus
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

# Liaison du client avec la bibliothèque implémentant le système de gestion des sources.
target_link_libraries(dvcsus
    PUBLIC
        dvcslib
		fmt::fmt
)
