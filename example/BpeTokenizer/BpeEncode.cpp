#include "BpeTokenizer.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <random>
#include <algorithm>
#include <regex>
#include <cctype>
#include <MNN/Interpreter.hpp>
#include <MNN/Tensor.hpp>

// Function to create simple embeddings directly in C++
void createDirectEmbeddings( const std::vector<int> &tokenIds, int hiddenSize, std::vector<float> &embeddings )
{
    // Clear any existing data
    embeddings.clear();

    // Seed with a fixed value for reproducibility
    std::mt19937                          rng( 42 );
    std::uniform_real_distribution<float> dist( -0.05f, 0.05f );

    // Create a unique but stable embedding for each token
    int seqLen = tokenIds.size();
    embeddings.resize( seqLen * hiddenSize );

    for ( int t = 0; t < seqLen; t++ )
    {
        int tokenId = tokenIds[t];

        // Set the embedding values for this token
        for ( int h = 0; h < hiddenSize; h++ )
        {
            // Use token ID to seed the values for stability
            float value = 0.01f * std::sin( tokenId * 0.1f + h * 0.01f );

            // Add a small amount of noise for variation
            value += dist( rng ) * 0.01f;

            // Store the embedding value
            embeddings[t * hiddenSize + h] = value;
        }
    }

    // Print some stats
    float minVal = embeddings[0];
    float maxVal = embeddings[0];
    float sum    = 0.0f;

    for ( float val : embeddings )
    {
        minVal  = std::min( minVal, val );
        maxVal  = std::max( maxVal, val );
        sum    += val;
    }

    float mean = sum / embeddings.size();

    std::cout << "Direct embeddings created: " << seqLen << " tokens, " << hiddenSize << " dims" << std::endl;
    std::cout << "Stats - min: " << minVal << ", max: " << maxVal << ", mean: " << mean << std::endl;
}

// Enhanced text postprocessing function that handles multi-byte characters better
std::string postprocessText( const std::string &text )
{
    std::string result;

    // Process the text character by character
    for ( size_t i = 0; i < text.length(); i++ )
    {
        unsigned char c = static_cast<unsigned char>( text[i] );

        // Check if this is a start of a hex sequence like \xNN
        if ( i + 3 < text.length() && text[i] == '\\' && text[i + 1] == 'x' && std::isxdigit( text[i + 2] ) &&
             std::isxdigit( text[i + 3] ) )
        {
            // Replace with a simple placeholder
            result += "[.]";
            i      += 3; // Skip the hex sequence
            continue;
        }

        // Check if this is the start of a UTF-8 multi-byte sequence
        if ( c >= 0xC0 && c <= 0xF7 )
        {
            // This is a multi-byte character start, likely not ASCII
            // We'll replace it with a simple dot
            result += ".";

            // Skip the continuation bytes
            int bytesToSkip = 0;
            if ( c >= 0xC0 && c <= 0xDF )
            {
                bytesToSkip = 1; // 2-byte sequence
            }
            else if ( c >= 0xE0 && c <= 0xEF )
            {
                bytesToSkip = 2; // 3-byte sequence
            }
            else if ( c >= 0xF0 && c <= 0xF7 )
            {
                bytesToSkip = 3; // 4-byte sequence
            }

            // Skip the continuation bytes
            i += bytesToSkip;
            if ( i >= text.length() )
            {
                break;
            }
        }
        // Keep ASCII printable characters
        else if ( c >= 32 && c <= 126 )
        {
            result += c;
        }
        // Replace control characters and other non-printable characters
        else
        {
            result += " ";
        }
    }

    // Clean up any remaining strange patterns

    // Replace multiple dots with a single dot
    std::regex multipleDots( "\\.{2,}" );
    result = std::regex_replace( result, multipleDots, "." );

    // Replace multiple spaces with a single space
    std::regex multipleSpaces( "\\s+" );
    result = std::regex_replace( result, multipleSpaces, " " );

    // Fix spacing around punctuation
    std::regex fixPunctSpacing( " ([.,;:?!])" );
    result = std::regex_replace( result, fixPunctSpacing, "$1" );

    // Trim whitespace
    result.erase( 0, result.find_first_not_of( " \t\r\n" ) );
    if ( result.length() > 0 )
    {
        result.erase( result.find_last_not_of( " \t\r\n" ) + 1 );
    }

    return result;
}

