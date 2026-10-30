// Stash/backpack search (pure, unit-tested): does an item match a typed query?
// A query is space-separated words, all of which must occur (case-insensitive)
// in the item's name, type, rarity or flags: "ancient ring", "primal", "unidentified".
// A primal item also matches "ancient". An empty query matches nothing.
#pragma once

#include <cctype>
#include <cstdio>
#include <cstring>

#include "Snapshot.h"

namespace d3hud {

inline bool ItemMatchesQuery(const ItemSnapshot& it, const char* query) {
    char hay[256];
    snprintf(hay, sizeof(hay), "%s %s %s%s%s%s", it.name, it.typeName, RarityName(it.rarity),
             it.primal ? " primal ancient" : it.ancient ? " ancient" : "", it.identified ? "" : " unidentified",
             it.augmentRank ? " augmented" : "");
    for (char* c = hay; *c != '\0'; ++c) *c = static_cast<char>(tolower(static_cast<unsigned char>(*c)));
    bool any = false;
    const char* p = query;
    while (*p != '\0') {
        while (*p == ' ') ++p;
        if (*p == '\0') break;
        char term[64];
        size_t n = 0;
        while (*p != '\0' && *p != ' ') {
            if (n < sizeof(term) - 1) term[n++] = static_cast<char>(tolower(static_cast<unsigned char>(*p)));
            ++p;
        }
        term[n] = '\0';
        any = true;
        if (strstr(hay, term) == nullptr) return false;
    }
    return any;
}

}  // namespace d3hud
