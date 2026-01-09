#ifndef HUFFMAN_H
#define HUFFMAN_H

#include <stddef.h>
#include <stdint.h>

// Huffman node structure
// Each node is 4 bytes: 2x 16-bit child indices
// If index < 0, it's a leaf node. The value is -(index + 1)
typedef struct {
    int16_t children[2];
} HuffmanNode;

// Huffman table structure for 1st-order conditional
typedef struct {
    HuffmanNode *trees; // Array of 128 trees, each tree starts at a fixed offset
    int nodes_per_tree;
} HuffmanTable;

// Initialize Huffman module
// Loads huffman.bin from the current directory
int huffman_init();

// Cleanup Huffman module
void huffman_cleanup();

// Decode an ATSC Multiple String Structure segment
// compr_type: 0x01 (Title) or 0x02 (Description)
// src: compressed bitstream
// src_len: length of bitstream in bytes
// dest: buffer to write decoded string
// dest_len: size of dest buffer
int huffman_decode(int compr_type, const uint8_t *src, int src_len, char *dest, int dest_len);

#endif
