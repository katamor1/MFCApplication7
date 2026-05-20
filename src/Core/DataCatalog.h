#pragma once

#include "DataTypes.h"

#include <string>
#include <vector>

struct DataDefinition
{
    int dataId{};
    std::wstring name;
    bool writable{false};
    int minSubId1{};
    int maxSubId1{};
    int minSubId2{};
    int maxSubId2{};
    std::vector<DataStyle> allowedStyles;
};

class DataCatalog
{
public:
    static DataCatalog CreateDefault();

    const std::vector<DataDefinition>& Definitions() const noexcept;
    const std::vector<DataKey>& CriticalKeys() const noexcept;
    const DataDefinition* FindDefinition(int dataId) const noexcept;
    bool IsStyleAllowed(int dataId, DataStyle style) const noexcept;
    BridgeError ValidateKey(const DataKey& key) const noexcept;

private:
    void AddDefinition(DataDefinition definition);
    void AddCriticalKey(DataKey key);

    std::vector<DataDefinition> definitions_;
    std::vector<DataKey> criticalKeys_;
};