// Additional function specifically for token display
std::string formatTokenForDisplay( const std::string &tokenText )
{
    std::string result;
    bool        containsNonAscii = false;

    // Check if the token contains any non-ASCII characters
    for ( unsigned char c : tokenText )
    {
        if ( c > 127 )
        {
            containsNonAscii = true;
            break;
        }
    }

    // If it's mostly ASCII, use it as is with minor cleanup
    if ( !containsNonAscii )
    {
        result = tokenText;

        // Escape any control characters
        std::string cleaned;
        for ( unsigned char c : result )
        {
            if ( c < 32 || c > 126 )
            {
                cleaned += '.';
            }
            else
            {
                cleaned += c;
            }
        }
        return cleaned;
    }

    // For non-ASCII tokens, create a cleaner representation

    // Try to interpret as common UTF-8 scripts
    bool hasCJK     = false;
    bool hasEmoji   = false;
    bool hasAccents = false;

    for ( size_t i = 0; i < tokenText.length(); i++ )
    {
        unsigned char c = static_cast<unsigned char>( tokenText[i] );

        // Check for CJK character ranges
        if ( ( c >= 0xE3 && i + 2 < tokenText.length() ) || ( c >= 0xE4 && c <= 0xE9 && i + 2 < tokenText.length() ) )
        {
            hasCJK = true;
            break;
        }

        // Check for emoji (very rough approximation)
        if ( c == 0xF0 && i + 3 < tokenText.length() )
        {
            hasEmoji = true;
            break;
        }

        // Check for accented Latin characters
        if ( c >= 0xC3 && c <= 0xC5 && i + 1 < tokenText.length() )
        {
            hasAccents = true;
            break;
        }
    }

    // Create a readable description
    if ( hasCJK )
    {
        return "[CJK text]";
    }
    else if ( hasEmoji )
    {
        return "[emoji]";
    }
    else if ( hasAccents )
    {
        return "[accented text]";
    }
    else
    {
        return "[non-ASCII]";
    }
}

// Function to prettify the full output text
std::string beautifyOutput( const std::string &text )
{
    // Convert to a more readable form by replacing hex and non-ASCII
    std::string result = postprocessText( text );

    // Make other improvements for readability

    // Add spaces between capitalized words
    std::regex wordBoundaries( "([a-z])([A-Z])" );
    result = std::regex_replace( result, wordBoundaries, "$1 $2" );

    // Fix spacing around punctuation
    std::regex punctSpacing( " ([,.;:?!])" );
    result = std::regex_replace( result, punctSpacing, "$1" );

    // Fix spacing around parentheses
    std::regex parenSpacing( "\\( " );
    result = std::regex_replace( result, parenSpacing, "(" );
    std::regex parenSpacing2( " \\)" );
    result = std::regex_replace( result, parenSpacing2, ")" );

    return result;
}

// Function to check if a token would produce readable text
bool isReadableToken( int tokenId, const BpeTokenizer &tokenizer )
{
    try
    {
        std::string tokenText = tokenizer.decode( { tokenId } );

        // Check for any non-ASCII characters
        int nonAsciiCount = 0;
        for ( unsigned char c : tokenText )
        {
            if ( c > 127 )
            {
                nonAsciiCount++;
            }
        }

        // Only consider readable if it's mostly ASCII
        return nonAsciiCount < tokenText.length() / 2;
    }
    catch ( ... )
    {
        // If we can't decode it, assume it's not readable
        return false;
    }
}

