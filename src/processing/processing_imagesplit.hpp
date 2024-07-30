/**
* Header file for splitting images based on block stride, line stride, and size
* @author Justin Church
*/
#ifndef PROCESSING_IMAGESPLIT_HPP
#define PROCESSING_IMAGESPLIT_HPP
#include <cmath>
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
        ImageSplitter() = default;
        /** Split an image loaded from file
        * @param filename - path/to/file.ext
        * @param blockstride - Stride to use for access pattern
        * @param blocklinestride - Line stride in bytes to get to next block start
        * @param blocklen - Block Length in bytes
        */
        ImageSplitter(
            const char* filename,
            uint32_t blockstride,
            uint32_t blocklinestride,
            uint32_t blocklen
        );

        /** Split an image loaded from raw data of a file loaded elsewhere, i.e. asynciomanager
        * @param buffer - Raw data of image file
        * @param blockstride - Stride to use for access pattern
        * @param blocklinestride - Line stride in bytes to get to next block start
        * @param blocklen - Block Length in bytes
        */
        ImageSplitter(const std::vector<char>& buffer,
            uint32_t blockstride,
            uint32_t blocklinestride,
            uint32_t blocklen);
        
        /** Split an image loaded from raw RGBA bytes
        * @param buffer - Raw RGBA
        * @param blockstride - Stride to use for access pattern
        * @param blocklinestride - Line stride in bytes to get to next block start
        * @param blocklen - Block Length in bytes
        */
        ImageSplitter(const std::vector<uint8_t>& buffer,
            uint32_t blockstride,
            uint32_t blocklinestride,
            uint32_t blocklen);

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
        size_t GetPartByCid( const libp2p::multi::ContentIdentifier &cid ) const;

        /** Get size of part in bytes
        * @param part - index
        */
        uint32_t GetPartSize( int part ) const;

        /** Get stride of part 
        * @param part - index
        */
        uint32_t GetPartStride( int part ) const;

        /** Get Width of part
        * @param part - index
        */
        int GetPartWidthActual( int part ) const;

        /** Get Height of part
        * @param part - index
        */
        int GetPartHeightActual( int part ) const;

        /** Get total number of parts
        */
        size_t GetPartCount() const;

        /** Get image size
        */
        size_t GetImageSize() const;

        libp2p::multi::ContentIdentifier GetPartCID( int part ) const;

    private:
        /** Function that actually splits image data
        */
        void SplitImageData();

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
    };
}

#endif