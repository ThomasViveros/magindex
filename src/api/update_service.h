#pragma once

#include <filesystem>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>

namespace magindex::api {
    class Context;
}

namespace magindex::importer {
    class ImportResult;
}

namespace magindex::api::detail {

    class StorageFailure final : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    class NoMatchingOracleCard final : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    // Coordinates external import, parsing, and storage for the API entry points.
    class UpdateService final {
    public:
        UpdateService(Context& context, std::filesystem::path environmentPath);

        void Update(const std::set<std::string>& setTypesToIgnore);
        void PatchSet(const std::string& setIdOrCode);
        void PatchCard(const std::string& oracleId);

    private:
        void ProcessAndStore(const importer::ImportResult& importResult,
                             std::optional<std::string> expectedOracleId = std::nullopt);

        Context& context_;
        std::filesystem::path environmentPath_;
    };

}
