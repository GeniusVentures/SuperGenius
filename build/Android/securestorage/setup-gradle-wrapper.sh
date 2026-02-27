#!/bin/bash
# Script to set up Gradle wrapper for the Android AAR build

set -e

cd "$(dirname "$0")"

# Check if gradlew already exists
if [ -f gradlew ]; then
    echo "Gradle wrapper already exists"
    exit 0
fi

GRADLE_VERSION="8.2"

# Check if gradle is installed and what version
if command -v gradle &> /dev/null; then
    GRADLE_VER=$(gradle --version | grep "^Gradle" | awk '{print $2}')
    echo "Found Gradle $GRADLE_VER"
    
    # Check if version is at least 6.0
    if [ "$(printf '%s\n' "6.0" "$GRADLE_VER" | sort -V | head -n1)" = "6.0" ]; then
        echo "Using installed Gradle to create wrapper..."
        gradle wrapper --gradle-version $GRADLE_VERSION
        exit 0
    else
        echo "Warning: Gradle $GRADLE_VER is too old (need 6.0+)"
    fi
fi

echo "Downloading Gradle wrapper files directly..."

# Create gradle/wrapper directory
mkdir -p gradle/wrapper

# Download gradle-wrapper.jar
curl -L "https://raw.githubusercontent.com/gradle/gradle/v${GRADLE_VERSION}.0/gradle/wrapper/gradle-wrapper.jar" \
    -o gradle/wrapper/gradle-wrapper.jar

# Download gradlew
curl -L "https://raw.githubusercontent.com/gradle/gradle/v${GRADLE_VERSION}.0/gradlew" \
    -o gradlew

# Download gradlew.bat
curl -L "https://raw.githubusercontent.com/gradle/gradle/v${GRADLE_VERSION}.0/gradlew.bat" \
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
