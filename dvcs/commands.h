#pragma once

#include <filesystem>
#include <iostream>
#include <string_view>

namespace fs = std::filesystem;

namespace dvcs
{

// Gestion locale
[[nodiscard]] bool Add(const fs::path &filePath) noexcept;
[[nodiscard]] bool Commit(std::string_view author, std::string_view email, std::string_view message) noexcept;
[[nodiscard]] bool Init() noexcept;
[[nodiscard]] bool Revert() noexcept;

// Gestion distante
// (Limité à un path pour ce prototype)
[[nodiscard]] bool Pull() noexcept;
[[nodiscard]] bool Push() noexcept;
[[nodiscard]] bool SetRemote(const fs::path &remoteRepoPath) noexcept;

// Gestion des branches
[[nodiscard]] bool CreateBranch(std::string_view branchName) noexcept;
[[nodiscard]] bool CheckoutBranch(std::string_view branchName) noexcept;

} // namespace dvcs
