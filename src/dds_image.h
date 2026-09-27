#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The first image of a DDS file as 8-bit RGBA, rows top first: uncompressed
// RGBA and BGRA, and the block formats BC1, BC2, BC3 and BC7. False, with the
// reason, for anything else.
bool dds_decode_rgba(const uint8_t* data, size_t size, int& width, int& height, std::vector<uint8_t>& rgba,
                     std::string& why);
