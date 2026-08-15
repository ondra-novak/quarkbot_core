#pragma once

#include "quarkbot/storage.hpp"
#include <filesystem>
namespace quarkbot {

Storage::Replicator::Connection connect_variable_reporter(Storage stor, std::filesystem::path output);
Storage::Replicator::Connection connect_variable_reporter(Storage stor, std::ostream &out);


}