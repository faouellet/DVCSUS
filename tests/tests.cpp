#include <boost/test/tools/old/interface.hpp>
#include <filesystem>
#define BOOST_TEST_MODULE DVCSTests

#include <boost/test/unit_test.hpp>

#include "testfolderfixture.h"

#include "../dvcs/commands.h"
#include "../dvcs/paths.h"

#include <sqlite3.h>

#include <fmt/format.h>
#include <fmt/ostream.h>

#include <algorithm>
#include <concepts>
#include <fstream>

namespace
{

const fs::path TEST_DATA_PATH{DATA_PATH};

////////////////////////////////////////////////////////////////////////////////////
// Utilitaire permettant de rediriger un flux pour capturer son contenu
////////////////////////////////////////////////////////////////////////////////////
template <typename TStream>
requires std::derived_from<TStream, std::ostream> class StreamInterceptor
{
  public:
    explicit StreamInterceptor(TStream &stream) : m_originalStream{stream} { m_pOldBuffer = m_originalStream.rdbuf(m_contentStream.rdbuf()); }
    ~StreamInterceptor() { m_originalStream.rdbuf(m_pOldBuffer); }
    std::string GetStreamContent() const
    {
        auto str = m_contentStream.str();
        m_contentStream.str(std::string{});
        return str;
    }

  private:
    TStream &m_originalStream;
    mutable std::stringstream m_contentStream;
    std::streambuf *m_pOldBuffer;
};

////////////////////////////////////////////////////////////////////////////////////
// Utilitaire permettant de verrouiller un répertoire en mode lecture seule
////////////////////////////////////////////////////////////////////////////////////
class DirectoryLocker
{
  public:
    explicit DirectoryLocker(fs::path dirPath_) : m_dirPath{std::move(dirPath_)}
    {
        BOOST_REQUIRE(fs::is_directory(m_dirPath));
        m_originalPermissions = fs::status(m_dirPath).permissions();
        fs::permissions(m_dirPath, fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read);
    }
    ~DirectoryLocker() { fs::permissions(m_dirPath, m_originalPermissions); }

  private:
    fs::path m_dirPath;
    fs::perms m_originalPermissions;
};

////////////////////////////////////////////////////////////////////////////////////
// Valide que le contenu du dépôt DVCS construit dans le répertoire de test est
// en tout point identique au contenu de la base de données situé à <expectedDatabasePath>.
////////////////////////////////////////////////////////////////////////////////////
void ValidateRepositoryContents(const fs::path &expectedDatabasePath) noexcept
{
    sqlite3 *pDBHandle;
    BOOST_REQUIRE(sqlite3_open(dvcs::REPO_DB_PATH.c_str(), &pDBHandle) == SQLITE_OK);
    BOOST_REQUIRE(pDBHandle != nullptr);

    try
    {
        const auto contentQuery = fmt::format("ATTACH DATABASE \"{0}\" as Expected;"
                                              "ATTACH DATABASE \"{1}\" as Staging;"
                                              "BEGIN TRANSACTION;"
                                              "SELECT * FROM Staging.Objects	EXCEPT SELECT * FROM Expected.ExpectedStagingObjects;"
                                              "SELECT * FROM Staging.Metadata	EXCEPT SELECT * FROM Expected.ExpectedStagingMetadata;"
                                              "SELECT * FROM Objects			EXCEPT SELECT * FROM Expected.ExpectedObjects;"
                                              "SELECT * FROM Commits			EXCEPT SELECT * FROM Expected.ExpectedCommits;"
                                              "SELECT * FROM CommitsObjects		EXCEPT SELECT * FROM Expected.ExpectedCommitsObjects;"
                                              "SELECT * FROM Branches			EXCEPT SELECT * FROM Expected.ExpectedBranches;"
                                              "SELECT * FROM BranchesCommits	EXCEPT SELECT * FROM Expected.ExpectedBranchesCommits;"
                                              "END TRANSACTION;"
                                              "DETACH DATABASE Staging;"
                                              "DETACH DATABASE Expected;",
                                              fs::path{TEST_DATA_PATH / expectedDatabasePath}.c_str(), dvcs::STAGING_DB_PATH.c_str());
        char *pErrMsg = nullptr;
        auto callback = [](void *count, int argc, char **pArgv, char ** /* pErrMsg */) {
            for (int iRes = 0; iRes < argc; ++iRes)
            {
                fmt::print(std::cout, "{0}\n", pArgv[iRes]);
            }
            int *pCount = reinterpret_cast<int *>(count);
            ++(*pCount);
            return SQLITE_OK;
        };
        int nbResults{};
        const auto res = sqlite3_exec(pDBHandle, contentQuery.c_str(), callback, &nbResults, &pErrMsg);
        if (res != SQLITE_OK)
        {
            fmt::print(std::cerr, "SQLite error: {}\n", pErrMsg);
        }
        BOOST_CHECK(nbResults == 0);
        BOOST_REQUIRE(sqlite3_close(pDBHandle) == SQLITE_OK);
    }
    catch (const std::exception &e)
    {
        fmt::print(std::cerr, "{}\n", e.what());
        BOOST_REQUIRE(false);
    }
}

////////////////////////////////////////////////////////////////////////////////////
// Valide que le contenu de <interceptor> débute bien par <expected>
////////////////////////////////////////////////////////////////////////////////////
template <typename TStream>
bool StartsWith(const StreamInterceptor<TStream> &interceptor, std::string_view expected)
{
    const auto content = interceptor.GetStreamContent();
    return content.starts_with(expected);
}

} // namespace

