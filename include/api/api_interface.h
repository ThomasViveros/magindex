#pragma once

#include <stddef.h>

#ifdef __cplusplus
#include <filesystem>
#include <memory>
#include <vector>

#include "types/card.h"
#include "types/oracle_card.h"
#include "types/query_builder.h"

namespace magindex::api {
    // Owns the database connection for its lifetime. Synchronous calls run on
    // the calling thread; a context serializes its own operations.
    class Context final {
    public:
        explicit Context(const std::filesystem::path &databasePath);
        ~Context();

        Context(const Context &) = delete;
        Context &operator=(const Context &) = delete;

        [[nodiscard]] types::QueryPage Query(const types::CardQuery &query,
                                             types::PageRequest page = {});

        void StoreCards(const std::vector<types::Card>& Cards);

        void GetTrackedSets(std::map<std::string, uint64_t>& OUTTrackedSets);

        void Clear();

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };
}

extern "C" {
#endif

typedef struct MagindexHandle MagindexHandle;

typedef enum MagindexStatus {
    MAGINDEX_STATUS_OK = 0,
    MAGINDEX_STATUS_INVALID_ARGUMENT = 1,
    MAGINDEX_STATUS_IO_ERROR = 2,
    MAGINDEX_STATUS_DATABASE_ERROR = 3,
    MAGINDEX_STATUS_QUERY_ERROR = 4,
    MAGINDEX_STATUS_INTERNAL_ERROR = 5
} MagindexStatus;

//magindex_path_utf8 is a directory that
// database_path_utf8 names the SQLite database file, not a directory.
MagindexStatus magindex_open(const char* magindex_path_utf8, const char *database_path_utf8, MagindexHandle **out_handle);
void magindex_close(MagindexHandle *handle);

/**
 *
 * @param handle Handle generated from magindex_open call
 * @param ignore_set_type_json Optional set types to ignore.
 * @return Status
 */
MagindexStatus magindex_update(MagindexHandle* handle, const char *ignore_set_type_json);

    MagindexStatus magindex_patch_set();

    void magindex_patch_card();



    //Emptys the database
MagindexStatus magindex_clear(MagindexHandle *handle);

// query_json and result_json are UTF-8. Query JSON can also include optional
// pagination fields: offset (default 0) and limit (default 50, maximum 1000).
// The result is an object with total_cards, offset, limit, has_more, and cards.
// The result buffer is allocated by the
// library and must be released with magindex_free_string().
// Query JSON accepts these optional keys:
//   name: string; sets/supertypes/subtypes/mana_symbols/rarities: string arrays
//   power/toughness/loyalty/defense/cmc: arrays of {"op":">=","value":2,"logic":"and"}
//   colors: {"op":"exactly|at_most|including","colors":["W","U"]}
//   legalities: format-to-status object, e.g. {"commander":"legal"}
// Set and rarity filters restrict which printings are included under each
// matching Oracle card. Other filters determine whether the Oracle card matches.
MagindexStatus magindex_query_json(MagindexHandle *handle, const char *query_json,
                                   char **out_result_json, size_t *out_result_size);
void magindex_free_string(char *value);

// Copies the most recent error for this handle (or the current thread's last
// open error when handle is NULL). required_size includes the
// trailing NUL. Pass buffer=NULL and capacity=0 to obtain the required size.
MagindexStatus magindex_get_last_error(MagindexHandle *handle, char *buffer,
                                       size_t capacity, size_t *required_size);

#ifdef __cplusplus
}
#endif
