// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Android Auto's track metadata (media status channel 12) and its cover. No I/O.

// Field numbers are those of aasdk's MediaInfoChannelMetadataData:
//   0x8003 metadata  {1 title, 2 artist, 3 album, 4 album art (image bytes), 6 track length in seconds}
// The playback state (0x8001) is read by core/aa_channels.cpp. Every field is optional here: a phone that leaves one
// out gives an empty string or no cover. Not yet seen on the car: confirm with the first log of a session.
#ifndef ENTRY_AAD_PLATFORM_MEDIA_INFO_HPP
#define ENTRY_AAD_PLATFORM_MEDIA_INFO_HPP

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

namespace evo {
namespace media {

struct Track {
    std::string title, artist, album;
    std::vector<uint8_t> cover;  // as the phone sent it (JPEG or PNG), empty when none
    bool hasLength = false;
    int64_t lengthSeconds = 0;
};

// Reads a metadata message body. False when it is malformed: `out` is then left empty.
bool readMetadata(const uint8_t* body, size_t size, Track& out);

// The cover's file type from its first bytes: "jpg", "png", or "" when neither.
const char* coverType(const std::vector<uint8_t>& cover);

// A cheap fingerprint (FNV-1a, 32 bits) to tell a new cover from the one already saved.
uint32_t fingerprint(const std::vector<uint8_t>& data);

}  // namespace media
}  // namespace evo

#endif
