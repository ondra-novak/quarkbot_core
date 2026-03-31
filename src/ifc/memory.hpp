#pragma once

#include <memory_resource>
namespace quarkbot {

inline std::pmr::synchronized_pool_resource mem_pool = {};


}