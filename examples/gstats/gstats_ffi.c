#include <time.h>

long long get_timestamp(long long dummy) {
    return (long long)time(NULL);
}
