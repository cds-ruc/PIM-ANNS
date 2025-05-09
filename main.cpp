#include "main.h"

int main(int argc, char **argv)
{
    int nprobe = -1; 
    if (argc > 1)
    {
        nprobe = atoi(argv[1]);
    }
    else
    {
        printf("Usage: %s <nprobe>\n", argv[0]);
        return 1;
    }

    

    CPPkernel(10,  nprobe);

    return 0;
}