#include "BpeTokenizer.hpp"
#include <MNN/Interpreter.hpp>
#include <MNN/Tensor.hpp>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <stdexcept>
#include <vector>
#include <string>
#include <chrono>
#include <thread>

using namespace MNN;

// Debug function to print tensor info
void printTensorInfo( const Tensor *tensor, const std::string &name )
{
    if ( !tensor )
    {
        std::cout << name << ": NULL tensor" << std::endl;
        return;
    }

    std::cout << name << " shape: [";
    for ( int i = 0; i < tensor->dimensions(); i++ )
    {
        std::cout << tensor->length( i ) << ( i < tensor->dimensions() - 1 ? ", " : "" );
    }
    std::cout << "], type code: " << tensor->getType().code << ", bytes: " << tensor->getType().bytes()
              << ", elements: " << tensor->elementSize() << std::endl;
}

// Debug helper to print tensor sample safely
void printTensorSample( const Tensor *tensor, const std::string &name, int count = 10 )
{
    if ( !tensor || !tensor->host<void>() )
    {
        std::cout << name << " sample: NULL data" << std::endl;
        return;
    }

    std::cout << name << " sample (" << count << " values): ";
    if ( tensor->getType().code == halide_type_float )
    {
        auto data = tensor->host<float>();
        for ( int i = 0; i < std::min( count, static_cast<int>( tensor->elementSize() ) ); i++ )
        {
            std::cout << data[i] << " ";
        }
    }
    else if ( tensor->getType().code == halide_type_int )
    {
        auto data = tensor->host<int>();
        for ( int i = 0; i < std::min( count, static_cast<int>( tensor->elementSize() ) ); i++ )
        {
            std::cout << data[i] << " ";
        }
    }
    std::cout << std::endl;
}

// Safe version of MNN model loading with retry
std::shared_ptr<Interpreter> safeLoadModel( const std::string &path, int retries = 3 )
{
    std::shared_ptr<Interpreter> net = nullptr;

    for ( int i = 0; i < retries; i++ )
    {
        std::cout << "Loading model: " << path << " (attempt " << ( i + 1 ) << ")" << std::endl;
        net = std::shared_ptr<Interpreter>( Interpreter::createFromFile( path.c_str() ) );

        if ( net )
        {
            std::cout << "Model loaded successfully" << std::endl;
            return net;
        }

        std::cerr << "Failed to load model, retrying in 1 second..." << std::endl;
        std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
    }

    std::cerr << "Failed to load model after " << retries << " attempts" << std::endl;
    return nullptr;
}

// Safe tensor copy that handles mismatched dimensions
bool safeTensorCopy( Tensor *dst, Tensor *src )
{
    if ( !dst || !src )
    {
        std::cerr << "NULL tensor in safeTensorCopy" << std::endl;
        return false;
    }

    if ( dst->elementSize() != src->elementSize() )
    {
        std::cerr << "Element size mismatch in safeTensorCopy: dst=" << dst->elementSize()
                  << ", src=" << src->elementSize() << std::endl;
        return false;
    }

    if ( dst->getType().code != src->getType().code )
    {
        std::cerr << "Type mismatch in safeTensorCopy" << std::endl;
        return false;
    }

    try
    {
        // Create a temporary tensor with the same buffer as src but with dst's dimensions
        // This avoids shape mismatch errors in copyToHostTensor
        auto temp = Tensor::create( dst->shape(), src->getType(), nullptr, Tensor::CAFFE );
        src->copyToHostTensor( temp );
        dst->copyFromHostTensor( temp );
        return true;
    }
    catch ( const std::exception &e )
    {
        std::cerr << "Exception in safeTensorCopy: " << e.what() << std::endl;
        return false;
    }
}

