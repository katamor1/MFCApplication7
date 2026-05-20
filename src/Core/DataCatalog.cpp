#include "DataCatalog.h"

#include <algorithm>

namespace {

std::vector<DataStyle> RawOnly()
{
    return {DataStyle::Raw};
}

std::vector<DataStyle> RawAnd(DataStyle style)
{
    return {DataStyle::Raw, style};
}

DataDefinition Definition(int dataId,
                          const wchar_t* name,
                          bool writable,
                          int minSubId1,
                          int maxSubId1,
                          int minSubId2,
                          int maxSubId2,
                          std::vector<DataStyle> styles)
{
    return {dataId, name, writable, minSubId1, maxSubId1, minSubId2, maxSubId2, std::move(styles)};
}

} // namespace

DataCatalog DataCatalog::CreateDefault()
{
    DataCatalog catalog;

    for (int dataId = 1000; dataId < 1020; ++dataId) {
        auto styles = RawOnly();
        if (dataId == 1010) {
            styles = RawAnd(DataStyle::ThousandsSeparated);
        } else if (dataId == 1012) {
            styles = RawAnd(DataStyle::SecondsToHhMmSs);
        } else if (dataId == 1014) {
            styles = RawAnd(DataStyle::MillimetersToInches);
        }
        catalog.AddDefinition(Definition(dataId, L"重要情報", false, 0, 0, 0, 0, styles));
        catalog.AddCriticalKey({dataId, 0, 0, styles.back()});
    }

    catalog.AddDefinition(Definition(2000, L"コンテナ番号", false, 1, 100, 0, 0, RawOnly()));
    catalog.AddDefinition(Definition(2001, L"コンテナ名", true, 1, 100, 0, 0, RawOnly()));
    catalog.AddDefinition(Definition(2002, L"コンテナ状態", false, 1, 100, 0, 0, RawOnly()));
    catalog.AddDefinition(Definition(2003, L"品目数", false, 1, 100, 0, 0, RawAnd(DataStyle::ThousandsSeparated)));
    catalog.AddDefinition(Definition(2100, L"品目名", true, 1, 100, 1, 10, RawOnly()));
    catalog.AddDefinition(Definition(2101, L"入庫日付", false, 1, 100, 1, 10, RawOnly()));
    catalog.AddDefinition(Definition(2102, L"出庫開始予定日時", true, 1, 100, 1, 10, RawOnly()));
    catalog.AddDefinition(Definition(2103, L"出庫順序", true, 1, 100, 1, 10, RawAnd(DataStyle::ThousandsSeparated)));
    catalog.AddDefinition(Definition(2104, L"出庫作業時間", false, 1, 100, 1, 10, RawAnd(DataStyle::SecondsToHhMmSs)));
    catalog.AddDefinition(Definition(3000, L"出庫終了予定日時", true, 1, 100, 1, 10, RawOnly()));
    catalog.AddDefinition(Definition(4000, L"出庫履歴", false, 0, 365, 0, 999, RawOnly()));

    return catalog;
}

const std::vector<DataDefinition>& DataCatalog::Definitions() const noexcept
{
    return definitions_;
}

const std::vector<DataKey>& DataCatalog::CriticalKeys() const noexcept
{
    return criticalKeys_;
}

const DataDefinition* DataCatalog::FindDefinition(int dataId) const noexcept
{
    const auto found = std::find_if(definitions_.begin(), definitions_.end(), [dataId](const DataDefinition& definition) {
        return definition.dataId == dataId;
    });
    return found == definitions_.end() ? nullptr : &(*found);
}

bool DataCatalog::IsStyleAllowed(int dataId, DataStyle style) const noexcept
{
    const auto* definition = FindDefinition(dataId);
    if (definition == nullptr) {
        return false;
    }
    return std::find(definition->allowedStyles.begin(), definition->allowedStyles.end(), style) != definition->allowedStyles.end();
}

BridgeError DataCatalog::ValidateKey(const DataKey& key) const noexcept
{
    const auto* definition = FindDefinition(key.dataId);
    if (definition == nullptr) {
        return BridgeError::InvalidDataId;
    }
    if (key.subId1 < definition->minSubId1 || key.subId1 > definition->maxSubId1 ||
        key.subId2 < definition->minSubId2 || key.subId2 > definition->maxSubId2) {
        return BridgeError::InvalidSubDataId;
    }
    if (!IsStyleAllowed(key.dataId, key.style)) {
        return BridgeError::InvalidStyle;
    }
    return BridgeError::Ok;
}

void DataCatalog::AddDefinition(DataDefinition definition)
{
    definitions_.push_back(std::move(definition));
}

void DataCatalog::AddCriticalKey(DataKey key)
{
    criticalKeys_.push_back(key);
}
