#ifndef PROCESSING_IMAGESPLIT_HPP
#define PROCESSING_IMAGESPLIT_HPP
#include <math.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
#include <openssl/sha.h>
#include <libp2p/multi/content_identifier_codec.hpp>
#include <stb_image.h>
#include <stb_image_write.h>

namespace sgns::processing
{
    class ImageSplitter
    {
    public:
        ImageSplitter() {

        }
        /** Split an image loaded from fie
        * @param filename - path/to/file.ext
        * @param blockstride - 
        * @param blocklinestride - 
        * @param blocklen - 
        */
        ImageSplitter(
            const char* filename,
            uint32_t blockstride,
            uint32_t blocklinestride,
            uint32_t blocklen
        );

        /** Split an image loaded from raw data of a file loaded elsewhere, i.e. asynciomanager
        * @param buffer - Raw data of image file
        * @param blockstride -
        * @param blocklinestride -
        * @param blocklen -
        */
        ImageSplitter(const std::vector<char>& buffer,
            uint32_t blockstride,
            uint32_t blocklinestride,
            uint32_t blocklen);
        
        /** Split an image loaded from raw RGBA bytes
        * @param buffer - Raw RGBA
        * @param blockstride -
        * @param blocklinestride -
        * @param blocklen -
        */
        ImageSplitter(const std::vector<uint8_t>& buffer,
            uint32_t blockstride,
            uint32_t blocklinestride,
            uint32_t blocklen);

        ~ImageSplitter()
        {
            //free(inputImage);
        }
        std::vector<uint8_t> GetPart(int part);

        size_t GetPartByCid(libp2p::multi::ContentIdentifier cid);

        uint32_t GetPartSize(int part);

        uint32_t GetPartStride(int part);

        int GetPartWidthActual(int part);

        int GetPartHeightActual(int part);

        size_t GetPartCount();

        size_t GetImageSize();

        libp2p::multi::ContentIdentifier GetPartCID(int part);

    private:
        std::vector<std::vector<uint8_t>> splitparts_;
        int partwidth_ = 32;
        int partheight_ = 32;
        uint32_t blockstride_;
        uint32_t blocklinestride_;
        uint32_t blocklen_;
        const unsigned char* inputImage;
        size_t imageSize;
        std::vector<int> chunkWidthActual_;
        std::vector<int> chunkHeightActual_;
        std::vector<libp2p::multi::ContentIdentifier> cids_;

        void SplitImageData();
    };
}

#endif