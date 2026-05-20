#include "DataCatalog.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

struct JsonValue
{
    enum class Type
    {
        Object,
        Array,
        String,
        Number,
        Boolean,
        Null,
    };

    Type type{Type::Null};
    std::map<std::string, JsonValue> object;
    std::vector<JsonValue> array;
    std::string string;
    double number{};
    bool boolean{};
};

class JsonParser
{
public:
    explicit JsonParser(std::string text)
        : text_(std::move(text))
    {
    }

    JsonValue Parse()
    {
        auto value = ParseValue();
        SkipWhitespace();
        if (position_ != text_.size()) {
            Fail("unexpected trailing characters");
        }
        return value;
    }

private:
    JsonValue ParseValue()
    {
        SkipWhitespace();
        if (position_ >= text_.size()) {
            Fail("unexpected end of json");
        }

        const auto ch = text_[position_];
        if (ch == '{') {
            return ParseObject();
        }
        if (ch == '[') {
            return ParseArray();
        }
        if (ch == '"') {
            JsonValue value;
            value.type = JsonValue::Type::String;
            value.string = ParseString();
            return value;
        }
        if (ch == 't' || ch == 'f') {
            return ParseBoolean();
        }
        if (ch == 'n') {
            ExpectLiteral("null");
            JsonValue value;
            value.type = JsonValue::Type::Null;
            return value;
        }
        if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) {
            return ParseNumber();
        }
        Fail("invalid json value");
    }

    JsonValue ParseObject()
    {
        JsonValue value;
        value.type = JsonValue::Type::Object;
        Expect('{');
        SkipWhitespace();
        if (TryConsume('}')) {
            return value;
        }

        while (true) {
            SkipWhitespace();
            if (Peek() != '"') {
                Fail("expected object key");
            }
            auto key = ParseString();
            SkipWhitespace();
            Expect(':');
            value.object.emplace(std::move(key), ParseValue());
            SkipWhitespace();
            if (TryConsume('}')) {
                return value;
            }
            Expect(',');
        }
    }

    JsonValue ParseArray()
    {
        JsonValue value;
        value.type = JsonValue::Type::Array;
        Expect('[');
        SkipWhitespace();
        if (TryConsume(']')) {
            return value;
        }

        while (true) {
            value.array.push_back(ParseValue());
            SkipWhitespace();
            if (TryConsume(']')) {
                return value;
            }
            Expect(',');
        }
    }

    std::string ParseString()
    {
        Expect('"');
        std::string value;
        while (position_ < text_.size()) {
            const auto ch = text_[position_++];
            if (ch == '"') {
                return value;
            }
            if (ch == '\\') {
                if (position_ >= text_.size()) {
                    Fail("unterminated escape");
                }
                const auto escaped = text_[position_++];
                switch (escaped) {
                case '"':
                case '\\':
                case '/':
                    value.push_back(escaped);
                    break;
                case 'b':
                    value.push_back('\b');
                    break;
                case 'f':
                    value.push_back('\f');
                    break;
                case 'n':
                    value.push_back('\n');
                    break;
                case 'r':
                    value.push_back('\r');
                    break;
                case 't':
                    value.push_back('\t');
                    break;
                default:
                    Fail("unsupported json escape");
                }
            } else {
                value.push_back(ch);
            }
        }
        Fail("unterminated string");
    }

    JsonValue ParseBoolean()
    {
        JsonValue value;
        value.type = JsonValue::Type::Boolean;
        if (StartsWith("true")) {
            position_ += 4;
            value.boolean = true;
            return value;
        }
        if (StartsWith("false")) {
            position_ += 5;
            value.boolean = false;
            return value;
        }
        Fail("invalid boolean");
    }

    JsonValue ParseNumber()
    {
        const auto start = position_;
        if (Peek() == '-') {
            ++position_;
        }
        while (position_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[position_]))) {
            ++position_;
        }
        if (position_ < text_.size() && text_[position_] == '.') {
            ++position_;
            while (position_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[position_]))) {
                ++position_;
            }
        }

        JsonValue value;
        value.type = JsonValue::Type::Number;
        value.number = std::stod(text_.substr(start, position_ - start));
        return value;
    }

    void SkipWhitespace()
    {
        while (position_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[position_]))) {
            ++position_;
        }
    }

    char Peek() const
    {
        return position_ < text_.size() ? text_[position_] : '\0';
    }

    bool StartsWith(const char* literal) const
    {
        const std::string expected(literal);
        return text_.compare(position_, expected.size(), expected) == 0;
    }

    void ExpectLiteral(const char* literal)
    {
        if (!StartsWith(literal)) {
            Fail("expected literal");
        }
        position_ += std::string(literal).size();
    }

    void Expect(char expected)
    {
        SkipWhitespace();
        if (Peek() != expected) {
            Fail("unexpected character");
        }
        ++position_;
    }

    bool TryConsume(char expected)
    {
        SkipWhitespace();
        if (Peek() != expected) {
            return false;
        }
        ++position_;
        return true;
    }

    [[noreturn]] void Fail(const char* message) const
    {
        throw std::runtime_error(std::string(message) + " at byte " + std::to_string(position_));
    }

    std::string text_;
    size_t position_{};
};

