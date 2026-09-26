#pragma once
#include <vector>

namespace drift {
// An empty ID is a saved "System default" choice, never a real endpoint.
// Resolve only against eligible outputs; do not silently pick an unrelated device.
template <class Device, class Id>
const Device *findOutput(const std::vector<Device> &devices, const Id &id) {
    if (id == Id{})
        return nullptr;
    for (const auto &device : devices)
        if (device.id == id)
            return &device;
    return nullptr;
}
template <class Device, class Id>
const Device *resolveOutput(const std::vector<Device> &devices, const Id &choice,
                            const Id &systemDefault, const Id &previous = Id{},
                            bool holdPrevious = false) {
    if (choice != Id{})
        return findOutput(devices, choice);
    if (const auto *device = findOutput(devices, systemDefault))
        return device;
    // Only the virtual-cable adapter opts in: its Windows default must be the
    // cable during use, while Drift's own output must remain a physical device.
    return holdPrevious ? findOutput(devices, previous) : nullptr;
}
} // namespace drift