BOOST_AUTO_TEST_SUITE(CommandsTestsSuite)

////////////////////////////////////////////////////////////////////////////////////
// Valide la création d'un dépôt
//
// Filtre: --run_test="CommandsTestsSuite/InitCommand"
////////////////////////////////////////////////////////////////////////////////////
BOOST_FIXTURE_TEST_CASE(InitCommand, TestFolderFixture)
{
    StreamInterceptor coutInterceptor{std::cout};

    try
    {
        BOOST_CHECK(dvcs::Init());

        BOOST_CHECK(fs::exists(dvcs::DVCS_PATH));
        BOOST_CHECK(fs::exists(dvcs::REPO_DB_PATH));
        BOOST_CHECK(fs::exists(dvcs::STAGING_DB_PATH));

        BOOST_CHECK(StartsWith(coutInterceptor, "initialized empty repository:"));
    }
    catch (const std::exception &e)
    {
        fmt::print(std::cerr, "{}\n", e.what());
        BOOST_REQUIRE(false);
    }
    ValidateRepositoryContents("InitTest.db");
}

////////////////////////////////////////////////////////////////////////////////////
// Valide qu'il n'est pas possible de créer un dépôt dans un répertoire où on n'a
// pas les droits d'écriture.
//
// Filtre: --run_test="CommandsTestsSuite/InitCommandFail"
////////////////////////////////////////////////////////////////////////////////////
BOOST_FIXTURE_TEST_CASE(InitCommandFail, TestFolderFixture)
{
    StreamInterceptor cerrInterceptor{std::cerr};

    try
    {
        {
            DirectoryLocker locker{GetTestFolderPath()};
            BOOST_CHECK(!dvcs::Init());
        }

        BOOST_CHECK(!fs::exists(dvcs::DVCS_PATH));
        BOOST_CHECK(!fs::exists(dvcs::REPO_DB_PATH));
        BOOST_CHECK(!fs::exists(dvcs::STAGING_DB_PATH));

        BOOST_CHECK(StartsWith(cerrInterceptor, "filesystem error: status: Permission denied"));
    }
    catch (const std::exception &e)
    {
        fmt::print(std::cerr, "{}\n", e.what());
        BOOST_REQUIRE(false);
    }
}

////////////////////////////////////////////////////////////////////////////////////
// Valide la mécanique d'ajout
//
// Filtre: --run_test="CommandsTestsSuite/AddCommand"
////////////////////////////////////////////////////////////////////////////////////
BOOST_FIXTURE_TEST_CASE(AddCommand, TestFolderFixture)
{
    BOOST_REQUIRE(dvcs::Init());

    // Ajout d'un fichier
    const auto &testFilePath{"test.txt"};
    {
        std::ofstream fileStream{testFilePath};
        BOOST_REQUIRE(fileStream);
    }

    BOOST_CHECK(dvcs::Add(testFilePath));
    ValidateRepositoryContents("AddTest.db");
}

////////////////////////////////////////////////////////////////////////////////////
// Valide qu'une tentative d'ajout d'un fichier non-existant ne fonctionnera pas.
//
// Filtre: --run_test="CommandsTestsSuite/AddCommandFailNonExisting"
////////////////////////////////////////////////////////////////////////////////////
BOOST_FIXTURE_TEST_CASE(AddCommandFailNonExisting, TestFolderFixture)
{
    StreamInterceptor cerrInterceptor{std::cerr};

    BOOST_REQUIRE(dvcs::Init());

    // Ajout d'un fichier qui n'existe pas
    BOOST_CHECK(!dvcs::Add("nope"));
    BOOST_CHECK(StartsWith(cerrInterceptor, "fatal: pathspec"));
}

