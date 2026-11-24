#include "update_service.h"

#include <map>
#include <stdexcept>

#include "api/api_interface.h"
#include "importer/importer_interface.h"
#include "importer/scryfall_importer.h"
#include "processor/scryfall_processor.h"
#include "storage/card_chunk_storage.h"

namespace magindex::api::detail {

    UpdateService::UpdateService(Context& context, std::filesystem::path environmentPath)
        : context_(context), environmentPath_(std::move(environmentPath)) {}

    void UpdateService::Update(const std::set<std::string>& setTypesToIgnore) {
        std::map<std::string, std::uint64_t> trackedSets;
        context_.GetTrackedSets(trackedSets);

        importer::ScryfallImporter importer;
        const importer::ImportResult result = importer.ImportCardsForUpdate(
            environmentPath_, trackedSets, setTypesToIgnore);
        ProcessAndStore(result);
    }

    void UpdateService::PatchSet(const std::string& setIdOrCode) {
        if (setIdOrCode.empty())
            throw std::invalid_argument("Set ID or code is required");

        importer::ScryfallImporter importer;
        const importer::ImportResult result = importer.ImportCardsForSetPatch(
            {setIdOrCode}, environmentPath_);
        if (result.Type() != UpdateType::PatchSets)
            throw std::logic_error("Set patch importer returned an unexpected update type");
        ProcessAndStore(result);
    }

    void UpdateService::PatchCard(const std::string& oracleId) {
        if (oracleId.empty())
            throw std::invalid_argument("Oracle ID is required");

        importer::ScryfallImporter importer;
        const importer::ImportResult result = importer.ImportCardForPatch(environmentPath_, oracleId);
        if (result.Type() != UpdateType::PatchCards)
            throw std::logic_error("Card patch importer returned an unexpected update type");
        ProcessAndStore(result, oracleId);
    }

    void UpdateService::ProcessAndStore(const importer::ImportResult& importResult,
                                        std::optional<std::string> expectedOracleId) {
        if (importResult.Type() == UpdateType::None)
            return;

        storage::CardChunk cardChunk;
        processor::ScryfallProcessor processor;
        switch (importResult.Type()) {
            case UpdateType::None:
                return;
            case UpdateType::PatchCards:
                if (!std::holds_alternative<importer::PatchCards>(importResult.Data()))
                    throw std::logic_error("Import result type does not match its patch-card payload");
                processor.ProcessCardsPatch(importResult.InstallFilePath(), cardChunk.cards);
                break;
            case UpdateType::PatchSets: {
                const auto* patch = std::get_if<importer::PatchSets>(&importResult.Data());
                if (!patch)
                    throw std::logic_error("Import result type does not match its patch-set payload");

                std::set<std::string> targetSets;
                if (patch->SetsToPatchFromBulkFile)
                    targetSets.insert(patch->SetsToPatchFromBulkFile->begin(),
                                      patch->SetsToPatchFromBulkFile->end());
                processor.ProcessSetsPatch(importResult.InstallFilePath(), cardChunk.cards,
                                           cardChunk.Sets, targetSets);
                break;
            }
            case UpdateType::TotalReset:
                if (!std::holds_alternative<importer::TotalReset>(importResult.Data()))
                    throw std::logic_error("Import result type does not match its total-reset payload");
                processor.ProcessTotalReset(importResult.InstallFilePath(), cardChunk.cards,
                                            cardChunk.Sets);
                break;
            default:
                throw std::logic_error("Importer returned an unsupported update type");
        }

        if (expectedOracleId) {
            bool foundExpectedIdentity = false;
            for (const auto& [storedOracleId, card] : cardChunk.cards) {
                if (storedOracleId == *expectedOracleId) {
                    foundExpectedIdentity = true;
                    break;
                }
                for (const auto& face : card.Faces) {
                    if (face.OracleId == *expectedOracleId) {
                        foundExpectedIdentity = true;
                        break;
                    }
                }
                if (foundExpectedIdentity)
                    break;
            }
            if (!foundExpectedIdentity)
                throw NoMatchingOracleCard("Scryfall returned no printings for Oracle ID: " + *expectedOracleId);
        }

        try {
            context_.StoreCards(cardChunk, importResult.Type());
        } catch (const std::exception& error) {
            throw StorageFailure(error.what());
        } catch (...) {
            throw StorageFailure("Unknown error while writing the imported card batch");
        }
    }

}
