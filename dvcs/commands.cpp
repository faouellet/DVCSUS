#include "commands.h"
#include "paths.h"

#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/uuid/sha1.hpp>

#include <filesystem>
#include <sqlite3.h>

#include <fmt/format.h>
#include <fmt/ostream.h>

#include <concepts>
#include <fstream>

using TDatabasePtr = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;
using TStatementPtr = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;
using TCallback = int (*)(void *, int, char **, char **);

#define RETURN_IF(cond, val)                                                                                                                         \
    if (cond)                                                                                                                                        \
    {                                                                                                                                                \
        return val;                                                                                                                                  \
    }

namespace
{

////////////////////////////////////////////////////////////////////////////////////
// Indique le sens du transfert de données à effectuer
////////////////////////////////////////////////////////////////////////////////////
enum class TransferDirection
{
    ToLocal,
    ToRemote
};

////////////////////////////////////////////////////////////////////////////////////
// Association entre des données compressées et le hash de ces données
////////////////////////////////////////////////////////////////////////////////////
struct HashedCompressedData
{
    std::string m_hash;
    std::vector<char> m_compressedData;
};

////////////////////////////////////////////////////////////////////////////////////
// Ouvre une connection <pDB> à la base de données situé à <dbPath>.
////////////////////////////////////////////////////////////////////////////////////
[[nodiscard]] bool OpenDatabaseConnection(const fs::path &dbPath, TDatabasePtr &pDB) noexcept
{
    sqlite3 *pDBHandle;
    RETURN_IF(sqlite3_open(dbPath.c_str(), &pDBHandle) != SQLITE_OK, false);
    RETURN_IF(pDBHandle == nullptr, false);
    pDB = TDatabasePtr{pDBHandle, sqlite3_close};
    return true;
}

////////////////////////////////////////////////////////////////////////////////////
// Exécute la requête <query> sur la base de données <pDB>.
// De la logique additionnelle peut être exécutée à l'aide du callback <pCallback>
// qui peut recevoir les arguments <pArg>.
// Pour plus d'informations sur la mécanique de callback, consultez:
// https://sqlite.org/c3ref/exec.html
////////////////////////////////////////////////////////////////////////////////////
[[nodiscard]] bool ExecuteQuery(TDatabasePtr &pDB, const std::string &query, TCallback pCallback = nullptr, void *pArg = nullptr) noexcept
{
    char *pErrMsg = nullptr;
    int execResult = sqlite3_exec(pDB.get(), query.c_str(), pCallback, pArg, &pErrMsg);
    const bool resultIsOK = execResult == SQLITE_OK;
    if (!resultIsOK)
    {
        fmt::print(std::cerr, "Internal error {0}: {1}\n", execResult, pErrMsg);
        return false;
    }
    return resultIsOK;
}

////////////////////////////////////////////////////////////////////////////////////
// Exécute la requête <query> sur la base de données situé à <databasePath>.
// De la logique additionnelle peut être exécutée à l'aide du callback <pCallback>
// qui peut recevoir les arguments <pArg>.
// Pour plus d'informations sur la mécanique de callback, consultez:
// https://sqlite.org/c3ref/exec.html
////////////////////////////////////////////////////////////////////////////////////
[[nodiscard]] bool ExecuteQuery(const fs::path &databasePath, const std::string &query, TCallback pCallback = nullptr, void *pArg = nullptr) noexcept
{
    RETURN_IF((pArg != nullptr) && (pCallback == nullptr), false);

    TDatabasePtr pDB{nullptr, sqlite3_close};
    try
    {
        RETURN_IF(!OpenDatabaseConnection(fs::current_path() / databasePath, pDB), false);
    }
    catch (const std::exception &e)
    {
        fmt::print(std::cerr, "{}\n", e.what());
        return false;
    }
    RETURN_IF(pDB == nullptr, false);

    return ExecuteQuery(pDB, query, pCallback, pArg);
}

////////////////////////////////////////////////////////////////////////////////////
// Fonction utilitaire permettant de valider que la requête <query> sur la base de
// données <pDB> ne produira aucun résultat.
////////////////////////////////////////////////////////////////////////////////////
[[nodiscard]] bool ValidateNoResult(TDatabasePtr &pDB, const std::string &query) noexcept
{
    int count{};
    auto countFn = [](void *pArg, int argc, char **pArgv, char ** /* pErrMsg */) {
        RETURN_IF(argc != 1, SQLITE_ERROR);
        try
        {
            int *pCount = reinterpret_cast<int *>(pArg);
            *pCount = std::stoi(pArgv[0]);
            return SQLITE_OK;
        }
        catch (const std::exception &e)
        {
            fmt::print(std::cerr, "{}\n", e.what());
            return SQLITE_ERROR;
        }
    };
    RETURN_IF(!ExecuteQuery(pDB, query, countFn, &count), false);
    return count == 0;
}

////////////////////////////////////////////////////////////////////////////////////
// Permet d'obtenir le chemin d'accès vers le dépôt distant.
////////////////////////////////////////////////////////////////////////////////////
fs::path GetRemote()
{
    fs::path remote{};
    auto callback = [](void *pArg, int argc, char **pArgv, char ** /* pErrMsg */) {
        RETURN_IF(argc != 1, SQLITE_ERROR);
        auto *pPath = reinterpret_cast<fs::path *>(pArg);
        *pPath = fs::path{pArgv[0]};
        return SQLITE_OK;
    };
    RETURN_IF(!ExecuteQuery(dvcs::STAGING_DB_PATH, "SELECT Value from Metadata WHERE Name = \"Remote\"", callback, &remote), {});
    RETURN_IF(remote.empty(), remote);
    try
    {
        const fs::path dvcsPath = fs::current_path() / dvcs::DVCS_PATH;
        return dvcsPath / remote;
    }
    catch (const std::exception &e)
    {
        fmt::print(std::cerr, "{}\n", e.what());
        return {};
    }
}

////////////////////////////////////////////////////////////////////////////////////
// Transfère vers un dépôt toutes les données d'une source qui ne s'y trouve pas.
////////////////////////////////////////////////////////////////////////////////////
[[nodiscard]] bool Transfer(TransferDirection direction) noexcept
{
    try
    {
        fs::path source;
        fs::path destination;
        switch (direction)
        {
        case TransferDirection::ToLocal:
            source = GetRemote();
            destination = fs::current_path() / dvcs::REPO_DB_PATH;
            break;
        case TransferDirection::ToRemote:
            destination = GetRemote();
            source = fs::current_path() / dvcs::REPO_DB_PATH;
            break;
        default:
            fmt::print(std::cerr, "Unsupported transfer option\n");
            return false;
        }
        RETURN_IF(source.empty(), false);
        RETURN_IF(destination.empty(), false);

        const auto query{
            fmt::format("ATTACH DATABASE \"{}\" as Source;"
                        "BEGIN TRANSACTION;"
                        "INSERT OR IGNORE INTO Objects (Hash, Path, Size, Content) SELECT Hash, Path, Size, Content FROM Source.Objects;"
                        "INSERT OR IGNORE INTO Commits (Hash, ParentHash, Author, Email, Message) SELECT Hash, ParentHash, Author, Email, "
                        "Message FROM Source.Commits;"
                        "INSERT OR IGNORE INTO CommitsObjects (ObjectHash, CommitHash) SELECT ObjectHash, CommitHash FROM "
                        "Source.CommitsObjects;"
                        "INSERT OR REPLACE INTO Branches (Name, HeadCommit) SELECT Name, HeadCommit FROM Source.Branches;"
                        "INSERT OR IGNORE INTO BranchesCommits (BranchName, CommitHash) SELECT BranchName, CommitHash FROM "
                        "Source.BranchesCommits;"
                        "END TRANSACTION;"
                        "DETACH DATABASE Source;",
                        source.string())};
        return ExecuteQuery(destination, query);
    }
    catch (const std::exception &e)
    {
        fmt::print(std::cerr, "{}\n", e.what());
        return false;
    }
}

////////////////////////////////////////////////////////////////////////////////////
// Calcul le SHA1 d'un ensemble de données brut <data>.
////////////////////////////////////////////////////////////////////////////////////
std::string ComputeSHA1(const std::vector<char> &data)
{
    boost::uuids::detail::sha1 sha1;
    sha1.process_bytes(data.data(), data.size());

    constexpr const int digestSize = 5;
    unsigned int digest[digestSize] = {0}; // NOLINT
    sha1.get_digest(digest);

    fmt::memory_buffer buf;
    for (int iHash = 0; iHash < digestSize; ++iHash) // NOLINT
    {
        fmt::format_to(buf, "{:08x}", digest[iHash]);
    }
    return fmt::to_string(buf);
}

////////////////////////////////////////////////////////////////////////////////////
// Transforme les données brutes d'un objet (accessible par le flux d'entrée
// <inputStream>) en données pouvant être stockées dans DVCSUS.
////////////////////////////////////////////////////////////////////////////////////
template <typename TStream>
requires std::derived_from<TStream, std::istream> HashedCompressedData PrepareObjectContent(TStream &inputStream)
{
    RETURN_IF(!inputStream.good(), {});

    namespace bios = boost::iostreams;
    using OutputDevice = bios::back_insert_device<std::vector<char>>;
    using OutputStream = bios::stream<OutputDevice>;

    std::vector<char> contents;
    OutputDevice device{contents};
    OutputStream objectStream{device};

    // Pour minimiser l'espace disque d'un objet, celui-ci sera
    // compressé à l'aide de zlib
    bios::filtering_streambuf<bios::input> objectCompressingStream;
    objectCompressingStream.push(bios::zlib_compressor());
    objectCompressingStream.push(inputStream);

    try
    {
        objectStream.exceptions(std::ios::badbit | std::ios::failbit);
        bios::copy(objectCompressingStream, objectStream);
    }
    catch (const std::exception &e)
    {
        fmt::print(std::cerr, "{}\n", e.what());
        return {};
    }

    // Un objet DVCS est identifé par un hash cryptographique de son contenu
    // (SHA1 pour être plus précis)
    const std::string hexdigest = ComputeSHA1(contents);

    return {hexdigest, std::move(contents)};
}

////////////////////////////////////////////////////////////////////////////////////
// Indique si <path> est contenu dans le répertoire courant ou dans un des
// sous-répertoires du répertoire courant.
// NOTE: On prend le path par copie à cause des modifications qu'on pourrait lui
//       apporter dans le cadre de la fonction.
////////////////////////////////////////////////////////////////////////////////////
bool IsContainedInCurrentDirectory(fs::path path)
{
    if (path.has_filename())
    {
        path.remove_filename();
    }

    const fs::path currentPath = fs::current_path();
    const auto currentPathLength = std::distance(currentPath.begin(), currentPath.end());
    const auto pathLength = std::distance(path.begin(), path.end());

    // Si le path reçu est plus court que le path courant, aucune chance
    // qu'il soit contenu dans le répertoire courant.
    RETURN_IF(pathLength < currentPathLength, false);

    return std::equal(currentPath.begin(), currentPath.end(), path.begin());
}

////////////////////////////////////////////////////////////////////////////////////
// Initialise le dossier dans lequel les données du dépôt seront entreposés.
////////////////////////////////////////////////////////////////////////////////////
[[nodiscard]] bool CreateDVCSFolder() noexcept
{
    try
    {
        const fs::path dvcsPath = fs::current_path() / dvcs::DVCS_PATH;
        if (fs::exists(dvcsPath))
        {
            fmt::print(std::cerr, "Repository already initialized in '{}'", fs::current_path().string());
            return false;
        }
        return fs::create_directory(dvcsPath);
    }
    catch (const std::exception &e)
    {
        fmt::print(std::cerr, "{}\n", e.what());
        return false;
    }
}

} // namespace

