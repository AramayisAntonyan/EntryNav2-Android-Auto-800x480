// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Tests platform/media_info.cpp: Android Auto's track metadata and its cover, hostile messages too.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "media_info.hpp"
#include "pb.hpp"

namespace {

int failures = 0;

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                     \
        }                                                                   \
    } while (0)

using Bytes = std::vector<uint8_t>;
using evo::media::Track;

const Bytes kJpeg = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 'J', 'F', 'I', 'F'};
const Bytes kPng = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00};

Bytes metadata(const std::string& title, const std::string& artist, const std::string& album, const Bytes& cover,
               int64_t length) {
    Bytes m;
    if (!title.empty()) {
        aa::pb::appendString(m, 1, title);
    }
    if (!artist.empty()) {
        aa::pb::appendString(m, 2, artist);
    }
    if (!album.empty()) {
        aa::pb::appendString(m, 3, album);
    }
    if (!cover.empty()) {
        aa::pb::appendBytes(m, 4, cover.data(), cover.size());
    }
    if (length >= 0) {
        aa::pb::appendUint(m, 6, static_cast<uint64_t>(length));
    }
    return m;
}

void fullTrack() {
    const Bytes m = metadata("Song", "Artist", "Album", kJpeg, 215);
    Track t;
    CHECK(evo::media::readMetadata(m.data(), m.size(), t));
    CHECK(t.title == "Song" && t.artist == "Artist" && t.album == "Album");
    CHECK(t.cover == kJpeg && std::string(evo::media::coverType(t.cover)) == "jpg");
    CHECK(t.hasLength && t.lengthSeconds == 215);
}

void everythingOptional() {
    Track t;
    CHECK(evo::media::readMetadata(nullptr, 0, t));  // empty message: an empty track
    CHECK(t.title.empty() && t.cover.empty() && !t.hasLength);
    const Bytes m = metadata("Only a title", "", "", Bytes(), -1);
    CHECK(evo::media::readMetadata(m.data(), m.size(), t));
    CHECK(t.title == "Only a title" && t.artist.empty() && t.cover.empty() && !t.hasLength);
}

void pngAndUnknownCovers() {
    Track t;
    Bytes m = metadata("S", "A", "", kPng, 1);
    CHECK(evo::media::readMetadata(m.data(), m.size(), t));
    CHECK(std::string(evo::media::coverType(t.cover)) == "png");
    m = metadata("S", "A", "", Bytes{'G', 'I', 'F', '8'}, 1);
    CHECK(evo::media::readMetadata(m.data(), m.size(), t));
    CHECK(std::string(evo::media::coverType(t.cover)).empty());
    CHECK(std::string(evo::media::coverType(Bytes{0xFF, 0xD8})).empty());  // too short to tell
}

void firstOccurrenceWins() {
    Bytes m = metadata("First", "", "", Bytes(), -1);
    const Bytes later = metadata("Second", "", "", kPng, -1);
    m.insert(m.end(), later.begin(), later.end());
    Track t;
    CHECK(evo::media::readMetadata(m.data(), m.size(), t));
    CHECK(t.title == "First" && t.cover == kPng);
}

void wrongTypesAreSkipped() {
    Bytes m;
    aa::pb::appendUint(m, 1, 7);           // title as a varint: not a title
    aa::pb::appendString(m, 6, "length");  // length as bytes: not a length
    aa::pb::appendString(m, 9, "unknown field");
    Track t;
    CHECK(evo::media::readMetadata(m.data(), m.size(), t));
    CHECK(t.title.empty() && !t.hasLength);
}

void malformedLeavesNothing() {
    Bytes m = metadata("Song", "Artist", "", kJpeg, 3);
    m.push_back(0x22);  // field 4, bytes...
    m.push_back(0x7F);  // ...127 of them, none there
    Track t;
    t.title = "stale";
    CHECK(!evo::media::readMetadata(m.data(), m.size(), t));
    CHECK(t.title.empty() && t.cover.empty() && !t.hasLength);
    const uint8_t endless[] = {0x08, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    CHECK(!evo::media::readMetadata(endless, sizeof(endless), t));
}

void fingerprintTellsCoversApart() {
    CHECK(evo::media::fingerprint(kJpeg) == evo::media::fingerprint(Bytes(kJpeg)));
    CHECK(evo::media::fingerprint(kJpeg) != evo::media::fingerprint(kPng));
    CHECK(evo::media::fingerprint(Bytes()) == 2166136261u);
}

}  // namespace

int main() {
    fullTrack();
    everythingOptional();
    pngAndUnknownCovers();
    firstOccurrenceWins();
    wrongTypesAreSkipped();
    malformedLeavesNothing();
    fingerprintTellsCoversApart();
    if (failures) {
        std::fprintf(stderr, "test-media-info: %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("test-media-info: OK\n");
    return EXIT_SUCCESS;
}