std::string ReadAllBytes(const std::wstring& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open catalog json");
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::wstring Utf8ToWide(const std::string& value)
{
#ifdef _WIN32
    if (value.empty()) {
        return L"";
    }
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) {
        throw std::runtime_error("invalid utf-8 text in catalog");
    }
    std::wstring wide(length, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), wide.data(), length);
    return wide;
#else
    return std::wstring(value.begin(), value.end());
#endif
}

const JsonValue& RequireField(const JsonValue& object, const char* fieldName)
{
    if (object.type != JsonValue::Type::Object) {
        throw std::runtime_error("expected json object");
    }
    const auto found = object.object.find(fieldName);
    if (found == object.object.end()) {
        throw std::runtime_error(std::string("missing catalog field: ") + fieldName);
    }
    return found->second;
}

const std::vector<JsonValue>& RequireArray(const JsonValue& object, const char* fieldName)
{
    const auto& value = RequireField(object, fieldName);
    if (value.type != JsonValue::Type::Array) {
        throw std::runtime_error(std::string("catalog field must be array: ") + fieldName);
    }
    return value.array;
}

std::string RequireString(const JsonValue& object, const char* fieldName)
{
    const auto& value = RequireField(object, fieldName);
    if (value.type != JsonValue::Type::String) {
        throw std::runtime_error(std::string("catalog field must be string: ") + fieldName);
    }
    return value.string;
}

int RequireInt(const JsonValue& object, const char* fieldName)
{
    const auto& value = RequireField(object, fieldName);
    if (value.type != JsonValue::Type::Number) {
        throw std::runtime_error(std::string("catalog field must be number: ") + fieldName);
    }
    return static_cast<int>(value.number);
}

bool RequireBool(const JsonValue& object, const char* fieldName)
{
    const auto& value = RequireField(object, fieldName);
    if (value.type != JsonValue::Type::Boolean) {
        throw std::runtime_error(std::string("catalog field must be boolean: ") + fieldName);
    }
    return value.boolean;
}

int RequireRangeValue(const JsonValue& object, const char* fieldName, const char* rangeName)
{
    const auto& range = RequireField(object, fieldName);
    return RequireInt(range, rangeName);
}

std::vector<DataStyle> RequireStyles(const JsonValue& object)
{
    std::vector<DataStyle> styles;
    for (const auto& value : RequireArray(object, "styles")) {
        if (value.type != JsonValue::Type::String) {
            throw std::runtime_error("style entries must be strings");
        }
        styles.push_back(ParseDataStyleName(value.string));
    }
    if (styles.empty()) {
        throw std::runtime_error("catalog definition must allow at least one style");
    }
    return styles;
}

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

DataCatalog DataCatalog::LoadFromFile(const std::wstring& path)
{
    JsonParser parser(ReadAllBytes(path));
    const auto root = parser.Parse();

    DataCatalog catalog;
    for (const auto& value : RequireArray(root, "definitions")) {
        if (value.type != JsonValue::Type::Object) {
            throw std::runtime_error("catalog definitions must be objects");
        }
        catalog.AddDefinition({
            RequireInt(value, "dataId"),
            Utf8ToWide(RequireString(value, "name")),
            RequireBool(value, "writable"),
            RequireRangeValue(value, "subId1", "min"),
            RequireRangeValue(value, "subId1", "max"),
            RequireRangeValue(value, "subId2", "min"),
            RequireRangeValue(value, "subId2", "max"),
            RequireStyles(value),
        });
    }

    for (const auto& value : RequireArray(root, "criticalKeys")) {
        if (value.type != JsonValue::Type::Object) {
            throw std::runtime_error("critical keys must be objects");
        }
        catalog.AddCriticalKey({
            RequireInt(value, "dataId"),
            RequireInt(value, "subId1"),
            RequireInt(value, "subId2"),
            ParseDataStyleName(RequireString(value, "style")),
        });
    }

    for (const auto& definition : catalog.definitions_) {
        for (const auto style : definition.allowedStyles) {
            if (!catalog.IsStyleAllowed(definition.dataId, style)) {
                throw std::runtime_error("catalog style validation failed");
            }
        }
    }
    for (const auto& key : catalog.criticalKeys_) {
        const auto validation = catalog.ValidateKey(key);
        if (validation != BridgeError::Ok) {
            throw std::runtime_error("critical key references invalid definition");
        }
    }

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

bool DataCatalog::IsWritable(int dataId) const noexcept
{
    const auto* definition = FindDefinition(dataId);
    return definition != nullptr && definition->writable;
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
