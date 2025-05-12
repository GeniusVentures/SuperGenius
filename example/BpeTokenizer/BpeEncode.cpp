#include "BpeTokenizer.hpp"
#include <MNN/Interpreter.hpp>
#include <MNN/Tensor.hpp>
#include <filesystem>
#include <iostream>
#include <algorithm>

using namespace MNN;

int main( int argc, char **argv )
{
    std::string dir = "."; // default to current directory
    if ( argc > 1 )
    {
        dir = argv[1];
    }

    std::filesystem::path vocabPath  = std::filesystem::path( dir ) / "vocab_trimmed.json";
    std::filesystem::path mergesPath = std::filesystem::path( dir ) / "merges.txt";
    std::filesystem::path embedPath  = std::filesystem::path( dir ) / "embedding_trimmed.mnn";
    std::filesystem::path expertPath = std::filesystem::path( dir ) / "expert_10_1.mnn";
    std::filesystem::path headPath   = std::filesystem::path( dir ) / "lm_head_trimmed.mnn";

    BpeTokenizer tokenizer;
    if ( !tokenizer.load( vocabPath.string(), mergesPath.string() ) )
    {
        std::cerr << "Failed to load tokenizer\n";
        return 1;
    }

    std::string input     = "The cat sat";
    auto        token_ids = tokenizer.encode( input );
    int         seq_len   = static_cast<int>( token_ids.size() );

    std::cout << "Input: " << input << "\nToken IDs: ";
    for ( int id : token_ids )
    {
        std::cout << id << " ";
    }
    std::cout << "\n";

    // --- Embedding ---
    auto embedNet     = Interpreter::createFromFile( embedPath.string().c_str() );
    auto embedSession = embedNet->createSession( {} );
    auto embedInput   = embedNet->getSessionInput( embedSession, nullptr );

    embedNet->resizeTensor( embedInput, { 1, seq_len } );
    embedNet->resizeSession( embedSession );

    auto inputPtr = embedInput->host<int>();
    for ( int i = 0; i < seq_len; ++i )
    {
        inputPtr[i] = token_ids[i];
    }

    embedNet->runSession( embedSession );
    auto embedOut = embedNet->getSessionOutput( embedSession, nullptr );

    // --- Expert ---
    auto expertNet     = Interpreter::createFromFile( expertPath.string().c_str() );
    auto expertSession = expertNet->createSession( {} );
    auto expertInput   = expertNet->getSessionInput( expertSession, nullptr );

    int hidden = embedOut->shape()[2];
    expertNet->resizeTensor( expertInput, { seq_len, hidden } );
    expertNet->resizeSession( expertSession );

    Tensor hostEmbed( embedOut, Tensor::CAFFE );
    Tensor expertHost( expertInput, Tensor::CAFFE );
    std::memcpy( expertHost.host<float>(), hostEmbed.host<float>(), sizeof( float ) * seq_len * hidden );
    expertInput->copyFromHostTensor( &expertHost );

    expertNet->runSession( expertSession );
    auto expertOut = expertNet->getSessionOutput( expertSession, nullptr );

    // --- LM Head ---
    auto headNet     = Interpreter::createFromFile( headPath.string().c_str() );
    auto headSession = headNet->createSession( {} );
    auto headInput   = headNet->getSessionInput( headSession, nullptr );

    headNet->resizeTensor( headInput, { seq_len, hidden } );
    headNet->resizeSession( headSession );

    Tensor expertOutHost( expertOut, Tensor::CAFFE );
    Tensor headHost( headInput, Tensor::CAFFE );
    std::memcpy( headHost.host<float>(), expertOutHost.host<float>(), sizeof( float ) * seq_len * hidden );
    headInput->copyFromHostTensor( &headHost );

    headNet->runSession( headSession );
    auto logits = headNet->getSessionOutput( headSession, nullptr );

    // --- Output Top-k ---
    int    vocab      = logits->shape()[1];
    float *lastLogits = logits->host<float>() + ( seq_len - 1 ) * vocab;

    std::vector<std::pair<int, float>> scored;
    for ( int i = 0; i < vocab; ++i )
    {
        scored.emplace_back( i, lastLogits[i] );
    }

    std::partial_sort( scored.begin(),
                       scored.begin() + 5,
                       scored.end(),
                       []( auto &a, auto &b ) { return a.second > b.second; } );

    std::cout << "Top 5 Predictions:\n";
    for ( int i = 0; i < 5; ++i )
    {
        std::cout << "Token ID " << scored[i].first << " (logit: " << scored[i].second << ")\n";
    }

    return 0;
}