namespace dvcs
{

////////////////////////////////////////////////////////////////////////////////////
// Ajoute aux fichiers monitorés par le système de gestion de sources le fichier
// dont le path relatif à la racine du dépôt est <filePath>
////////////////////////////////////////////////////////////////////////////////////
[[nodiscard]] bool Add(const fs::path &filePath) noexcept
{
    try
    {
        const auto absPath{fs::absolute(filePath)};
        if (!fs::is_regular_file(absPath))
        {
            // Ceci n'est pas un fichier
            fmt::print(std::cerr, "fatal: pathspec '{}' did not match any files\n", absPath.c_str());
            return false;
        }
        if (!IsContainedInCurrentDirectory(absPath))
        {
            // Où est-ce que tu va chercher ce fichier-là?
            fmt::print(std::cerr, "fatal: '{}' is outside repository\n", absPath.c_str());
            return false;
        }

        std::ifstream fileStream{filePath.string(), std::ios::in | std::ios::binary};
        const auto objContent{PrepareObjectContent(fileStream)};
        RETURN_IF(objContent.m_hash.empty(), false);

        // HashObject a consommé le stream. On peut donc déterminer la taille des données ici
        const auto dataSize = fileStream.tellg();

        TDatabasePtr pDB{nullptr, sqlite3_close};
        RETURN_IF(!OpenDatabaseConnection(fs::current_path() / STAGING_DB_PATH, pDB), false);
        RETURN_IF(pDB == nullptr, false);

        // Le chemin d'accès stocké dans la BD doit être relatif au chemin d'accès du dépôt
        const fs::path dvcsPath = fs::current_path() / dvcs::DVCS_PATH;

        sqlite3_stmt *pSQLStmt;
        RETURN_IF(sqlite3_prepare_v2(pDB.get(), "INSERT INTO Objects VALUES(@hash, @path, @size, @content)", -1, &pSQLStmt, nullptr) != SQLITE_OK,
                  false);
        TStatementPtr pStmt{pSQLStmt, sqlite3_finalize};
        RETURN_IF(sqlite3_bind_text(pStmt.get(), 1, objContent.m_hash.c_str(), -1, SQLITE_STATIC) != SQLITE_OK, false);
        RETURN_IF(sqlite3_bind_text(pStmt.get(), 2, fs::relative(filePath, dvcsPath).c_str(), -1, SQLITE_STATIC) != SQLITE_OK, false);
        RETURN_IF(sqlite3_bind_int64(pStmt.get(), 3, static_cast<sqlite3_int64>(dataSize)) != SQLITE_OK, false);
        RETURN_IF(sqlite3_bind_blob64(pStmt.get(), 4, objContent.m_compressedData.data(),
                                      static_cast<sqlite3_uint64>(objContent.m_compressedData.size()), SQLITE_STATIC) != SQLITE_OK,
                  false);

        RETURN_IF(sqlite3_step(pStmt.get()) != SQLITE_DONE, false);
    }
    catch (const std::exception &e)
    {
        fmt::print(std::cerr, "{}\n", e.what());
        return false;
    }

    return true;
}

////////////////////////////////////////////////////////////////////////////////////
// Crée un commit avec le message <message> ayant comme auteur <author> qu'on peut
// rejoindre à l'addresse courriel <email>.
////////////////////////////////////////////////////////////////////////////////////
[[nodiscard]] bool Commit(const std::string_view author, const std::string_view email, const std::string_view message) noexcept
{
    for (const auto &arg : {author, email, message})
    {
        if (arg.empty())
        {
            fmt::print(std::cerr, "Can't commit. Missing information\n");
            return false;
        }
    }

    auto callback = [](void *pArg, int argc, char **pArgv, char ** /* pErrMsg */) {
        RETURN_IF(argc != 1, SQLITE_ERROR);
        auto *pHash = reinterpret_cast<std::string *>(pArg);
        *pHash = pArgv[0];
        return SQLITE_OK;
    };
    std::string hash;
    RETURN_IF(!ExecuteQuery(STAGING_DB_PATH, "SELECT Value FROM Metadata WHERE Name = \"CurrentCommit\";", callback, &hash), false);

    std::vector<char> commitData;
    commitData.insert(commitData.end(), author.cbegin(), author.cend());
    commitData.insert(commitData.end(), email.cbegin(), email.cend());
    commitData.insert(commitData.end(), message.cbegin(), message.cend());
    commitData.insert(commitData.end(), hash.cbegin(), hash.cend());
    const auto commitHash = ComputeSHA1(commitData);

    try
    {
        const auto stagingFullPath = fs::current_path() / STAGING_DB_PATH;
        const auto commitQuery = fmt::format(
            "ATTACH DATABASE \"{0}\" as Staging;"
            "BEGIN TRANSACTION;"
            "INSERT INTO Objects (Hash, Path, Size, Content) SELECT Hash, Path, Size, Content FROM Staging.Objects;"
            "INSERT INTO Commits (Hash, ParentHash, Author, Email, Message) SELECT \"{1}\", Value, \"{2}\", \"{3}\", \"{4}\" FROM Staging.Metadata "
            "WHERE Name = \"CurrentCommit\";"
            "INSERT INTO CommitsObjects (ObjectHash, CommitHash) SELECT Hash, \"{1}\" FROM Staging.Objects;"
            "INSERT INTO BranchesCommits (BranchName, CommitHash) SELECT Value, \"{1}\" FROM Staging.Metadata WHERE Name = \"CurrentBranch\";"
            "INSERT OR REPLACE INTO Branches (Name, HeadCommit) SELECT Value, \"{1}\" FROM Staging.Metadata WHERE Name = \"CurrentBranch\";"
            "DELETE FROM Staging.Objects;"
            "INSERT OR REPLACE INTO Staging.Metadata (Name,  Value) VALUES (\"CurrentCommit\", \"{1}\");"
            "END TRANSACTION;"
            "DETACH DATABASE Staging;",
            stagingFullPath.string(), commitHash, author, email, message);

        RETURN_IF(!ExecuteQuery(REPO_DB_PATH, commitQuery), false);
    }
    catch (const std::exception &e)
    {
        fmt::print(std::cerr, "{}\n", e.what());
        return false;
    }

    return true;
}

////////////////////////////////////////////////////////////////////////////////////
// Initialise un dépôt DVCS dans le répertoire courant
//
// Concrètement, un dépôt DVCS ressemble à ceci:
// repoPath
// | -- .dvcs
//     | -- repo.db
//     | -- staging.db
////////////////////////////////////////////////////////////////////////////////////
[[nodiscard]] bool Init() noexcept
{
    RETURN_IF(!CreateDVCSFolder(), false);
    try
    {
        // Création des bases de données contenant le repo en tant que tel
        // ainsi que la zone de staging.
        const auto initQuery{
            fmt::format("ATTACH DATABASE \"{}\" as Staging;"
                        "PRAGMA foreign_keys = ON;"
                        "BEGIN TRANSACTION;"
                        "CREATE TABLE Staging.Objects("
                        "   Hash    TEXT    NOT NULL PRIMARY KEY,"
                        "   Path    TEXT    NOT NULL,"
                        "   Size    INTEGER NOT NULL,"
                        "   Content BLOB);"
                        "CREATE TABLE Staging.Metadata("
                        "   Name   TEXT NOT NULL PRIMARY KEY CHECK(Name = \"CurrentBranch\" or Name = \"CurrentCommit\" or Name = \"Remote\"),"
                        "   Value  TEXT NOT NULL);"
                        "CREATE TABLE Objects("
                        "   Hash    TEXT    NOT NULL PRIMARY KEY,"
                        "   Path    TEXT    NOT NULL,"
                        "   Size    INTEGER NOT NULL,"
                        "   Content BLOB);"
                        "CREATE TABLE Commits("
                        "   Hash        TEXT NOT NULL PRIMARY KEY,"
                        "   ParentHash  TEXT,"
                        "   Author      TEXT NOT NULL,"
                        "   Email       TEXT NOT NULL,"
                        "   Message     TEXT NOT NULL);"
                        "CREATE TABLE CommitsObjects("
                        "   ObjectHash TEXT NOT NULL,"
                        "   CommitHash TEXT NOT NULL,"
                        "   FOREIGN KEY (ObjectHash) REFERENCES Objects(Hash),"
                        "   FOREIGN KEY (CommitHash) REFERENCES Commits(Hash));"
                        "CREATE TABLE Branches("
                        "   Name        TEXT NOT NULL PRIMARY KEY,"
                        "   HeadCommit  TEXT,"
                        "   FOREIGN KEY (HeadCommit) REFERENCES Commits(Hash));"
                        "CREATE TABLE BranchesCommits("
                        "   BranchName TEXT NOT NULL,"
                        "   CommitHash TEXT NOT NULL,"
                        "   FOREIGN KEY (BranchName) REFERENCES Branches(Name),"
                        "   FOREIGN KEY (CommitHash) REFERENCES Commits(Hash));"
                        "INSERT INTO Branches (Name) VALUES (\"default\");"
                        "INSERT INTO Staging.Metadata (Name, Value) VALUES (\"CurrentBranch\", \"default\");"
                        "INSERT INTO Staging.Metadata (Name, Value) VALUES (\"CurrentCommit\", \"0000000000000000000000000000000000000000\");"
                        "END TRANSACTION;"
                        "DETACH DATABASE Staging;",
                        (fs::current_path() / STAGING_DB_PATH).c_str())};

        RETURN_IF(!ExecuteQuery(REPO_DB_PATH, initQuery), false);
    }
    catch (const std::exception &e)
    {
        fmt::print(std::cerr, "{}\n", e.what());
        return false;
    }

    // On annonce que la job est finie
    fmt::print(std::cout, "initialized empty repository: {}\n", fs::current_path().c_str());

    return true;
}

////////////////////////////////////////////////////////////////////////////////////
// Défait tout changement non-committé.
////////////////////////////////////////////////////////////////////////////////////
[[nodiscard]] bool Revert() noexcept { return ExecuteQuery(STAGING_DB_PATH, "DELETE FROM Objects;"); }

////////////////////////////////////////////////////////////////////////////////////
// Récupère tous les nouveaux commits se trouvant dans la source de données distante.
////////////////////////////////////////////////////////////////////////////////////
[[nodiscard]] bool Pull() noexcept { return Transfer(TransferDirection::ToLocal); }

////////////////////////////////////////////////////////////////////////////////////
// Envoie tous les nouveaux commits locaux à la source de données distante.
////////////////////////////////////////////////////////////////////////////////////
[[nodiscard]] bool Push() noexcept { return Transfer(TransferDirection::ToRemote); }

////////////////////////////////////////////////////////////////////////////////////
// Indique au dépôt que sa source de données distantes se trouve à <remoteRepoPath>.
////////////////////////////////////////////////////////////////////////////////////
[[nodiscard]] bool SetRemote(const fs::path &remoteRepoPath) noexcept
{
    try
    {
        const auto dvcsPath = fs::current_path() / dvcs::DVCS_PATH;
        const auto remoteRepoRelativePath = fs::relative(remoteRepoPath, dvcsPath);
        TDatabasePtr pDB{nullptr, sqlite3_close};
        if (!OpenDatabaseConnection(remoteRepoRelativePath, pDB))
        {
            fmt::print(std::cerr, "Remote must be a DVCS database\n");
            return false;
        }
        const auto setRemoteQuery{
            fmt::format("INSERT OR REPLACE INTO Metadata (Name, Value) VALUES (\"Remote\", \"{}\");", remoteRepoRelativePath.string())};
        return ExecuteQuery(STAGING_DB_PATH, setRemoteQuery);
    }
    catch (const std::exception &e)
    {
        fmt::print(std::cerr, "{}\n", e.what());
        return false;
    }
}

////////////////////////////////////////////////////////////////////////////////////
// Ajoute une branche nommé <branchName> au dépôt
////////////////////////////////////////////////////////////////////////////////////
[[nodiscard]] bool CreateBranch(const std::string_view branchName) noexcept
{
    try
    {
        TDatabasePtr pDB{nullptr, sqlite3_close};
        RETURN_IF(!OpenDatabaseConnection(fs::current_path() / REPO_DB_PATH, pDB), false);

        if (!ValidateNoResult(pDB, fmt::format("SELECT COUNT(*) FROM Branches WHERE Name = \"{}\"", branchName)))
        {
            fmt::print(std::cerr, fmt::format("Branch '{}' already exists.\n", branchName));
            return false;
        }

        if (ValidateNoResult(pDB, "SELECT COUNT(*) FROM Commits"))
        {
            fmt::print(std::cerr, fmt::format("Can't create branch '{}' in empty repository.\n", branchName));
            return false;
        }

        const auto createQuery =
            fmt::format("ATTACH DATABASE \"{0}\" as Staging;"
                        "BEGIN TRANSACTION;"
                        "INSERT INTO Branches (Name, HeadCommit) SELECT \"{1}\", Value FROM Staging.Metadata WHERE Name = \"CurrentCommit\";"
                        "END TRANSACTION;"
                        "DETACH DATABASE Staging;",
                        (fs::current_path() / STAGING_DB_PATH).c_str(), branchName);
        return ExecuteQuery(pDB, createQuery);
    }
    catch (const std::exception &e)
    {
        fmt::print(std::cerr, "{}\n", e.what());
        return false;
    }
}

////////////////////////////////////////////////////////////////////////////////////
// Positionne DVCSUS sur la branche <branchName>
////////////////////////////////////////////////////////////////////////////////////
[[nodiscard]] bool CheckoutBranch(const std::string_view branchName) noexcept
{
    try
    {
        TDatabasePtr pDB{nullptr, sqlite3_close};
        RETURN_IF(!OpenDatabaseConnection(fs::current_path() / REPO_DB_PATH, pDB), false);
        if (ValidateNoResult(pDB, fmt::format("SELECT COUNT(*) FROM Branches WHERE Name = \"{}\"", branchName)))
        {
            fmt::print(std::cerr, fmt::format("Can't checkout branch '{}'. It doesn't exists.\n", branchName));
            return false;
        }

        RETURN_IF(!OpenDatabaseConnection(fs::current_path() / STAGING_DB_PATH, pDB), false);
        if (!ValidateNoResult(pDB, "SELECT COUNT(*) FROM Objects"))
        {
            fmt::print(std::cerr, fmt::format("Can't checkout '{}' branch. Uncommitted changes detected.\n", branchName));
            return false;
        }

        const auto checkoutQuery =
            fmt::format("ATTACH DATABASE \"{0}\" as Repo;"
                        "BEGIN TRANSACTION;"
                        "INSERT OR REPLACE INTO Metadata (Name, Value) VALUES (\"CurrentBranch\", \"{1}\");"
                        "INSERT OR REPLACE INTO Metadata (Name, Value) SELECT \"CurrentCommit\", HeadCommit FROM Repo.Branches WHERE Name = \"{1}\";"
                        "END TRANSACTION;"
                        "DETACH DATABASE Repo;",
                        (fs::current_path() / REPO_DB_PATH).c_str(), branchName);

        return ExecuteQuery(pDB, checkoutQuery);
    }
    catch (const std::exception &e)
    {
        fmt::print(std::cerr, "{}\n", e.what());
        return false;
    }
}

} // namespace dvcs
