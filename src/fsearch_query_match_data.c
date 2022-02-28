#include <assert.h>
#include <glib.h>
#include <locale.h>
#include <stdlib.h>

#include "fsearch_limits.h"
#include "fsearch_query_match_data.h"
#include "fsearch_utf.h"

struct FsearchQueryMatchData {
    FsearchDatabaseEntry *entry;

    FsearchUtfBuilder *utf_name_builder;
    FsearchUtfBuilder *utf_path_builder;
    FsearchUtfBuilder *utf_parent_path_builder;
    GString *path_buffer;
    GString *parent_path_buffer;

    UCaseMap *case_map;
    const UNormalizer2 *normalizer;
    uint32_t fold_options;

    PangoAttrList *highlights[NUM_DATABASE_INDEX_TYPES];

    int32_t thread_id;

    bool utf_name_ready;
    bool utf_path_ready;
    bool utf_parent_path_ready;
    bool path_ready;
    bool parent_path_ready;
    bool matches;
};

FsearchUtfBuilder *
fsearch_query_match_data_get_utf_parent_path_builder(FsearchQueryMatchData *match_data) {
    if (!match_data->utf_parent_path_ready) {
        match_data->utf_parent_path_ready =
            fsearch_utf_builder_normalize_and_fold_case(match_data->utf_parent_path_builder,
                                                        match_data->case_map,
                                                        match_data->normalizer,
                                                        fsearch_query_match_data_get_parent_path_str(match_data));
    }
    return match_data->utf_parent_path_builder;
}

FsearchUtfBuilder *
fsearch_query_match_data_get_utf_name_builder(FsearchQueryMatchData *match_data) {
    if (!match_data->utf_name_ready) {
        match_data->utf_name_ready =
            fsearch_utf_builder_normalize_and_fold_case(match_data->utf_name_builder,
                                                        match_data->case_map,
                                                        match_data->normalizer,
                                                        db_entry_get_name_raw_for_display(match_data->entry));
    }
    return match_data->utf_name_builder;
}

FsearchUtfBuilder *
fsearch_query_match_data_get_utf_path_builder(FsearchQueryMatchData *match_data) {
    if (!match_data->utf_path_ready) {
        match_data->utf_path_ready =
            fsearch_utf_builder_normalize_and_fold_case(match_data->utf_path_builder,
                                                        match_data->case_map,
                                                        match_data->normalizer,
                                                        fsearch_query_match_data_get_path_str(match_data));
    }
    return match_data->utf_path_builder;
}

const char *
fsearch_query_match_data_get_name_str(FsearchQueryMatchData *match_data) {
    return db_entry_get_name_raw_for_display(match_data->entry);
}

const char *
fsearch_query_match_data_get_parent_path_str(FsearchQueryMatchData *match_data) {
    if (!match_data->entry) {
        return NULL;
    }
    if (!match_data->parent_path_ready) {
        g_string_truncate(match_data->parent_path_buffer, 0);
        db_entry_append_path(match_data->entry, match_data->parent_path_buffer);
        g_string_append_c(match_data->parent_path_buffer, G_DIR_SEPARATOR);

        match_data->parent_path_ready = true;
    }

    return match_data->parent_path_buffer->str;
}

const char *
fsearch_query_match_data_get_path_str(FsearchQueryMatchData *match_data) {
    if (!match_data->entry) {
        return NULL;
    }
    if (!match_data->path_ready) {
        g_string_truncate(match_data->path_buffer, 0);
        db_entry_append_path(match_data->entry, match_data->path_buffer);
        g_string_append_c(match_data->path_buffer, G_DIR_SEPARATOR);
        g_string_append(match_data->path_buffer, db_entry_get_name_raw(match_data->entry));

        match_data->path_ready = true;
    }

    return match_data->path_buffer->str;
}

FsearchDatabaseEntry *
fsearch_query_match_data_get_entry(FsearchQueryMatchData *match_data) {
    return match_data->entry;
}

