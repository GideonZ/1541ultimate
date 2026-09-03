#!/bin/bash

# Colors for Feedback
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${GREEN}Star Setup...${NC}"

# 1. Platform detection
OS_TYPE="$(uname -s)"
DISTRO=""

if [ "$OS_TYPE" == "Linux" ]; then
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        DISTRO=$ID
    fi
fi

# 2. install system dependencies
case "$OS_TYPE" in
    "Linux")
        echo -e "${YELLOW}Linux detected ($DISTRO). Check for libusb...${NC}"
        if [[ "$DISTRO" == "ubuntu" || "$DISTRO" == "debian" || "$DISTRO" == "raspbian" ]]; then
            sudo apt-get update -qq
            sudo apt-get install -y libusb-1.0-0 python3-venv
        fi
        
        # udev rules 
        if [ -f "99-ft232h.rules" ]; then
            echo -e "${YELLOW}Install udev Rules...${NC}"
            sudo cp 99-ft232h.rules /etc/udev/rules.d/
            sudo udevadm control --reload-rules
            sudo udevadm trigger
        fi
        ;;
        
    "Darwin")
        echo -e "${YELLOW}macOS detected. Check Homebrew dependencies...${NC}"
        if command -v brew &> /dev/null; then
            brew install libusb
        else
            echo -e "${RED}Error: Homebrew not installed. Please visit https://brew.sh${NC}"
        fi
        ;;

    *)
        echo -e "${RED}Unknown System: $OS_TYPE. Please install manually.${NC}"
        ;;
esac

# 3. Virtual environment & Python packets (für alle gleich)
echo -e "${GREEN}Setup Python virtual environment ...${NC}"
python3 -m venv .venv
source .venv/bin/activate
pip install --upgrade pip
pip install -r requirements.txt

echo -e "${GREEN}Setup succesfull!${NC}"