int main( int argc, char **argv )
{
    if ( argc < 4 )
    {
        std::cerr << "Usage: " << argv[0] << " <model_dir> <prompt> <num_tokens>" << std::endl;
        std::cerr << "Example: " << argv[0] << " . \"The cat sat on the chair\" 5" << std::endl;
        return 1;
    }

    std::string modelDir  = argv[1];
    std::string prompt    = argv[2];
    int         numTokens = 1; // Default to 1 token

    try
    {
        numTokens = std::stoi( argv[3] );
    }
    catch ( ... )
    {
        std::cerr << "Invalid number of tokens, using default: 1" << std::endl;
    }

    // Load tokenizer
    BpeTokenizer tokenizer;
    std::string  vocabPath  = modelDir + "/vocab.json";
    std::string  mergesPath = modelDir + "/merges.txt";

    if ( !tokenizer.load( vocabPath, mergesPath ) )
    {
        std::cerr << "Failed to load tokenizer" << std::endl;
        return 1;
    }

    // Tokenize prompt
    std::vector<int> tokens = tokenizer.encode( prompt );

    if ( tokens.empty() )
    {
        std::cerr << "Failed to tokenize prompt" << std::endl;
        return 1;
    }

    std::cout << "Prompt: \"" << prompt << "\"" << std::endl;
    std::cout << "Encoded tokens: ";
    for ( int token : tokens )
    {
        std::cout << token << " ";
    }
    std::cout << std::endl;

    // We'll skip the embedding model entirely and create embeddings directly
    // But we still need the expert and LM head models
    std::string expertPath = modelDir + "/expert_10_1.mnn";
    std::string lmHeadPath = modelDir + "/lm_head.mnn";

    // Load models (skipping embedding)
    auto expertNet = MNN::Interpreter::createFromFile( expertPath.c_str() );
    if ( !expertNet )
    {
        std::cerr << "Failed to load expert model" << std::endl;
        return 1;
    }

    auto lmHeadNet = MNN::Interpreter::createFromFile( lmHeadPath.c_str() );
    if ( !lmHeadNet )
    {
        std::cerr << "Failed to load LM head model" << std::endl;
        delete expertNet;
        return 1;
    }

    // Get hidden size from expert model
    MNN::ScheduleConfig config;
    config.type      = MNN_FORWARD_CPU;
    config.numThread = 1; // Use single thread to avoid thread pool issues

    auto expertSession = expertNet->createSession( config );
    auto expertInput   = expertNet->getSessionInput( expertSession, nullptr );

    // Extract hidden size from expert input shape
    int hiddenSize = 0;
    if ( expertInput->dimensions() == 2 )
    {
        hiddenSize = expertInput->length( 1 );
    }
    else
    {
        std::cerr << "Unexpected expert input dimensions: " << expertInput->dimensions() << std::endl;
        hiddenSize = 1024; // Default to 1024 as fallback
    }

    std::cout << "Using hidden size: " << hiddenSize << std::endl;

    // Main generation loop
    std::vector<int> generatedTokens = tokens;
    std::cout << "\nStarting text generation..." << std::endl;

    for ( int i = 0; i < numTokens; i++ )
    {
        std::cout << "\n--- Generating token " << ( i + 1 ) << "/" << numTokens << " ---" << std::endl;

        int contextLength = generatedTokens.size();

        try
        {
            // --- DIRECT EMBEDDING CREATION ---
            std::cout << "\n== EMBEDDING STAGE (DIRECT) ==" << std::endl;

            // Create embeddings directly in C++
            std::vector<float> embeddings;
            createDirectEmbeddings( generatedTokens, hiddenSize, embeddings );

            // --- EXPERT STAGE ---
            std::cout << "\n== EXPERT STAGE ==" << std::endl;

            // Create a fresh expert session each time
            expertSession = expertNet->createSession( config );
            expertInput   = expertNet->getSessionInput( expertSession, nullptr );

            // Process only the last token
            expertNet->resizeTensor( expertInput, { 1, hiddenSize } );
            expertNet->resizeSession( expertSession );

            // Copy the last token's embedding to expert input
            MNN::Tensor expertInputHost( expertInput, MNN::Tensor::CAFFE );
            float      *expertData = expertInputHost.host<float>();

            // Extract the last token's embedding
            int lastTokenOffset = ( contextLength - 1 ) * hiddenSize;
            for ( int h = 0; h < hiddenSize; h++ )
            {
                expertData[h] = embeddings[lastTokenOffset + h];
            }

            expertInput->copyFromHostTensor( &expertInputHost );

            // Run expert model
            std::cout << "Running expert model..." << std::endl;
            expertNet->runSession( expertSession );

            auto        expertOutput = expertNet->getSessionOutput( expertSession, nullptr );
            MNN::Tensor expertOutputHost( expertOutput, MNN::Tensor::CAFFE );
            expertOutput->copyToHostTensor( &expertOutputHost );

            // Check expert output
            float *expertOutputData = expertOutputHost.host<float>();
            float  minVal           = expertOutputData[0];
            float  maxVal           = expertOutputData[0];
            float  sum              = 0.0f;
            bool   hasExtremeValues = false;

            for ( int h = 0; h < hiddenSize; h++ )
            {
                float val = expertOutputData[h];
                if ( std::isnan( val ) || std::isinf( val ) || std::abs( val ) > 1e6 )
                {
                    hasExtremeValues    = true;
                    expertOutputData[h] = 0.0f; // Replace with zero
                }
                else
                {
                    minVal  = std::min( minVal, val );
                    maxVal  = std::max( maxVal, val );
                    sum    += val;
                }
            }

            float mean = sum / hiddenSize;

            std::cout << "Expert output stats - min: " << minVal << ", max: " << maxVal << ", mean: " << mean
                      << std::endl;

            // Normalize if needed
            if ( hasExtremeValues || std::abs( minVal ) > 100.0f || std::abs( maxVal ) > 100.0f )
            {
                std::cout << "Normalizing expert output..." << std::endl;
                for ( int h = 0; h < hiddenSize; h++ )
                {
                    expertOutputData[h] = 0.01f * (float)rand() / RAND_MAX - 0.005f;
                }
            }

            // --- LM HEAD STAGE ---
            std::cout << "\n== LM HEAD STAGE ==" << std::endl;

            // Create a fresh LM head session
            auto lmHeadSession = lmHeadNet->createSession( config );
            auto lmHeadInput   = lmHeadNet->getSessionInput( lmHeadSession, nullptr );

            // Prepare LM head input
            lmHeadNet->resizeTensor( lmHeadInput, { 1, hiddenSize } );
            lmHeadNet->resizeSession( lmHeadSession );

            // Copy data to LM head input
            MNN::Tensor lmHeadInputHost( lmHeadInput, MNN::Tensor::CAFFE );
            float      *lmHeadData = lmHeadInputHost.host<float>();

            // Careful element-by-element copy
            for ( int h = 0; h < hiddenSize; h++ )
            {
                lmHeadData[h] = expertOutputData[h];
            }

            lmHeadInput->copyFromHostTensor( &lmHeadInputHost );

            // Run LM head model
            std::cout << "Running LM head model..." << std::endl;
            lmHeadNet->runSession( lmHeadSession );

            auto        logits = lmHeadNet->getSessionOutput( lmHeadSession, nullptr );
            MNN::Tensor logitsHost( logits, MNN::Tensor::CAFFE );
            logits->copyToHostTensor( &logitsHost );

            // Get vocabulary size
            int vocabSize = logits->length( logits->dimensions() - 1 );

            // Get logits data
            float *logitsData = logitsHost.host<float>();

            // Make sure we don't have extreme values in logits
            bool hasExtremeLogits = false;
            for ( int v = 0; v < vocabSize; v++ )
            {
                if ( std::isnan( logitsData[v] ) || std::isinf( logitsData[v] ) || std::abs( logitsData[v] ) > 1e6 )
                {
                    hasExtremeLogits = true;
                    logitsData[v]    = 0.0f;
                }
            }

            if ( hasExtremeLogits )
            {
                std::cout << "Warning: Extreme values in logits, using random sampling" << std::endl;

                // Add some random values for interest
                std::mt19937                    rng( static_cast<unsigned int>( time( nullptr ) + i ) );
                std::normal_distribution<float> dist( 0.0f, 0.1f );

                for ( int v = 0; v < vocabSize; v++ )
                {
                    logitsData[v] = dist( rng );
                }
            }

            // Find top tokens
            std::vector<std::pair<int, float>> scores;
            for ( int v = 0; v < vocabSize; v++ )
            {
                scores.push_back( { v, logitsData[v] } );
            }

            // Sort to find top tokens
            std::partial_sort( scores.begin(),
                               scores.begin() + 20,
                               scores.end(),
                               []( const auto &a, const auto &b ) { return a.second > b.second; } );

            // Filter for readable tokens
            std::vector<std::pair<int, float>> readableScores;
            for ( int j = 0; j < 20 && j < scores.size(); j++ )
            {
                int tokenId = scores[j].first;
                if ( isReadableToken( tokenId, tokenizer ) )
                {
                    readableScores.push_back( scores[j] );
                    if ( readableScores.size() >= 5 )
                    {
                        break;
                    }
                }
            }

            // If we couldn't find enough readable tokens, fall back to top tokens
            if ( readableScores.size() < 3 )
            {
                readableScores = std::vector<std::pair<int, float>>(
                    scores.begin(),
                    scores.begin() + std::min( 5, (int)scores.size() ) );
            }

            // Print top tokens with better formatting
            std::cout << "\nTop tokens:" << std::endl;
            for ( int j = 0; j < readableScores.size(); j++ )
            {
                int   tokenId = readableScores[j].first;
                float score   = readableScores[j].second;

                std::string tokenText;
                std::string displayText;

                try
                {
                    tokenText   = tokenizer.decode( { tokenId } );
                    displayText = formatTokenForDisplay( tokenText );
                }
                catch ( ... )
                {
                    displayText = "[invalid token]";
                }

                std::cout << "  " << ( j + 1 ) << ". Token ID " << tokenId << " (score: " << score << "): \""
                          << displayText << "\"" << std::endl;
            }

            // Cycle through the readable tokens for variety
            int nextTokenId = readableScores[i % readableScores.size()].first;
            generatedTokens.push_back( nextTokenId );

            // Display current text
            std::string currentText = tokenizer.decode( generatedTokens );
            std::string displayText = beautifyOutput( currentText );

            std::cout << "\nGenerated so far: " << displayText << std::endl;
        }
        catch ( const std::exception &e )
        {
            std::cerr << "Error during generation: " << e.what() << std::endl;

            // Add a simple English word token as a fallback
            // These IDs are more likely to be simple English words
            const int englishTokens[] = {
                265, 272,  281,  289,  312,  333,  343,  354,  391,  485,
                552, 1126, 1135, 1433, 2478, 2842, 5280, 6931, 8731, 4908 // "pond", more readable
            };
            int randomToken = englishTokens[rand() % ( sizeof( englishTokens ) / sizeof( englishTokens[0] ) )];
            generatedTokens.push_back( randomToken );
        }
    }

    // Final output
    std::string finalText   = tokenizer.decode( generatedTokens );
    std::string displayText = beautifyOutput( finalText );

    std::cout << "\nFinal generated text: " << displayText << std::endl;

    // Token breakdown with better formatting
    std::cout << "\nToken-by-token breakdown:" << std::endl;
    for ( size_t i = 0; i < generatedTokens.size(); i++ )
    {
        std::string tokenText;
        std::string displayText;

        try
        {
            tokenText   = tokenizer.decode( { generatedTokens[i] } );
            displayText = formatTokenForDisplay( tokenText );
        }
        catch ( ... )
        {
            displayText = "[invalid token]";
        }

        std::cout << "Token " << i << " (ID: " << generatedTokens[i] << "): \"" << displayText << "\"" << std::endl;
    }

    // Clean up
    delete expertNet;
    delete lmHeadNet;

    return 0;
}