////////////////////////////////////////////////////////////////////////////////////
// Valide qu'il n'est pas possible d'ajout un fichier se situant en dehors du dépôt.
//
// Filtre: --run_test="CommandsTestsSuite/AddCommandFailOutsideRepo"
////////////////////////////////////////////////////////////////////////////////////
BOOST_FIXTURE_TEST_CASE(AddCommandFailOutsideRepo, TestFolderFixture)
{
    StreamInterceptor cerrInterceptor{std::cerr};

    BOOST_REQUIRE(dvcs::Init());

    // Ajout d'un fichier en dehors du dépôt
    BOOST_CHECK(!dvcs::Add(fs::path{TEST_DATA_PATH} / fs::path{"AddTest.db"}));
    BOOST_CHECK(StartsWith(cerrInterceptor, "fatal"));
}

////////////////////////////////////////////////////////////////////////////////////
// Valide la mécanique de retrait
//
// Filtre: --run_test="CommandsTestsSuite/RevertCommand"
////////////////////////////////////////////////////////////////////////////////////
BOOST_FIXTURE_TEST_CASE(RevertCommand, TestFolderFixture)
{
    // StreamInterceptor coutInterceptor{std::cout};
    BOOST_REQUIRE(dvcs::Init());

    // Ajout d'un fichier pour de vrai
    const auto &testFilePath{"test.txt"};
    {
        std::ofstream fileStream{testFilePath};
        BOOST_REQUIRE(fileStream);
    }

    BOOST_CHECK(dvcs::Add(testFilePath));
    BOOST_CHECK(dvcs::Revert());
    ValidateRepositoryContents("InitTest.db");
}

////////////////////////////////////////////////////////////////////////////////////
// Valide la mécanique de commit
//
// Filtre: --run_test="CommandsTestsSuite/CommitCommand"
////////////////////////////////////////////////////////////////////////////////////
BOOST_FIXTURE_TEST_CASE(CommitCommand, TestFolderFixture)
{
    CreateNonEmptyRepository();
    ValidateRepositoryContents("CommitTest.db");
}

////////////////////////////////////////////////////////////////////////////////////
// Valide qu'on ne peut pas créer un commit si des informations sont manquantes.
//
// Filtre: --run_test="CommandsTestsSuite/CommitCommandFailMissingInformation"
////////////////////////////////////////////////////////////////////////////////////
BOOST_FIXTURE_TEST_CASE(CommitCommandFailMissingInformation, TestFolderFixture)
{
    StreamInterceptor cerrInterceptor{std::cerr};

    BOOST_REQUIRE(dvcs::Init());

    // Ajout d'un fichier pour de vrai
    const std::string testFilePath{"test.txt"};
    {
        std::ofstream fileStream{testFilePath};
        BOOST_REQUIRE(fileStream);
    }

    BOOST_CHECK(dvcs::Add(testFilePath));

    BOOST_CHECK(!dvcs::Commit("", "Email", "Message"));
    BOOST_CHECK(StartsWith(cerrInterceptor, "Can't commit. Missing information"));
    BOOST_CHECK(!dvcs::Commit("Author", "", "Message"));
    BOOST_CHECK(StartsWith(cerrInterceptor, "Can't commit. Missing information"));
    BOOST_CHECK(!dvcs::Commit("Author", "Email", ""));
    BOOST_CHECK(StartsWith(cerrInterceptor, "Can't commit. Missing information"));
}

////////////////////////////////////////////////////////////////////////////////////
// Valide la mécanique d'ajout d'une source de données distantes à un dépôt
//
// Filtre: --run_test="CommandsTestsSuite/SetRemoteCommand"
////////////////////////////////////////////////////////////////////////////////////
BOOST_FIXTURE_TEST_CASE(SetRemoteCommand, TestFolderFixture)
{
    // StreamInterceptor coutInterceptor{std::cout};
    BOOST_REQUIRE(dvcs::Init());
    SetupRemoteRepository(TEST_DATA_PATH / fs::path{"TestRemote.db"});
    ValidateRepositoryContents("SetRemoteTest.db");
}