int main( int argc, char **argv )
{
// Enable backtrace for debugging
#ifdef __linux__
    signal( SIGSEGV,
            []( int sig )
            {
                void  *array[10];
                size_t size = backtrace( array, 10 );
                std::cerr << "Error: signal " << sig << std::endl;
                backtrace_symbols_fd( array, size, STDERR_FILENO );
                exit( 1 );
            } );
#endif

    std::string dir = "."; // default to current directory
    if ( argc > 1 )
    {
        dir = argv[1];
    }

    std::filesystem::path vocabPath  = std::filesystem::path( dir ) / "vocab.json";
    std::filesystem::path mergesPath = std::filesystem::path( dir ) / "merges.txt";
    std::filesystem::path embedPath  = std::filesystem::path( dir ) / "embedding.mnn";
    std::filesystem::path expertPath = std::filesystem::path( dir ) / "expert_10_1.mnn";
    std::filesystem::path headPath   = std::filesystem::path( dir ) / "lm_head.mnn";

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

    // --- Create standard MNN config ---
    MNN::ScheduleConfig config;
    config.type      = MNN_FORWARD_CPU;
    config.numThread = 4;

    BackendConfig backendConfig;
    backendConfig.precision = BackendConfig::Precision_High;
    backendConfig.memory    = BackendConfig::Memory_High;
    config.backendConfig    = &backendConfig;

    // --- Embedding ---
    std::cout << "\n==== EMBEDDING STAGE ====\n";
    auto embedNet = safeLoadModel( embedPath.string() );
    if ( !embedNet )
    {
        return 1;
    }

    auto embedSession = embedNet->createSession( config );
    auto embedInput   = embedNet->getSessionInput( embedSession, nullptr );

    printTensorInfo( embedInput, "Initial embedInput" );

    // Resize with batch dimension explicitly set to 1
    std::vector<int> inputDims = { 1, seq_len };
    embedNet->resizeTensor( embedInput, inputDims );
    embedNet->resizeSession( embedSession );

    printTensorInfo( embedInput, "Resized embedInput" );

    // Copy input data
    auto tempInput = new Tensor( embedInput, Tensor::CAFFE );
    auto inputPtr  = tempInput->host<int>();
    for ( int i = 0; i < seq_len; ++i )
    {
        inputPtr[i] = token_ids[i];
    }
    embedInput->copyFromHostTensor( tempInput );
    delete tempInput;

    // Run embedding
    std::cout << "Running embedding model..." << std::endl;
    embedNet->runSession( embedSession );
    std::cout << "Embedding completed" << std::endl;

    auto embedOut = embedNet->getSessionOutput( embedSession, nullptr );
    printTensorInfo( embedOut, "embedOut" );

    // Copy embedding output to host
    auto hostEmbed = new Tensor( embedOut, Tensor::CAFFE );
    embedOut->copyToHostTensor( hostEmbed );
    printTensorSample( hostEmbed, "Embedding output" );

    // Determine actual hidden size from the embedding output
    int batch    = embedOut->length( 0 );
    int sequence = embedOut->length( 1 );
    int hidden   = embedOut->length( 2 );
    std::cout << "Detected dimensions - Batch: " << batch << ", Sequence: " << sequence << ", Hidden: " << hidden
              << std::endl;

    // --- Expert ---
    std::cout << "\n==== EXPERT STAGE ====\n";
    auto expertNet = safeLoadModel( expertPath.string() );
    if ( !expertNet )
    {
        return 1;
    }

    auto expertSession = expertNet->createSession( config );
    auto expertInput   = expertNet->getSessionInput( expertSession, nullptr );

    printTensorInfo( expertInput, "Initial expertInput" );

    // Modify shape for expert input
    std::vector<int> expertInputDims;
    bool             reshapeNeeded = true;

    if ( expertInput->dimensions() == 2 )
    {
        if ( sequence == seq_len )
        {
            // Default case: [seq_len, hidden]
            expertInputDims = { seq_len, hidden };
            reshapeNeeded   = true;
        }
        else
        {
            // Batch included case: [batch*seq, hidden]
            expertInputDims = { batch * sequence, hidden };
            reshapeNeeded   = true;
        }
    }
    else if ( expertInput->dimensions() == 3 )
    {
        // 3D input case: [batch, seq, hidden]
        expertInputDims = { batch, sequence, hidden };
        reshapeNeeded   = true;
    }
    else
    {
        std::cout << "Using expert's original dimensions" << std::endl;
        reshapeNeeded = false;
    }

    if ( reshapeNeeded )
    {
        expertNet->resizeTensor( expertInput, expertInputDims );
        expertNet->resizeSession( expertSession );
    }

    printTensorInfo( expertInput, "Prepared expertInput" );

    // Copy data from embedding output to expert input
    auto flatEmbed = new Tensor( expertInput, Tensor::CAFFE );

    // Handle the dimensions properly
    if ( expertInput->dimensions() == 2 && embedOut->dimensions() == 3 )
    {
        // Need to flatten from [batch, seq, hidden] to [batch*seq, hidden]
        float *src = hostEmbed->host<float>();
        float *dst = flatEmbed->host<float>();

        // Copy with layout transformation
        for ( int b = 0; b < batch; b++ )
        {
            for ( int s = 0; s < sequence; s++ )
            {
                int srcOffset = ( b * sequence + s ) * hidden;
                int dstOffset = ( b * sequence + s ) * hidden;
                std::memcpy( dst + dstOffset, src + srcOffset, hidden * sizeof( float ) );
            }
        }
    }
    else
    {
        // Direct copy if dimensions match
        std::memcpy( flatEmbed->host<float>(), hostEmbed->host<float>(), hostEmbed->size() );
    }

    printTensorSample( flatEmbed, "Expert input data sample" );
    expertInput->copyFromHostTensor( flatEmbed );

    // Free memory
    delete hostEmbed;

    // Run expert model
    std::cout << "Running expert model..." << std::endl;
    expertNet->runSession( expertSession );
    std::cout << "Expert completed" << std::endl;

    auto expertOut = expertNet->getSessionOutput( expertSession, nullptr );
    printTensorInfo( expertOut, "expertOut" );

    auto expertHost = new Tensor( expertOut, Tensor::CAFFE );
    expertOut->copyToHostTensor( expertHost );
    printTensorSample( expertHost, "Expert output" );

    // --- LM Head with safer approach ---
    std::cout << "\n==== LM HEAD STAGE ====\n";

    // Try to create a different model storage path for the LM head
    std::string modelDir  = std::filesystem::path( headPath ).parent_path().string();
    std::string modelName = std::filesystem::path( headPath ).filename().string();

    // Try a smaller batch size for LM head to avoid memory issues
    int batchSize = 1; // Process one token at a time if needed

    try
    {
        auto headNet = safeLoadModel( headPath.string() );
        if ( !headNet )
        {
            return 1;
        }

        auto headSession = headNet->createSession( config );
        if ( !headSession )
        {
            std::cerr << "Failed to create LM head session" << std::endl;
            return 1;
        }

        auto headInput = headNet->getSessionInput( headSession, nullptr );
        if ( !headInput )
        {
            std::cerr << "Failed to get LM head input" << std::endl;
            return 1;
        }

        printTensorInfo( headInput, "Initial headInput" );

        // Prepare head input dimensions
        std::vector<int> headInputDims;
        if ( headInput->dimensions() == 2 )
        {
            // If LM head expects 2D input: [seq, hidden]
            headInputDims = { sequence, hidden };
        }
        else if ( headInput->dimensions() == 3 )
        {
            // If LM head expects 3D input: [batch, seq, hidden]
            headInputDims = { batch, sequence, hidden };
        }
        else
        {
            std::cerr << "Unexpected LM head input dimensions: " << headInput->dimensions() << std::endl;
            return 1;
        }

        std::cout << "Resizing LM head input to: ";
        for ( auto d : headInputDims )
        {
            std::cout << d << " ";
        }
        std::cout << std::endl;

        headNet->resizeTensor( headInput, headInputDims );
        headNet->resizeSession( headSession );

        printTensorInfo( headInput, "Resized headInput" );

        // Create a host tensor for the head input with proper dimensions
        auto headInputHost = new Tensor( headInput, Tensor::CAFFE );

        // Copy data from expert output with appropriate transformations
        if ( headInput->dimensions() == expertOut->dimensions() )
        {
            // Direct copy if dimensions match
            std::memcpy( headInputHost->host<float>(), expertHost->host<float>(), expertHost->size() );
        }
        else if ( headInput->dimensions() == 2 && expertOut->dimensions() == 3 )
        {
            // Flatten from [batch, seq, hidden] to [seq, hidden]
            float *src = expertHost->host<float>();
            float *dst = headInputHost->host<float>();

            // We'll just use the first batch
            int srcOffset = 0 * sequence * hidden;
            std::memcpy( dst, src + srcOffset, sequence * hidden * sizeof( float ) );
        }
        else if ( headInput->dimensions() == 3 && expertOut->dimensions() == 2 )
        {
            // Expand from [seq, hidden] to [batch, seq, hidden]
            float *src = expertHost->host<float>();
            float *dst = headInputHost->host<float>();

            for ( int b = 0; b < batch; b++ )
            {
                int dstOffset = b * sequence * hidden;
                std::memcpy( dst + dstOffset, src, sequence * hidden * sizeof( float ) );
            }
        }
        else
        {
            std::cerr << "Incompatible dimensions between expert output and LM head input" << std::endl;
            return 1;
        }

        printTensorSample( headInputHost, "LM head input data" );
        headInput->copyFromHostTensor( headInputHost );
        delete headInputHost;
        delete expertHost;

        // Run LM head model with error handling
        std::cout << "Running LM head model..." << std::endl;
        try
        {
            headNet->runSession( headSession );
            std::cout << "LM head completed" << std::endl;
        }
        catch ( const std::exception &e )
        {
            std::cerr << "Exception running LM head: " << e.what() << std::endl;
            return 1;
        }

        auto logits = headNet->getSessionOutput( headSession, nullptr );
        if ( !logits )
        {
            std::cerr << "Failed to get LM head output" << std::endl;
            return 1;
        }

        printTensorInfo( logits, "logits" );

        auto logitHost = new Tensor( logits, Tensor::CAFFE );
        logits->copyToHostTensor( logitHost );
        printTensorSample( logitHost, "Logits sample", 20 );

        // Process logits for final output
        int vocabSize = logits->length( logits->dimensions() - 1 );
        std::cout << "Vocabulary size: " << vocabSize << std::endl;

        // Get the logits for the last token
        int lastTokenPos = sequence - 1;

        // Safely get pointer to last token's logits
        float *lastLogits = nullptr;
        if ( logits->dimensions() == 3 )
        {
            // [batch, seq, vocab]
            lastLogits = logitHost->host<float>() + ( 0 * sequence + lastTokenPos ) * vocabSize;
        }
        else if ( logits->dimensions() == 2 )
        {
            // [seq, vocab]
            lastLogits = logitHost->host<float>() + lastTokenPos * vocabSize;
        }
        else
        {
            std::cerr << "Unexpected logits dimension: " << logits->dimensions() << std::endl;
            delete logitHost;
            return 1;
        }

        // Score and sort the top tokens
        std::vector<std::pair<int, float>> scored;
        for ( int i = 0; i < vocabSize; ++i )
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
            //if ( scored[i].first < tokenizer.vocab_size() )
            //{
                std::vector<int> predicted      = { scored[i].first };
                std::string      predicted_text = tokenizer.decode( predicted );
                std::cout << "Top prediction (decoded): \"" << predicted_text << "\"\n";
            //}
            //else
            //{
            //    std::cout << "Token ID out of vocabulary range!\n";
            //}
        }

        delete logitHost;
    }
    catch ( const std::exception &e )
    {
        std::cerr << "Exception in LM head processing: " << e.what() << std::endl;
        return 1;
    }
    catch ( ... )
    {
        std::cerr << "Unknown exception in LM head processing" << std::endl;
        return 1;
    }

    return 0;
}
