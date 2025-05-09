dpu-upmem-dpurte-clang -O3 pivot_example.c -o pivot_example

gcc -O3 --std=c99 -o pivot_example_host pivot_example_host.c -g `dpu-pkg-config --cflags --libs dpu`

./pivot_example_host