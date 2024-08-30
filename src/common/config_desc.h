#pragma once

#include <json20.h>

#include <quarkbot/strategy.h>

namespace quarkbot {

    json::value config_schema_to_json(const ConfigSchema &desc);
    json::value config_desc_to_json(const IStrategy *strategy);

}

