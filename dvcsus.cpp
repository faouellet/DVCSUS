#include <dvcs/commands.h>

#include <algorithm>
#include <cassert>
#include <iostream>
#include <string_view>
#include <vector>

#include <fmt/format.h>
#include <fmt/ostream.h>

namespace
{

////////////////////////////////////////////////////////////////////////////////////
// Informations sur une commande
////////////////////////////////////////////////////////////////////////////////////
struct CommandInfo
{
    const std::string m_command;
    const std::vector<std::string> m_args;
};

// Commandes supportées
std::vector<CommandInfo> cmdInfos{
    {"help", std::vector<std::string>{}},
    {"init", std::vector<std::string>{}},
    {"add", std::vector<std::string>{"<filepath>"}},
    {"commit", std::vector<std::string>{"<author>", "<email>", "<msg>"}},
    {"set_remote", std::vector<std::string>{"<filepath>"}},
    {"push", std::vector<std::string>{}},
    {"pull", std::vector<std::string>{}},
    {"branch_create", std::vector<std::string>{"<branchname>"}},
    {"branch_checkout", std::vector<std::string>{"<branchname>"}},
};

////////////////////////////////////////////////////////////////////////////////////
// Manuel d'aide
////////////////////////////////////////////////////////////////////////////////////
void ShowHelp()
{
    std::cout << "usage: dvcsus <command> [<args>]\n\n"
              << "These are common dvcsus commands used in various situations:\n\n"
              << "help			   Shows help menu\n"
              << "init			   Creates an empty repository or reinitialize an existing one\n"
              << "add			   Adds file contents to the staging area\n"
              << "commit    	   Record changes to the repository\n"
              << "set_remote	   Sets the remote repository to pull/push changes from\n"
              << "push			   Pushes local changes to the remote repository\n"
              << "pull			   Pulls local changes to the remote repository\n"
              << "branch_create    Creates a new branch\n"
              << "branch_checkout  Checks out a given branch\n";
}

} // namespace

////////////////////////////////////////////////////////////////////////////////////
// Point d'entrée de notre petit client
////////////////////////////////////////////////////////////////////////////////////
int main(int argc, char **argv)
{
    if (argc < 2)
    {
        // L'usager ne semble pas savoir quoi faire. On va lui montrer le menu d'aide.
        ShowHelp();
        return 0;
    }

    const std::string_view command = argv[1];

    const auto commandIt =
        std::find_if(cmdInfos.cbegin(), cmdInfos.cend(), [&command](const CommandInfo &info) { return info.m_command == command; });
    if (commandIt == cmdInfos.cend())
    {
        fmt::print(std::cout, "dvcsus {} is not a command. See 'dvcsus help'.\n", command);
        return 1;
    }

    // Validation du nombre d'arguments reçus pour la commande.
    // NOTE: Les 2 premières valeurs dans argv sont le nom du programme et la commande à exécuter.
    if (argc > commandIt->m_args.size() + 2)
    {
        fmt::print("usage: dvcsus {0} {1}", commandIt->m_command, fmt::join(commandIt->m_args, " "));
        return 1;
    }

    if (command == "help")
    {
        ShowHelp();
    }
    else if (command == "init")
    {
        return dvcs::Init();
    }
    else if (command == "add")
    {
        return dvcs::Add(argv[2]);
    }
    else if (command == "commit")
    {
        return dvcs::Commit(argv[2], argv[3], argv[4]);
    }
    else if (command == "set_remote")
    {
        return dvcs::SetRemote(argv[2]);
    }
    else if (command == "push")
    {
        return dvcs::Push();
    }
    else if (command == "pull")
    {
        return dvcs::Pull();
    }
    else if (command == "branch_create")
    {
        return dvcs::CreateBranch(argv[2]);
    }
    else if (command == "branch_checkout")
    {
        return dvcs::CheckoutBranch(argv[2]);
    }
    else
    {
        assert(false);
        return 1;
    }
}