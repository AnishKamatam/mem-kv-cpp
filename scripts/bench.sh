HOST="127.0.0.1"
PORT=8080
CONCURRENT_CLIENTS=10
REQUESTS_PER_CLIENT=1000

echo "Starting benchmark: $CONCURRENT_CLIENTS clients, $REQUESTS_PER_CLIENT requests each..."

start_time=$(date +%s%N)

for ((i=1; i<=CONCURRENT_CLIENTS; i++)); do
    (
        for ((j=1; j<=REQUESTS_PER_CLIENT; j++)); do
            echo "SET key_$i_$j value_$j" | nc $HOST $PORT > /dev/null
        done
    ) &
done

wait

end_time=$(date +%s%N)
duration=$(echo "scale=3; ($end_time - $start_time) / 1000000000" | bc)
total_reqs=$((CONCURRENT_CLIENTS * REQUESTS_PER_CLIENT))
rps=$(echo "scale=0; $total_reqs / $duration" | bc)

echo "------------------------------"
echo "Total Requests: $total_reqs"
echo "Total Time:     $duration s"
echo "Requests/sec:   $rps"
echo "------------------------------"

