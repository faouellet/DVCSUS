####################################################################################
## Ajoute des invocations d'outils d'analyse statique à une cible donnée.
####################################################################################
function(target_add_static_analysis target)
	# Si clang-tidy est installé, on va s'en servir pour valider le code
	find_program(CLANG_TIDY_FOUND clang-tidy)
	if(CLANG_TIDY_FOUND)
	set_target_properties(${target}
		PROPERTIES CXX_CLANG_TIDY
		"clang-tidy"
	)
	endif()

	# Si iwyu est installé, on va s'en servir pour minimiser le réseau d'inclusions
	find_program(IWYU_FOUND iwyu)
	if(IWYU_FOUND)
	set_target_properties(${target}
		PROPERTIES CXX_INCLUDE_WHAT_YOU_USE
		"iwyu"
	)
	endif()
endfunction()

