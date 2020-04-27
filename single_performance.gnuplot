reset

# Output as SVG
set terminal svg size 800, 500 rounded linewidth 2
set output  "single_performance.svg"

# Set plot and axis titles
set title "Number of Client Threads vs. Mean Throughput and 95th% Latency\n\
{/*0.75 server threads = 1, maxmem = 64KiB}" font ",18"
set xlabel "Client Threads"
set ylabel "Mean Throughput (req/s)"
set y2label "95th-percentile latency (µs)"

# Put the key at the bottom right
set key bottom right

# Configure axes
set xrange  [1:16]      # Set X-axis scale
set xtics   1           # Add X-tics at intervals of 1
set yrange  [0:100000]  # Set left Y-axis scale
set ytics   10000       # Add left Y-tics at intervals of 10k
set y2range [0:250]     # Set right Y-axis scale
set y2tics  25          # Add right Y-tics at intervals of 25µs
set grid                # Enable grid lines (easier to read)

# Plot data using lines with circle points
set style data linespoints
plot "single_performance.dat" using 1:2 axes x1y1 \
     title "Mean Throughput" pointtype 7 pointsize 0.75, \
     "single_performance.dat" using 1:3 axes x1y2 \
     title "95th% Latency" pointtype 7 pointsize 0.75
