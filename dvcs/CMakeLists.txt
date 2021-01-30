# Pour être en mesure de faire la majeure partie du travail en mémoire, les flux de la
# bibliothèque standard ne suffiront pas. Boost.IOStreams vient combler ces lacunes.
find_package(Boost REQUIRED COMPONENTS iostreams)

# Ajout de la bibliothèque implémentant les fonctionnalités du 
# système de gestion des sources.
add_library(dvcslib
    commands.h 
    commands.cpp)

# Indique à la bibliothèque où se trouve les fichiers du système de gestion des sources
target_include_directories(dvcslib
PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

# Liaison de la bibliothèque avec les bibliothèques tierces requises
target_link_libraries(dvcslib
    PRIVATE
		sqlite3	
        Boost::iostreams
        zlib
		fmt::fmt
)

# Ajout d'analyses statiques si les outils nécessaires sont présents
include("${CMAKE_SOURCE_DIR}/cmake/static_analysis.cmake")
target_add_static_analysis(dvcslib)
