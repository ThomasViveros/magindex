#pragma once

#include <string>
#include <cstdint>

namespace mtg::importer {


    /* GET: https://api.scryfall.com/bulk-data/default-cards
     *
     *
    * "object": "bulk_data",
      "id": "e2ef41e3-5778-4bc2-af3f-78eca4dd9c23",
      "type": "default_cards",
      "updated_at": "2026-10-30T21:06:04.129+00:00",
      "uri": "https://api.scryfall.com/bulk-data/e2ef41e3-5778-4bc2-af3f-78eca4dd9c23",
      "name": "Default Cards",
      "description": "A JSON file containing every card object on Scryfall in English or the printed language if the card is only available in one language.",
      "jsonl_download_uri": "https://data.scryfall.io/default-cards/default-cards-20261030210604.jsonl.gz",
      "compressed_size": 80615941
     */

    struct BulkMetadata {
        std::string id;
        std::string type;
        std::string name;
        std::string updated_at;

        std::string download_uri;
        std::string jsonl_download_uri;

        std::uint64_t compressed_size = 0;
    };

}