#!/bin/bash

if [ "$#" -lt 1 ]; then
  echo "Usage: $0 {main|test}"
  echo ""
  echo "Commands:"
  echo "  ./run.sh main  - Build and run main lonely_runners program"
  echo "  ./run.sh test  - Build and run unit tests"
  exit 1
fi

TARGET=$1

# Build the project
echo "Building project..."
cmake -B build > /dev/null 2>&1
if [ $? -ne 0 ]; then
  echo "CMake configuration failed"
  exit 1
fi

cmake --build build > /dev/null 2>&1
if [ $? -ne 0 ]; then
  echo "Build failed"
  exit 1
fi

# Run the appropriate target
start_iso=$(date -Iseconds)
echo "Execution start: $start_iso"
echo ""

case "$TARGET" in
  main)
    ./build/lonely_runners
    ;;
  test)
    ./build/test_main
    ;;
  *)
    echo "Unknown target: $TARGET"
    echo "Use 'main' or 'test'"
    exit 1
    ;;
esac

end_iso=$(date -Iseconds)
echo ""
echo "Execution end: $end_iso"
