#include "testfolderfixture.h"

#include "../dvcs/commands.h"

#include <boost/test/unit_test.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>

////////////////////////////////////////////////////////////////////////////////////
// Constructeur
// S'occupe de mettre en place un répertoire vide pour les test
////////////////////////////////////////////////////////////////////////////////////
TestFolderFixture::TestFolderFixture() : m_testFolderPath{fs::current_path() / "TEST"}
{
    try
    {
        // Au cas où un test n'aurait pas terminé correctement et aurait laissé des traces derrière lui
        fs::remove_all(m_testFolderPath);

        fs::create_directory(m_testFolderPath);
        fs::current_path(m_testFolderPath);
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        // Shamefur dispray. Commit sudoku.
        std::exit(1);
    }
}

////////////////////////////////////////////////////////////////////////////////////
// Destructeur
// Efface le répertoire de test
////////////////////////////////////////////////////////////////////////////////////
TestFolderFixture::~TestFolderFixture()
{
    try
    {
        fs::current_path(m_testFolderPath.parent_path());
        fs::remove_all(m_testFolderPath);
    }
    catch (const std::exception &e)
    {
        // Pas le fun, mais pas le choix avec la façon dont (boost|std)::filesystem gêre ses erreurs
        std::cerr << e.what() << '\n';
    }
}

////////////////////////////////////////////////////////////////////////////////////
// Donne accès en lecture au chemin d'accès du répertoire de test
////////////////////////////////////////////////////////////////////////////////////
[[nodiscard]] const fs::path &TestFolderFixture::GetTestFolderPath() const { return m_testFolderPath; }

////////////////////////////////////////////////////////////////////////////////////
// Crée un dépôt non-vide.
// Utile pour tester les fonctionnalités qui ne peuvent s'exécuter sur un dépôt vide
////////////////////////////////////////////////////////////////////////////////////
void CreateNonEmptyRepository()
{
    BOOST_REQUIRE(dvcs::Init());

    const auto &testFilePath{"test.txt"};
    {
        std::ofstream fileStream{testFilePath};
        BOOST_REQUIRE(fileStream);
    }

    BOOST_REQUIRE(dvcs::Add(testFilePath));

    BOOST_CHECK(dvcs::Commit("Author", "Email", "Message"));
}

////////////////////////////////////////////////////////////////////////////////////
// Crée un dépôt prêt à interagir avec une source de données distante.
////////////////////////////////////////////////////////////////////////////////////
void SetupRemoteRepository(const fs::path &remoteRepoPath)
{
    try
    {
        fs::copy_file(remoteRepoPath, remoteRepoPath.filename());
    }
    catch (...)
    {
        // Si on ne peut pas mettre en place la source distante, ça ne vaut pas la peine d'aller plus loin.
        BOOST_REQUIRE(false);
    }
    BOOST_CHECK(dvcs::SetRemote(remoteRepoPath.filename()));
}
