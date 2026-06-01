#!/bin/bash
# Code review script using OpenAI ChatGPT Codex
# Reads diff from stdin, sends to OpenAI API for review
# Requires: OPENAI_API_KEY environment variable

set -euo pipefail

if [ -z "${OPENAI_API_KEY:-}" ]; then
    echo '{"verdict":"REVISE","confidence":0,"summary":"OPENAI_API_KEY not set","issues":[]}'
    exit 1
fi

# Read the review prompt from stdin
PROMPT=$(cat)

# Call OpenAI API with gpt-4o (Codex model)
RESPONSE=$(curl -s https://api.openai.com/v1/chat/completions \
    -H "Content-Type: application/json" \
    -H "Authorization: Bearer ${OPENAI_API_KEY}" \
    -d "$(jq -n \
        --arg prompt "$PROMPT" \
        '{
            "model": "gpt-4o",
            "messages": [
                {
                    "role": "system",
                    "content": "You are a code reviewer. Analyze the diff and respond with JSON: { \"verdict\": \"APPROVED\" or \"REVISE\", \"confidence\": 0-100, \"summary\": \"...\", \"issues\": [{\"severity\": \"...\", \"file\": \"...\", \"line_range\": \"...\", \"description\": \"...\", \"suggestion\": \"...\"}] }"
                },
                {
                    "role": "user",
                    "content": $prompt
                }
            ],
            "temperature": 0.1,
            "max_tokens": 4000
        }')")

# Extract the content from the response
CONTENT=$(echo "$RESPONSE" | jq -r '.choices[0].message.content // empty')

if [ -z "$CONTENT" ]; then
    echo '{"verdict":"REVISE","confidence":0,"summary":"Failed to get response from OpenAI API","issues":[]}'
    exit 1
fi

# Try to extract JSON from the response (handle markdown code blocks)
JSON_CONTENT=$(echo "$CONTENT" | sed -n '/^```json/,/^```/p' | sed '1d;$d')
if [ -z "$JSON_CONTENT" ]; then
    JSON_CONTENT="$CONTENT"
fi

# Validate it's valid JSON and output
echo "$JSON_CONTENT" | jq '.' 2>/dev/null || echo "{\"verdict\":\"REVISE\",\"confidence\":0,\"summary\":\"Invalid JSON response from API\",\"issues\":[]}"
