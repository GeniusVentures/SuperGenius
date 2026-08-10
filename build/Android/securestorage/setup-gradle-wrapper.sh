#!/bin/bash
# Script to set up Gradle wrapper for the Android AAR build

set -e

cd "$(dirname "$0")"

GRADLE_VERSION="8.14.4"

# Check if gradlew already exists and its version matches
if [ -f gradlew ] && [ -f gradle/wrapper/gradle-wrapper.jar ]; then
    EXISTING_VERSION=$(./gradlew --version 2>/dev/null | grep "^Gradle " | awk '{print $2}' || true)
    if [ "$EXISTING_VERSION" = "$GRADLE_VERSION" ]; then
        echo "Gradle wrapper already exists (version $GRADLE_VERSION matches)"
        exit 0
    elif [ -n "$EXISTING_VERSION" ]; then
        echo "Gradle wrapper exists but version is $EXISTING_VERSION (need $GRADLE_VERSION), regenerating..."
    else
        echo "Gradle wrapper exists but could not determine version, regenerating..."
    fi
else
    if [ -f gradlew ]; then
        echo "Gradle wrapper script exists but jar is missing, regenerating..."
    fi
fi

# Check if gradle is installed and what version
if command -v gradle &> /dev/null; then
    GRADLE_VER=$(gradle --version | grep "^Gradle" | awk '{print $2}')
    echo "Found Gradle $GRADLE_VER"
    
    # Check if version is in [6.0, 9.0) — Gradle 9.x removed APIs that AGP 8.x needs.
    if [ "$(printf '%s\n' "6.0" "$GRADLE_VER" | sort -V | head -n1)" = "6.0" ] && \
       [ "$(printf '%s\n' "$GRADLE_VER" "9.0" | sort -V | head -n1)" = "$GRADLE_VER" ]; then
        echo "Using installed Gradle to create wrapper..."
        gradle wrapper --gradle-version $GRADLE_VERSION
        exit 0
    elif [ "$(printf '%s\n' "9.0" "$GRADLE_VER" | sort -V | head -n1)" = "9.0" ]; then
        echo "Warning: Gradle $GRADLE_VER is too new (>= 9.0 breaks AGP 8.x), downloading wrapper manually"
    else
        echo "Warning: Gradle $GRADLE_VER is too old (need 6.0+)"
    fi
fi

echo "Downloading Gradle wrapper files directly..."

# Create gradle/wrapper directory
mkdir -p gradle/wrapper

# Download gradle-wrapper.jar
curl -L "https://raw.githubusercontent.com/gradle/gradle/v${GRADLE_VERSION}/gradle/wrapper/gradle-wrapper.jar" \
    -o gradle/wrapper/gradle-wrapper.jar

# Download gradlew
curl -L "https://raw.githubusercontent.com/gradle/gradle/v${GRADLE_VERSION}/gradlew" \
    -o gradlew

# Download gradlew.bat
curl -L "https://raw.githubusercontent.com/gradle/gradle/v${GRADLE_VERSION}/gradlew.bat" \
    -o gradlew.bat

# Create gradle-wrapper.properties
cat > gradle/wrapper/gradle-wrapper.properties << EOF
distributionBase=GRADLE_USER_HOME
distributionPath=wrapper/dists
distributionUrl=https\://services.gradle.org/distributions/gradle-${GRADLE_VERSION}-bin.zip
networkTimeout=10000
validateDistributionUrl=true
zipStoreBase=GRADLE_USER_HOME
zipStorePath=wrapper/dists
EOF

chmod +x gradlew

echo "Gradle wrapper setup complete"
