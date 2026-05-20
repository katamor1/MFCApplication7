#include "DataCatalog.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <windows.h>

namespace {

std::string WideToUtf8(const std::wstring& value)
{
    if (value.empty()) {
        return "";
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), utf8.data(), length, nullptr, nullptr);
    return utf8;
}

std::wstring JoinStyles(const std::vector<DataStyle>& styles)
{
    std::wstring text;
    for (size_t index = 0; index < styles.size(); ++index) {
        if (index > 0) {
            text += L", ";
        }
        text += ToStyleName(styles[index]);
    }
    return text;
}

bool IsCritical(const DataCatalog& catalog, int dataId)
{
    for (const auto& key : catalog.CriticalKeys()) {
        if (key.dataId == dataId) {
            return true;
        }
    }
    return false;
}

std::wstring GenerateMarkdown(const DataCatalog& catalog, const std::wstring& inputPath)
{
    std::wostringstream out;
    out << L"# データIDカタログ仕様\n\n";
    out << L"- Source: `" << inputPath << L"`\n";
    out << L"- Definitions: " << catalog.Definitions().size() << L"\n";
    out << L"- Critical keys: " << catalog.CriticalKeys().size() << L"\n\n";
    out << L"## Definitions\n\n";
    out << L"| Data ID | Name | Writable | Sub ID1 | Sub ID2 | Styles | Critical |\n";
    out << L"|---:|---|---|---|---|---|---|\n";
    for (const auto& definition : catalog.Definitions()) {
        out << L"| " << definition.dataId
            << L" | " << definition.name
            << L" | " << (definition.writable ? L"yes" : L"no")
            << L" | " << definition.minSubId1 << L"-" << definition.maxSubId1
            << L" | " << definition.minSubId2 << L"-" << definition.maxSubId2
            << L" | " << JoinStyles(definition.allowedStyles)
            << L" | " << (IsCritical(catalog, definition.dataId) ? L"yes" : L"no")
            << L" |\n";
    }

    out << L"\n## Critical Keys\n\n";
    out << L"| Data ID | Sub ID1 | Sub ID2 | Style |\n";
    out << L"|---:|---:|---:|---|\n";
    for (const auto& key : catalog.CriticalKeys()) {
        out << L"| " << key.dataId << L" | " << key.subId1 << L" | " << key.subId2 << L" | " << ToStyleName(key.style) << L" |\n";
    }
    return out.str();
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    const std::wstring inputPath = argc >= 2 ? argv[1] : L"config/data_catalog.json";
    const std::wstring outputPath = argc >= 3 ? argv[2] : L"docs/data-catalog.md";

    try {
        const auto catalog = DataCatalog::LoadFromFile(inputPath);
        const auto markdown = GenerateMarkdown(catalog, inputPath);
        std::filesystem::create_directories(std::filesystem::path(outputPath).parent_path());
        std::ofstream output(outputPath, std::ios::binary);
        output << WideToUtf8(markdown);
    } catch (const std::exception& ex) {
        std::cerr << "failed to generate data catalog spec: " << ex.what() << '\n';
        return 1;
    }
    return 0;
}
