#pragma once

#include "processor_interface.h"
#include "types/card.h"

namespace magindex::processor {
    class ScryfallProcessor : public ProcessorBase {
    public:
        /**
         * Parse a Scryfall JSONL file, either plain or gzip-compressed, into Card values.
         * UpdateType::None returns an empty vector. I/O or malformed-record errors throw
         * std::runtime_error with the source path and line number when available.
         */
        std::vector<types::Card> Process(std::filesystem::path file, UpdateType updateType) override;
    };
}