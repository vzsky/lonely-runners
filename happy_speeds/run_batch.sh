#!/bin/bash

# Configuration
OUTPUT_FILE="one_to_k_happy_result.txt"
K_VALUE=10
START_P=100
END_P=500
EXECUTABLE="./one_to_k_happy"

# Pure Bash function to check if a number is prime
check_prime() {
    local num=$1
    if (( num <= 1 )); then echo "no"; return; fi
    for (( i=2; i*i<=num; i++ )); do
        if (( num % i == 0 )); then
            echo "no"
            return
        fi
    done
    echo "yes"
}

# Initialize or clear the results file
echo "Batch Run: k=$K_VALUE, p from $START_P to $END_P" > "$OUTPUT_FILE"
echo "Generated on: $(date)" >> "$OUTPUT_FILE"
echo "------------------------------------------------" >> "$OUTPUT_FILE"

# Ensure the executable exists
if [ ! -f "$EXECUTABLE" ]; then
    echo "Error: $EXECUTABLE not found. Please compile your C++ code first."
    exit 1
fi

echo "Starting batch run. Monitoring primes from $START_P to $END_P..."

# Loop through the range
for ((p=$START_P; p<=$END_P; p++))
do
    # Use our internal pure-bash prime function
    is_prime=$(check_prime $p)

    if [ "$is_prime" == "yes" ]; then
        echo "Processing p=$p..."
        
        # Feed p and k to the program. 
        # We use 'grep' to capture only the final result and time.
        result=$(echo "$p $K_VALUE" | $EXECUTABLE | grep -E "Problem|Time|unhappy")

        # Record to file
        echo "p=$p, k=$K_VALUE" >> "$OUTPUT_FILE"
        echo "$result" >> "$OUTPUT_FILE"
        echo "------------------------------------------------" >> "$OUTPUT_FILE"
    fi
done

echo "Batch processing complete. Results recorded in $OUTPUT_FILE"