////////////////////////////////////////////////////////////////////////////////////
// Valide qu'il n'est pas possible de définir un fichier qui n'est pas une base de
// données comme étant une source de données distantes
//
// Filtre: --run_test="CommandsTestsSuite/SetRemoteCommandFailNotDB"
////////////////////////////////////////////////////////////////////////////////////
BOOST_FIXTURE_TEST_CASE(SetRemoteCommandFailNotDB, TestFolderFixture)
{
    BOOST_REQUIRE(dvcs::Init());
    const std::string testFilePath{"test.txt"};
    {
        std::ofstream fileStream{testFilePath};
        BOOST_REQUIRE(fileStream);
    }
    BOOST_CHECK(dvcs::SetRemote(testFilePath));
}

////////////////////////////////////////////////////////////////////////////////////
// Valide qu'il n'est pas possible de définir un fichier qui n'existe pas
//
// Filtre: --run_test="CommandsTestsSuite/SetRemoteCommandFailNotExisting"
////////////////////////////////////////////////////////////////////////////////////
BOOST_FIXTURE_TEST_CASE(SetRemoteCommandFailNotExisting, TestFolderFixture)
{
    BOOST_REQUIRE(dvcs::Init());
    BOOST_CHECK(dvcs::SetRemote("NotAFile"));
}

////////////////////////////////////////////////////////////////////////////////////
// Valide la mécanique permettant d'aller chercher des changements sur un dépôt distant
//
// Filtre: --run_test="CommandsTestsSuite/PullCommand"
////////////////////////////////////////////////////////////////////////////////////
BOOST_FIXTURE_TEST_CASE(PullCommand, TestFolderFixture)
{
    BOOST_REQUIRE(dvcs::Init());
    SetupRemoteRepository(TEST_DATA_PATH / fs::path{"PullRemote.db"});
    BOOST_CHECK(dvcs::Pull());
    ValidateRepositoryContents("Remote.db");
}

////////////////////////////////////////////////////////////////////////////////////
// Valide qu'il n'est pas possible d'aller chercher des changements sur un dépôt
// distant si celui-ci n'a pas été spécifié au préalable
//
// Filtre: --run_test="CommandsTestsSuite/PushCommandFailNoRemote"
////////////////////////////////////////////////////////////////////////////////////
BOOST_FIXTURE_TEST_CASE(PullCommandFailNoRemote, TestFolderFixture)
{
    BOOST_REQUIRE(dvcs::Init());
    BOOST_CHECK(!dvcs::Pull());
}

////////////////////////////////////////////////////////////////////////////////////
// Valide la mécanique permettant d'envoyer des changements sur un dépôt distant
//
// Filtre: --run_test="CommandsTestsSuite/PushCommand"
////////////////////////////////////////////////////////////////////////////////////
BOOST_FIXTURE_TEST_CASE(PushCommand, TestFolderFixture)
{
    CreateNonEmptyRepository();
    const auto remoteRepoPath = TEST_DATA_PATH / fs::path{"Empty.db"};
    SetupRemoteRepository(remoteRepoPath);
    BOOST_CHECK(dvcs::Push());

    sqlite3 *pDBHandle;
    BOOST_REQUIRE(sqlite3_open(remoteRepoPath.c_str(), &pDBHandle) == SQLITE_OK);
    BOOST_REQUIRE(pDBHandle != nullptr);

    try
    {
        const auto contentQuery = fmt::format("ATTACH DATABASE \"{}\" as Expected;"
                                              "BEGIN TRANSACTION;"
                                              "SELECT * FROM Objects			EXCEPT SELECT * FROM Expected.Objects;"
                                              "SELECT * FROM Commits			EXCEPT SELECT * FROM Expected.Commits;"
                                              "SELECT * FROM CommitsObjects		EXCEPT SELECT * FROM Expected.CommitsObjects;"
                                              "SELECT * FROM Branches			EXCEPT SELECT * FROM Expected.Branches;"
                                              "SELECT * FROM BranchesCommits	EXCEPT SELECT * FROM Expected.BranchesCommits;"
                                              "END TRANSACTION;"
                                              "DETACH DATABASE Expected;",
                                              dvcs::REPO_DB_PATH.c_str());
        char *pErrMsg = nullptr;
        auto callback = [](void *count, int argc, char **pArgv, char ** /* pErrMsg */) {
            for (int iRes = 0; iRes < argc; ++iRes)
            {
                fmt::print(std::cout, "{0}\n", pArgv[iRes]);
            }
            int *pCount = reinterpret_cast<int *>(count);
            ++(*pCount);
            return SQLITE_OK;
        };
        int nbResults{};
        const auto res = sqlite3_exec(pDBHandle, contentQuery.c_str(), callback, &nbResults, &pErrMsg);
        if (res != SQLITE_OK)
        {
            fmt::print(std::cerr, "SQLite error: {}\n", pErrMsg);
        }
        BOOST_CHECK(nbResults == 0);
        BOOST_REQUIRE(sqlite3_close(pDBHandle) == SQLITE_OK);
    }
    catch (const std::exception &e)
    {
        fmt::print(std::cerr, "{}\n", e.what());
        BOOST_REQUIRE(false);
    }
}

