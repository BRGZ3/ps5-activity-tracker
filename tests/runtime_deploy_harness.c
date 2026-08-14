#include "offline_update.h"

#include <stdio.h>

int
main(int argc, char **argv) {
    unsigned copies = 0;
    int manual = 0;
    int result;
    if(argc != 3) return 2;
    result = offline_deploy_runtime_copies(
        argv[1], argv[2], &copies, &manual);
    printf("%d %u %d\n", result, copies, manual);
    return result < 0 ? 2 : result;
}
