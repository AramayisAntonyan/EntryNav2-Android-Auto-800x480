// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "media_info.hpp"

#include "pb.hpp"

namespace evo {
namespace media {

namespace {

const uint32_t kTitle = 1, kArtist = 2, kAlbum = 3, kCover = 4, kLength = 6;

std::string text(const aa::pb::Field& f) {
    return f.type == aa::pb::kBytes ? std::string(reinterpret_cast<const char*>(f.data), f.size) : std::string();
}

}  // namespace

bool readPlaybackState(const uint8_t* body, size_t size, uint32_t* state) {
    if (!body && size) {
        return false;
    }
    aa::pb::FieldReader reader(body, size);
    aa::pb::Field f;
    while (reader.next(f)) {
        if (f.number == 1 && f.type == aa::pb::kVarint) {
            *state = static_cast<uint32_t>(f.value);
            return !reader.malformed();
        }
    }
    return false;
}

bool readMetadata(const uint8_t* body, size_t size, Track& out) {
    out = Track();
    if (!body && size) {
        return false;
    }
    Track t;
    aa::pb::FieldReader reader(body, size);
    aa::pb::Field f;
    // First occurrence of each field wins, as in kombi_phone.
    bool seenTitle = false, seenArtist = false, seenAlbum = false, seenCover = false;
    while (reader.next(f)) {
        if (f.number == kTitle && !seenTitle && f.type == aa::pb::kBytes) {
            t.title = text(f);
            seenTitle = true;
        } else if (f.number == kArtist && !seenArtist && f.type == aa::pb::kBytes) {
            t.artist = text(f);
            seenArtist = true;
        } else if (f.number == kAlbum && !seenAlbum && f.type == aa::pb::kBytes) {
            t.album = text(f);
            seenAlbum = true;
        } else if (f.number == kCover && !seenCover && f.type == aa::pb::kBytes) {
            t.cover.assign(f.data, f.data + f.size);
            seenCover = true;
        } else if (f.number == kLength && !t.hasLength && f.type == aa::pb::kVarint) {
            t.hasLength = true;
            t.lengthSeconds = static_cast<int64_t>(f.value);
        }
    }
    if (reader.malformed()) {
        return false;
    }
    out = t;
    return true;
}

const char* coverType(const std::vector<uint8_t>& c) {
    if (c.size() >= 3 && c[0] == 0xFF && c[1] == 0xD8 && c[2] == 0xFF) {
        return "jpg";
    }
    if (c.size() >= 8 && c[0] == 0x89 && c[1] == 'P' && c[2] == 'N' && c[3] == 'G' && c[4] == 0x0D && c[5] == 0x0A &&
        c[6] == 0x1A && c[7] == 0x0A) {
        return "png";
    }
    return "";
}

uint32_t fingerprint(const std::vector<uint8_t>& data) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < data.size(); ++i) {
        h = (h ^ data[i]) * 16777619u;
    }
    return h;
}

}  // namespace media
}  // namespace evo