////////////////////////////////////////////////////////////////////////////////////
// Valide qu'il n'est pas possible d'envoyer des changements sur un dépôt distant si
// celui-ci n'a pas été spécifié au préalable
//
// Filtre: --run_test="CommandsTestsSuite/PushCommandFailNoRemote"
////////////////////////////////////////////////////////////////////////////////////
BOOST_FIXTURE_TEST_CASE(PushCommandFailNoRemote, TestFolderFixture)
{
    BOOST_REQUIRE(dvcs::Init());
    BOOST_CHECK(!dvcs::Push());
}

////////////////////////////////////////////////////////////////////////////////////
// Valide la mécanique de créations de branches
//
// Filtre: --run_test="CommandsTestsSuite/CreateBranchCommand"
////////////////////////////////////////////////////////////////////////////////////
BOOST_FIXTURE_TEST_CASE(CreateBranchCommand, TestFolderFixture)
{
    // StreamInterceptor coutInterceptor{std::cout};
    CreateNonEmptyRepository();

    BOOST_CHECK(dvcs::CreateBranch("MaBranche"));
    ValidateRepositoryContents("CreateBranchTest.db");
}

////////////////////////////////////////////////////////////////////////////////////
// Valide qu'il n'est pas possible de créer une même branche deux fois.
//
// Filtre: --run_test="CommandsTestsSuite/CreateBranchCommandFailAlreadyExists"
////////////////////////////////////////////////////////////////////////////////////
BOOST_FIXTURE_TEST_CASE(CreateBranchCommandFailAlreadyExists, TestFolderFixture)
{
    StreamInterceptor cerrInterceptor{std::cerr};
    CreateNonEmptyRepository();

    BOOST_CHECK(dvcs::CreateBranch("MaBranche"));
    BOOST_CHECK(!dvcs::CreateBranch("MaBranche"));
    BOOST_CHECK(StartsWith(cerrInterceptor, "Branch 'MaBranche' already exists."));
}

////////////////////////////////////////////////////////////////////////////////////
// Valide qu'il n'est pas possible de créer une branche dans un dépôt vide
//
// Filtre: --run_test="CommandsTestsSuite/CreateBranchCommandFailEmptyRepository"
////////////////////////////////////////////////////////////////////////////////////
BOOST_FIXTURE_TEST_CASE(CreateBranchCommandFailEmptyRepository, TestFolderFixture)
{
    // StreamInterceptor coutInterceptor{std::cout};
    BOOST_CHECK(!dvcs::CreateBranch("MaBranche"));
}

////////////////////////////////////////////////////////////////////////////////////
// Valide la mécanique permettant de changer la branche active d'un dépôt
//
// Filtre: --run_test="CommandsTestsSuite/CheckoutBranchCommand"
////////////////////////////////////////////////////////////////////////////////////
BOOST_FIXTURE_TEST_CASE(CheckoutBranchCommand, TestFolderFixture)
{
    // StreamInterceptor coutInterceptor{std::cout};
    CreateNonEmptyRepository();

    BOOST_REQUIRE(dvcs::CreateBranch("MaBranche"));
    BOOST_REQUIRE(dvcs::CheckoutBranch("MaBranche"));
    ValidateRepositoryContents("CheckoutBranchTest.db");
}

////////////////////////////////////////////////////////////////////////////////////
// Valide qu'on ne peut se positionner sur une branche non-existante
//
// Filtre: --run_test="CommandsTestsSuite/CheckoutBranchCommandFailDoesntExist"
////////////////////////////////////////////////////////////////////////////////////
BOOST_FIXTURE_TEST_CASE(CheckoutBranchCommandFailDoesntExist, TestFolderFixture) { BOOST_REQUIRE(!dvcs::CheckoutBranch("MaBranche")); }

BOOST_AUTO_TEST_SUITE_END()
