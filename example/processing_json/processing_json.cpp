#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>

// Helper function to change file extension to .data
std::string replaceExtensionWithData(const std::string& filename) {
    size_t lastDot = filename.find_last_of('.');
    if (lastDot == std::string::npos) {
        return filename + ".data"; // No extension found, just add .data
    }
    return filename.substr(0, lastDot) + ".data";
}

void processImage(const std::string& filename, int horizontalDiv, int verticalDiv) {
    int width, height, channels;

    // Load image using stb_image
    unsigned char* imageData = stbi_load(filename.c_str(), &width, &height, &channels, 0);
    if (!imageData) {
        std::cerr << "Failed to load image: " << filename << std::endl;
        return;
    }

    // Calculate total size in bytes
    size_t totalSize = static_cast<size_t>(width) * height * channels;

    // Calculate strides and chunk values
    int block_line_stride = width * channels;
    int block_stride = 0;
    int chunk_line_stride = block_line_stride / verticalDiv;
    int chunk_offset = 0;
    int chunk_stride = block_line_stride - chunk_line_stride;
    int chunk_subchunk_height = horizontalDiv;
    int chunk_subchunk_width = verticalDiv;
    int chunk_count = horizontalDiv * verticalDiv;

    // Print calculated values
    std::cout << "block_len: " << totalSize << std::endl;
    std::cout << "block_line_stride: " << block_line_stride << std::endl;
    std::cout << "block_stride: " << block_stride << std::endl;
    std::cout << "chunk_line_stride: " << chunk_line_stride << std::endl;
    std::cout << "chunk_offset: " << chunk_offset << std::endl;
    std::cout << "chunk_stride: " << chunk_stride << std::endl;
    std::cout << "chunk_subchunk_height: " << chunk_subchunk_height << std::endl;
    std::cout << "chunk_subchunk_width: " << chunk_subchunk_width << std::endl;
    std::cout << "chunk_count: " << chunk_count << std::endl;
    std::cout << "channels: " << channels << std::endl;

    // Construct the output filename with .data extension
    std::string outFilename = replaceExtensionWithData(filename);

    // Save the raw image data to a .data file
    std::ofstream outFile(outFilename, std::ios::binary);
    if (outFile) {
        outFile.write(reinterpret_cast<const char*>(imageData), totalSize);
        std::cout << "Saved raw data to: " << outFilename << std::endl;
    }
    else {
        std::cerr << "Failed to save data file: " << outFilename << std::endl;
    }

    // Free the image memory
    stbi_image_free(imageData);
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <image_file> <horizontal_divisions> <vertical_divisions>" << std::endl;
        return 1;
    }

    std::string filename = argv[1];
    int horizontalDiv = std::stoi(argv[2]);
    int verticalDiv = std::stoi(argv[3]);

    processImage(filename, horizontalDiv, verticalDiv);

    return 0;
}