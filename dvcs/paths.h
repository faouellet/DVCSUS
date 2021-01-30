#pragma once

#include <filesystem>

namespace fs = std::filesystem;

namespace dvcs
{

const fs::path DVCS_PATH{".dvcs"};
const fs::path REPO_DB_PATH = DVCS_PATH / fs::path{"repo.db"};
const fs::path STAGING_DB_PATH = DVCS_PATH / fs::path{"staging.db"};

} // namespace dvcs
