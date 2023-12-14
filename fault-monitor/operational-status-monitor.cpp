#include "config.h"

#include "operational-status-monitor.hpp"

#ifdef IBM_SAI
#include "ibm-sai.hpp"
#endif

#include <phosphor-logging/elog.hpp>
#include <phosphor-logging/lg2.hpp>

namespace phosphor
{
namespace led
{
namespace Operational
{
namespace status
{
namespace monitor
{
void Monitor::removeCriticalAssociation(const std::string& objectPath)
{
    try
    {
        PropertyValue getAssociationValue = dBusHandler.getProperty(
            objectPath.c_str(), "xyz.openbmc_project.Association.Definitions",
            "Associations");

        auto association = std::get<AssociationsProperty>(getAssociationValue);

        AssociationTuple criticalAssociation{
            "health_rollup", "critical",
            "/xyz/openbmc_project/inventory/system/chassis"};

        auto it = std::find(association.begin(), association.end(),
                            criticalAssociation);

        if (it != association.end())
        {
            association.erase(it);

            dBusHandler.setProperty(
                objectPath.c_str(),
                "xyz.openbmc_project.Association.Definitions", "Associations",
                association);

            lg2::info(
                "Removed chassis critical association. INVENTORY_PATH = {PATH}",
                "PATH", objectPath);
        }
    }
    catch (const sdbusplus::exception::exception& e)
    {
        lg2::error(
            "Failed to remove chassis critical association, ERROR = {ERROR}, PATH = {PATH}",
            "ERROR", e, "PATH", objectPath);
    }
    catch (const std::bad_variant_access& e)
    {
        lg2::error(
            "Failed to remove chassis critical association, ERROR = {ERROR}, PATH = {PATH}",
            "ERROR", e, "PATH", objectPath);
    }
}

void Monitor::matchHandler(sdbusplus::message_t& msg)
{
    // Get the ObjectPath of the `xyz.openbmc_project.Inventory.Manager`
    // service
    std::string invObjectPath = msg.get_path();

    // Get all the properties of
    // "xyz.openbmc_project.State.Decorator.OperationalStatus" interface
    std::string interfaceName{};
    std::unordered_map<std::string, std::variant<bool>> properties;
    msg.read(interfaceName, properties);

    const auto it = properties.find("Functional");
    if (it != properties.end())
    {
        const bool* value = std::get_if<bool>(&it->second);
        if (!value)
        {
            lg2::error(
                "Faild to get the Functional property, INVENTORY_PATH = {PATH}",
                "PATH", invObjectPath);
            return;
        }

        if (*value)
        {
            removeCriticalAssociation(invObjectPath);
        }

        // See if the Inventory D-Bus object has an association with LED groups
        // D-Bus object.
        auto ledGroupPath = getLedGroupPaths(invObjectPath);
        if (ledGroupPath.empty())
        {
            lg2::info(
                "The inventory D-Bus object is not associated with the LED "
                "group D-Bus object. INVENTORY_PATH = {PATH}",
                "PATH", invObjectPath);
            return;
        }

        // Update the Asserted property by the Functional property value.
        updateAssertedProperty(ledGroupPath, *value);
    }
}

const std::vector<std::string>
    Monitor::getLedGroupPaths(const std::string& inventoryPath) const
{
    // Get endpoints from fType
    std::string faultLedAssociation = inventoryPath + "/fault_identifying";

    // endpoint contains the vector of strings, where each string is a Inventory
    // D-Bus object that this, associated with this LED Group D-Bus object
    // pointed to by fru_fault
    PropertyValue endpoint{};

    try
    {
        endpoint = dBusHandler.getProperty(faultLedAssociation,
                                           "xyz.openbmc_project.Association",
                                           "endpoints");
    }
    catch (const sdbusplus::exception_t& e)
    {
        lg2::error(
            "Failed to get endpoints property, ERROR = {ERROR}, PATH = {PATH}",
            "ERROR", e, "PATH", faultLedAssociation);

        return {};
    }

    return std::get<std::vector<std::string>>(endpoint);
}

void Monitor::updateAssertedProperty(
    const std::vector<std::string>& ledGroupPaths, bool value)
{
    for (const auto& path : ledGroupPaths)
    {
#ifdef IBM_SAI
        if (path == phosphor::led::ibm::PARTITION_SAI ||
            path == phosphor::led::ibm::PLATFORM_SAI)
        {
            continue;
        }
#endif

        try
        {
            // Call "Group Asserted --> true" if the value of Functional is
            // false Call "Group Asserted --> false" if the value of Functional
            // is true
            PropertyValue assertedValue{!value};
            dBusHandler.setProperty(path, "xyz.openbmc_project.Led.Group",
                                    "Asserted", assertedValue);
        }
        catch (const sdbusplus::exception_t& e)
        {
            lg2::error(
                "Failed to set Asserted property, ERROR = {ERROR}, PATH = {PATH}",
                "ERROR", e, "PATH", path);
        }
    }
}
} // namespace monitor
} // namespace status
} // namespace Operational
} // namespace led
} // namespace phosphor
