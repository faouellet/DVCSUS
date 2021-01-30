#pragma once

#include <filesystem>

namespace fs = std::filesystem;

////////////////////////////////////////////////////////////////////////////////////
// Fixture gérant le répertoire de test.
// Elle nous assure que toutes les opérations se font dans un répertoire de tests et
// qu'on ne leak pas de fichiers ou de répertoires après une run de tests. Sans ça,
// on ne pourrait pas garantir que l'exécution des tests se fait dans un
// environnement propre.
////////////////////////////////////////////////////////////////////////////////////
class TestFolderFixture
{
  public:
    TestFolderFixture();
    ~TestFolderFixture();

    [[nodiscard]] const fs::path &GetTestFolderPath() const;

  private:
    fs::path m_testFolderPath{}; // Path du répertoire de test
};

void CreateNonEmptyRepository();
void SetupRemoteRepository(const fs::path& remoteRepoPath);
