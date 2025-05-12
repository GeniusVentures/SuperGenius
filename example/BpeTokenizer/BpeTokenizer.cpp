#include "BpeTokenizer.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <iterator>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>

using namespace rapidjson;

namespace {
    struct pair_hash {
        std::size_t operator()(const std::pair<std::string, std::string>& p) const {
            return std::hash<std::string>()(p.first + " " + p.second);
        }
    };
}

bool BpeTokenizer::load(const std::string& vocabPath, const std::string& mergesPath) {
    // Load vocab.json
    std::ifstream vocabFile(vocabPath);
    if (!vocabFile.is_open()) return false;
    IStreamWrapper isw(vocabFile);
    Document doc;
    doc.ParseStream(isw);
    for (auto itr = doc.MemberBegin(); itr != doc.MemberEnd(); ++itr) {
        vocab[itr->name.GetString()] = itr->value.GetInt();
    }

    // Load merges.txt
    std::ifstream mergesFile(mergesPath);
    if (!mergesFile.is_open()) return false;
    std::string line;
    int rank = 0;
    while (std::getline(mergesFile, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        std::string a, b;
        iss >> a >> b;
        mergeRanks[{a, b}] = rank++;
    }
    return true;
}

std::vector<std::string> BpeTokenizer::splitIntoBytes(const std::string& word) {
    std::vector<std::string> chars;
    for (unsigned char c : word) {
        chars.push_back(utf8ByteChar(c));
    }
    return chars;
}

std::string BpeTokenizer::utf8ByteChar(unsigned char byte) {
    return std::string(1, static_cast<char>(byte));
}

std::vector<std::string> BpeTokenizer::bpe(const std::string& word) const {
    auto tokens = splitIntoBytes(word);
    std::set<std::pair<std::string, std::string>> pairs;
    for (size_t i = 0; i + 1 < tokens.size(); ++i)
        pairs.insert({tokens[i], tokens[i + 1]});

    while (!pairs.empty()) {
        auto minRank = INT_MAX;
        std::pair<std::string, std::string> bestPair;

        for (auto& pair : pairs) {
            auto it = mergeRanks.find(pair);
            if (it != mergeRanks.end() && it->second < minRank) {
                minRank = it->second;
                bestPair = pair;
            }
        }

        if (minRank == INT_MAX) break;

        std::vector<std::string> newTokens;
        size_t i = 0;
        while (i < tokens.size()) {
            if (i + 1 < tokens.size() && tokens[i] == bestPair.first && tokens[i + 1] == bestPair.second) {
                newTokens.push_back(tokens[i] + tokens[i + 1]);
                i += 2;
            } else {
                newTokens.push_back(tokens[i++]);
            }
        }
        tokens = std::move(newTokens);

        pairs.clear();
        for (size_t i = 0; i + 1 < tokens.size(); ++i)
            pairs.insert({tokens[i], tokens[i + 1]});
    }
    return tokens;
}

std::vector<int> BpeTokenizer::encode(const std::string& input) const {
    std::istringstream iss(input);
    std::vector<int> result;
    std::string word;

    while (iss >> word) {
        auto bpeTokens = bpe(word);
        for (const auto& token : bpeTokens) {
            auto it = vocab.find(token);
            if (it != vocab.end()) {
                result.push_back(it->second);
            } else {
                result.push_back(0); // or 0 if unknown
            }
        }
    }
    return result;
}
