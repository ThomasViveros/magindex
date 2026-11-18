#pragma once

#include <stddef.h>

#ifdef __cplusplus
#include <filesystem>
#include <memory>
#include <vector>

#include "types/card.h"
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

        [[nodiscard]] std::vector<types::Card> Query(const types::CardQuery &query);
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

// database_path_utf8 names the SQLite database file, not a directory.
MagindexStatus magindex_open(const char *database_path_utf8, MagindexHandle **out_handle);
void magindex_close(MagindexHandle *handle);
MagindexStatus magindex_clear(MagindexHandle *handle);

// query_json and result_json are UTF-8. The result buffer is allocated by the
// library and must be released with magindex_free_string().
// Query JSON accepts these optional keys:
//   name: string; sets/supertypes/subtypes/mana_symbols/rarities: string arrays
//   power/toughness/loyalty/defense/cmc: arrays of {"op":">=","value":2,"logic":"and"}
//   colors: {"op":"exactly|at_most|including","colors":["W","U"]}
//   legalities: format-to-status object, e.g. {"commander":"legal"}
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
