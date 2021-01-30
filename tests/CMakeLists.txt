# Les tests sont écrits à l'aide de Boost.Test. 
# On ne peut rien faire s'il n'est pas présent dans le système!
find_package(Boost REQUIRED COMPONENTS unit_test_framework)

# Ajout de l'exécutable contenant les tests
add_executable(dvcstests
    testfolderfixture.h
    testfolderfixture.cpp
    tests.cpp
)

# Indique à l'exécutable où se situe les fichiers qu'il peut inclure
target_include_directories(dvcstests 
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Liaison de l'exécutable de test avec les bibliothèques tierces requises
target_link_libraries(dvcstests
    PRIVATE
        dvcslib
		sqlite3
        Boost::unit_test_framework
		fmt::fmt
)

# Les tests ont besoin de savoir où se situent les bases de données de tests.
# Pour ce faire, on va définir le symbole DATA_PATH qui va contenir le chemin 
# d'accès vers ces bases de données. Les tests n'auront qu'à le consulter pour
# savoir où se trouvent les données de tests.
target_compile_definitions(dvcstests
	PRIVATE
		DATA_PATH="${CMAKE_CURRENT_SOURCE_DIR}/data"
)

# Ajout d'analyses statiques si les outils nécessaires sont présents
include("${CMAKE_SOURCE_DIR}/cmake/static_analysis.cmake")
target_add_static_analysis(dvcstests)

# Ajout de tous les tests se trouvant dans le répertoire courant
add_test(all dvcstests)
