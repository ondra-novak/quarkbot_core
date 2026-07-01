#pragma once

namespace quarkbot {

struct InterfaceVersion {
    unsigned int major;
    unsigned int minor;

    ///tests whether version which represents this header interface is compatible with ABI
    /**
        API headers can be used only if major version equals to abi major, or when
        API header's minor version is lower then abi version (so new features on ABI doesn't break header API)

        api.major == abi.major
        api.minor <= abi.minor
    */
    constexpr bool is_compatible_with_abi(const InterfaceVersion &abi) const {
        return abi.major == major && minor <= abi.minor;
    }
};

}