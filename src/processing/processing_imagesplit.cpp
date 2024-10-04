#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <processing/processing_imagesplit.hpp>

namespace sgns::processing
{
    //ImageSplitter::ImageSplitter(
    //    const char* filename,
    //    uint32_t blockstride,
    //    uint32_t blocklinestride,
    //    uint32_t blocklen
    //) : blockstride_(blockstride), blocklinestride_(blocklinestride), blocklen_(blocklen) {
    //    int originalWidth;
    //    int originalHeight;
    //    int originChannel;
    //    inputImage = stbi_load(filename, &originalWidth, &originalHeight, &originChannel, 4);
    //    imageSize = originalWidth * originalHeight * 4;
    //    //std::cout << " Image Size : " << imageSize << std::endl;
    //    // Check if imageSize is evenly divisible by blocklen_
    //    SplitImageData();
    //}

    //ImageSplitter::ImageSplitter(const std::vector<char>& buffer,
    //    uint32_t blockstride,
    //    uint32_t blocklinestride,
    //    uint32_t blocklen)
    //    : blockstride_(blockstride), blocklinestride_(blocklinestride), blocklen_(blocklen) {
    //    // Set inputImage and imageSize from the provided buffer
    //    //inputImage = reinterpret_cast<const unsigned char*>(buffer.data());
    //    int originalWidth;
    //    int originalHeight;
    //    int originChannel;
    //    inputImage = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(buffer.data()), buffer.size(), &originalWidth, &originalHeight, &originChannel, STBI_rgb_alpha);
    //    imageSize = originalWidth * originalHeight * 4;

    //    SplitImageData();
    //}

    ImageSplitter::ImageSplitter(const std::vector<uint8_t>& buffer,
        uint64_t blockstride,
        uint64_t blocklinestride,
        uint64_t blocklen)
        : blockstride_(blockstride), blocklinestride_(blocklinestride), blocklen_(blocklen) {
        // Set inputImage and imageSize from the provided buffer
        //inputImage = reinterpret_cast<const unsigned char*>(buffer.data());

        inputImage = reinterpret_cast<const unsigned char*>(buffer.data());
        imageSize = buffer.size();

        SplitImageData();
    }

    std::vector<uint8_t> ImageSplitter::GetPart(int part)
    {
        return splitparts_.at(part);
    }
    size_t ImageSplitter::GetPartByCid(libp2p::multi::ContentIdentifier cid)
    {
        //Find the index of cid in cids_
        auto it = std::find(cids_.begin(), cids_.end(), cid);
        if (it == cids_.end()) {
            //CID not found
            return -1;
        }

        //Find index in splitparts_ corresponding to cid
        size_t index = std::distance(cids_.begin(), it);

        //Return the data
        if (index < splitparts_.size()) {
            return index;
        }
        else {
            //Index out of range
            return -1;
        }
    }

    void ImageSplitter::SplitImageData()
    {
        // Check if imageSize is evenly divisible by blocklen_
        std::cout << "Image Size: " << imageSize << std::endl;
        std::cout << "Blocklen Size: " << blocklen_ << std::endl;
        if (imageSize % blocklen_ != 0) {
            throw std::invalid_argument("Image size is not evenly divisible by block length");
        }

        for (uint64_t i = 0; i < imageSize; i += blocklen_)
        {
            std::vector<uint8_t> chunkBuffer(blocklen_);
            int rowsdone = (i / (blocklen_ *
                ((blockstride_ + blocklinestride_) / blockstride_)));
            uint32_t bufferoffset = 0 + (i / blocklen_ * blockstride_);
            bufferoffset -= (blockstride_ + blocklinestride_) * rowsdone;
            bufferoffset +=
                rowsdone
                * (blocklen_ *
                    ((blockstride_ + blocklinestride_) / blockstride_));
            //std::cout << "buffer offset:  " << bufferoffset << std::endl;
            for (uint64_t size = 0; size < blocklen_; size += blockstride_)
            {
                auto chunkData = inputImage + bufferoffset;
                std::memcpy(chunkBuffer.data() + (size), chunkData, blockstride_);
                bufferoffset += blockstride_ + blocklinestride_;
            }
            //std::string filename = "chunk_" + std::to_string(i) + ".png";
            //int result = stbi_write_png(filename.c_str(), blockstride_ / 4, blocklen_ / blockstride_, 4, chunkBuffer.data(), blockstride_);
            //if (!result) {
            //    std::cerr << "Error writing PNG file: " << filename << "\n";
            //}
            splitparts_.push_back(chunkBuffer);
            chunkWidthActual_.push_back(blockstride_ / 4);
            chunkHeightActual_.push_back(blocklen_ / blockstride_);
            gsl::span<const uint8_t> byte_span(chunkBuffer);
            std::vector<uint8_t> shahash(SHA256_DIGEST_LENGTH);
            SHA256_CTX sha256;
            SHA256_Init(&sha256);
            SHA256_Update(&sha256, chunkBuffer.data(), chunkBuffer.size());
            SHA256_Final(shahash.data(), &sha256);
            auto hash = libp2p::multi::Multihash::create(libp2p::multi::HashType::sha256, shahash);
            cids_.push_back(libp2p::multi::ContentIdentifier(
                libp2p::multi::ContentIdentifier::Version::V0,
                libp2p::multi::MulticodecType::Code::DAG_PB,
                hash.value()
            ));
        }
    }

    uint32_t ImageSplitter::GetPartSize(int part)
    {
        return splitparts_.at(part).size();
    }

    uint32_t ImageSplitter::GetPartStride(int part)
    {
        return chunkWidthActual_.at(part);
    }

    int ImageSplitter::GetPartWidthActual(int part)
    {
        return chunkWidthActual_.at(part);
    }

    int ImageSplitter::GetPartHeightActual(int part)
    {
        return chunkHeightActual_.at(part);
    }

    size_t ImageSplitter::GetPartCount()
    {
        return splitparts_.size();
    }

    size_t ImageSplitter::GetImageSize()
    {
        return imageSize;
    }

    libp2p::multi::ContentIdentifier ImageSplitter::GetPartCID(int part)
    {
        return cids_.at(part);
    }
}