FsearchQueryMatchData *
fsearch_query_match_data_new(void) {
    FsearchQueryMatchData *match_data = calloc(1, sizeof(FsearchQueryMatchData));
    assert(match_data != NULL);
    match_data->utf_name_builder = calloc(1, sizeof(FsearchUtfBuilder));
    match_data->utf_path_builder = calloc(1, sizeof(FsearchUtfBuilder));
    match_data->utf_parent_path_builder = calloc(1, sizeof(FsearchUtfBuilder));
    fsearch_utf_builder_init(match_data->utf_name_builder, 4 * PATH_MAX);
    fsearch_utf_builder_init(match_data->utf_path_builder, 4 * PATH_MAX);
    fsearch_utf_builder_init(match_data->utf_parent_path_builder, 4 * PATH_MAX);
    match_data->path_buffer = g_string_sized_new(PATH_MAX);
    match_data->parent_path_buffer = g_string_sized_new(PATH_MAX);

    match_data->utf_name_ready = false;
    match_data->utf_path_ready = false;
    match_data->utf_parent_path_ready = false;
    match_data->path_ready = false;
    match_data->parent_path_ready = false;

    match_data->fold_options = U_FOLD_CASE_DEFAULT;
    const char *current_locale = setlocale(LC_CTYPE, NULL);
    if (current_locale && (!strncmp(current_locale, "tr", 2) || !strncmp(current_locale, "az", 2))) {
        // Use special case mapping for Turkic languages
        match_data->fold_options = U_FOLD_CASE_EXCLUDE_SPECIAL_I;
    }

    UErrorCode status = U_ZERO_ERROR;
    match_data->case_map = ucasemap_open(current_locale, match_data->fold_options, &status);
    assert(U_SUCCESS(status));

    match_data->normalizer = unorm2_getNFDInstance(&status);
    assert(U_SUCCESS(status));

    return match_data;
}

static void
free_highlights(FsearchQueryMatchData *match_data) {
    if (!match_data) {
        return;
    }
    for (uint32_t i = 0; i < NUM_DATABASE_INDEX_TYPES; i++) {
        if (match_data->highlights[i]) {
            g_clear_pointer(&match_data->highlights[i], pango_attr_list_unref);
        }
    }
}

void
fsearch_query_match_data_free(FsearchQueryMatchData *match_data) {
    if (!match_data) {
        return;
    }

    free_highlights(match_data);

    fsearch_utf_builder_clear(match_data->utf_name_builder);
    g_clear_pointer(&match_data->utf_name_builder, free);
    fsearch_utf_builder_clear(match_data->utf_path_builder);
    g_clear_pointer(&match_data->utf_path_builder, free);
    fsearch_utf_builder_clear(match_data->utf_parent_path_builder);
    g_clear_pointer(&match_data->utf_parent_path_builder, free);

    g_clear_pointer(&match_data->case_map, ucasemap_close);

    g_string_free(g_steal_pointer(&match_data->path_buffer), TRUE);
    g_string_free(g_steal_pointer(&match_data->parent_path_buffer), TRUE);

    g_clear_pointer(&match_data, free);
}

void
fsearch_query_match_data_set_entry(FsearchQueryMatchData *match_data, FsearchDatabaseEntry *entry) {
    if (!match_data) {
        return;
    }

    // invalidate string buffers
    free_highlights(match_data);
    match_data->utf_name_ready = false;
    match_data->utf_path_ready = false;
    match_data->utf_parent_path_ready = false;
    match_data->path_ready = false;
    match_data->parent_path_ready = false;

    match_data->entry = entry;
}

void
fsearch_query_match_data_set_result(FsearchQueryMatchData *match_data, bool result) {
    match_data->matches = result;
}

bool
fsearch_query_match_data_get_result(FsearchQueryMatchData *match_data) {
    return match_data->matches;
}

void
fsearch_query_match_data_set_thread_id(FsearchQueryMatchData *match_data, int32_t thread_id) {
    match_data->thread_id = thread_id;
}

int32_t
fsearch_query_match_data_get_thread_id(FsearchQueryMatchData *match_data) {
    return match_data->thread_id;
}

PangoAttrList *
fsearch_query_match_get_highlight(FsearchQueryMatchData *match_data, FsearchDatabaseIndexType idx) {
    assert(idx < NUM_DATABASE_INDEX_TYPES);
    return match_data->highlights[idx];
}

void
fsearch_query_match_data_add_highlight(FsearchQueryMatchData *match_data,
                                       PangoAttribute *attribute,
                                       FsearchDatabaseIndexType idx) {
    assert(idx < NUM_DATABASE_INDEX_TYPES);
    if (!match_data->highlights[idx]) {
        match_data->highlights[idx] = pango_attr_list_new();
    }
    pango_attr_list_change(match_data->highlights[idx], attribute);
}