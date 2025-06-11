#!/bin/bash

# Script to run the LED matrix demo with environment variables loaded
# Usage: ./run-demo.sh

# Check if credentials file exists
if [ ! -f "credentials.env" ]; then
    echo "Error: credentials.env file not found!"
    echo "Please copy credentials.env.example to credentials.env and fill in your credentials."
    exit 1
fi

# Load environment variables
echo "Loading credentials from credentials.env..."
source credentials.env

# Check if required variables are set
if [ -z "$SPOTIFY_CLIENT_ID" ] || [ -z "$SPOTIFY_CLIENT_SECRET" ] || [ -z "$SPOTIFY_REFRESH_TOKEN" ]; then
    echo "Warning: Some Spotify credentials are missing!"
fi

if [ -z "$WEATHER_API_KEY" ]; then
    echo "Warning: Weather API key is missing!"
fi

# Build the demo if needed
if [ ! -f "multi-screen-demo" ] || [ "multi-screen-demo.cc" -nt "multi-screen-demo" ]; then
    echo "Building multi-screen-demo..."
    make multi-screen-demo
    if [ $? -ne 0 ]; then
        echo "Build failed!"
        exit 1
    fi
fi

# Run the demo (preserve environment variables with sudo -E)
echo "Starting LED matrix demo..."
sudo -E ./multi-screen-demo 