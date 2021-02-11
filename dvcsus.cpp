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
    const std::string_view m_command;
    const std::vector<std::string> m_args;
};

// Commandes supportées
const std::string HELP_COMMAND{"help"};
const std::string INIT_COMMAND{"init"};
const std::string ADD_COMMAND{"add"};
const std::string COMMIT_COMMAND{"commit"};
const std::string SET_REMOTE_COMMAND{"set_remote"};
const std::string PUSH_COMMAND{"push"};
const std::string PULL_COMMAND{"pull"};
const std::string BRANCH_CREATE_COMMAND{"branch_create"};
const std::string BRANCH_CHECKOUT_COMMAND{"branch_checkout"};

// Informations sur les commandes supportées
std::vector<CommandInfo> cmdInfos{
    {HELP_COMMAND, std::vector<std::string>{}},
    {INIT_COMMAND, std::vector<std::string>{}},
    {ADD_COMMAND, std::vector<std::string>{"<filepath>"}},
    {COMMIT_COMMAND, std::vector<std::string>{"<author>", "<email>", "<msg>"}},
    {SET_REMOTE_COMMAND, std::vector<std::string>{"<filepath>"}},
    {PUSH_COMMAND, std::vector<std::string>{}},
    {PULL_COMMAND, std::vector<std::string>{}},
    {BRANCH_CREATE_COMMAND, std::vector<std::string>{"<branchname>"}},
    {BRANCH_CHECKOUT_COMMAND, std::vector<std::string>{"<branchname>"}},
};

////////////////////////////////////////////////////////////////////////////////////
// Manuel d'aide
////////////////////////////////////////////////////////////////////////////////////
void ShowHelp()
{
    fmt::print(std::cout, "usage: dvcsus <command> [<args>]\n\n"
                          "These are common dvcsus commands used in various situations:\n\n"
                          "help             Shows help menu\n"
                          "init             Creates an empty repository or reinitialize an existing one\n"
                          "add              Adds file contents to the staging area\n"
                          "commit           Record changes to the repository\n"
                          "set_remote       Sets the remote repository to pull/push changes from\n"
                          "push             Pushes local changes to the remote repository\n"
                          "pull             Pulls local changes to the remote repository\n"
                          "branch_create    Creates a new branch\n"
                          "branch_checkout  Checks out a given branch\n");
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
        fmt::print(std::cout, "usage: dvcsus {0} {1}", commandIt->m_command, fmt::join(commandIt->m_args, " "));
        return 1;
    }

    if (command == HELP_COMMAND)
    {
        ShowHelp();
    }
    else if (command == INIT_COMMAND)
    {
        return dvcs::Init() ? 0 : 1;
    }
    else if (command == ADD_COMMAND)
    {
        return dvcs::Add(argv[2]) ? 0 : 1;
    }
    else if (command == COMMIT_COMMAND)
    {
        return dvcs::Commit(argv[2], argv[3], argv[4]) ? 0 : 1;
    }
    else if (command == SET_REMOTE_COMMAND)
    {
        return dvcs::SetRemote(argv[2]) ? 0 : 1;
    }
    else if (command == PUSH_COMMAND)
    {
        return dvcs::Push() ? 0 : 1;
    }
    else if (command == PULL_COMMAND)
    {
        return dvcs::Pull() ? 0 : 1;
    }
    else if (command == BRANCH_CREATE_COMMAND)
    {
        return dvcs::CreateBranch(argv[2]) ? 0 : 1;
    }
    else if (command == BRANCH_CHECKOUT_COMMAND)
    {
        return dvcs::CheckoutBranch(argv[2]) ? 0 : 1;
    }
    else
    {
        assert(false);
        return 1;
    }

    return 0;
}