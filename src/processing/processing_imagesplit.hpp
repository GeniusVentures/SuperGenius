/**
* Header file for splitting images based on block stride, line stride, and size
* @author Justin Church
*/
#ifndef PROCESSING_IMAGESPLIT_HPP
#define PROCESSING_IMAGESPLIT_HPP
#include <math.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
#include <openssl/sha.h>
#include <libp2p/multi/content_identifier_codec.hpp>
//#include <stb_image.h>
//#include <stb_image_write.h>

namespace sgns::processing
{
    class ImageSplitter
    {
    public:
        ImageSplitter() {

        }
        /** Split an image loaded from file
        * @param filename - path/to/file.ext
        * @param blockstride - Stride to use for access pattern
        * @param blocklinestride - Line stride in bytes to get to next block start
        * @param blocklen - Block Length in bytes
        */
        ImageSplitter(
            const char* filename,
            uint64_t blockstride,
            uint64_t blocklinestride,
            uint64_t blocklen,
            int channels
        );

        /** Split an image loaded from raw data of a file loaded elsewhere, i.e. asynciomanager
        * @param buffer - Raw data of image file
        * @param blockstride - Stride to use for access pattern
        * @param blocklinestride - Line stride in bytes to get to next block start
        * @param blocklen - Block Length in bytes
        */
        ImageSplitter(const std::vector<char>& buffer,
            uint64_t blockstride,
            uint64_t blocklinestride,
            uint64_t blocklen,
            int channels);
        
        /** Split an image loaded from raw RGBA bytes
        * @param buffer - Raw RGBA
        * @param blockstride - Stride to use for access pattern
        * @param blocklinestride - Line stride in bytes to get to next block start
        * @param blocklen - Block Length in bytes
        */
        ImageSplitter(const std::vector<uint8_t>& buffer,
            uint64_t blockstride,
            uint64_t blocklinestride,
            uint64_t blocklen,
            int channels);

        ~ImageSplitter()
        {
            //free(inputImage);
        }

        /** Get data of part
        * @param part - index
        */
        std::vector<uint8_t> GetPart(int part);

        /** Get index of a part by CID
        * @param cid - CID of part
        */
        size_t GetPartByCid(libp2p::multi::ContentIdentifier cid);

        /** Get size of part in bytes
        * @param part - index
        */
        uint32_t GetPartSize(int part);

        /** Get stride of part 
        * @param part - index
        */
        uint32_t GetPartStride(int part);

        /** Get Width of part
        * @param part - index
        */
        int GetPartWidthActual(int part);

        /** Get Height of part
        * @param part - index
        */
        int GetPartHeightActual(int part);

        /** Get total number of parts
        */
        size_t GetPartCount();

        /** Get image size
        */
        size_t GetImageSize();

        libp2p::multi::ContentIdentifier GetPartCID(int part);

    private:
        /** Function that actually splits image data
        */
        void SplitImageData();

        std::vector<std::vector<uint8_t>> splitparts_;
        int partwidth_ = 32;
        int partheight_ = 32;
        uint64_t blockstride_;
        uint64_t blocklinestride_;
        uint64_t blocklen_;
        int channels_;
        const unsigned char* inputImage;
        uint64_t imageSize;
        std::vector<int> chunkWidthActual_;
        std::vector<int> chunkHeightActual_;
        std::vector<libp2p::multi::ContentIdentifier> cids_;
    };
}

#endif