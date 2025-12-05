#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <stdbool.h>
#include <sys/types.h>

#define MAX_PIDS 4096

typedef struct {
    pid_t pid;
    unsigned long utime;
    unsigned long stime;
    char comm[256];
} ProcessTime;

// Function to get system uptime in seconds
double get_uptime() {
    FILE* f = fopen("/proc/uptime", "r");
    if (!f) {
        perror("Failed to open /proc/uptime");
        return -1.0;
    }
    double uptime;
    if (fscanf(f, "%lf", &uptime) != 1) {
        uptime = -1.0;
    }
    fclose(f);
    return uptime;
}

// Function to read process statistics from /proc
int get_process_times(ProcessTime *proc_times) {
    DIR *proc_dir;
    struct dirent *entry;
    int count = 0;

    if (!(proc_dir = opendir("/proc"))) {
        perror("Failed to open /proc");
        return 0;
    }

    while ((entry = readdir(proc_dir)) != NULL && count < MAX_PIDS) {
        if (entry->d_type == DT_DIR && isdigit(*entry->d_name)) {
            pid_t pid = atoi(entry->d_name);
            char stat_path[256];
            snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", pid);

            FILE *stat_file = fopen(stat_path, "r");
            if (stat_file) {
                proc_times[count].pid = pid;

                char line[1024];
                if (fgets(line, sizeof(line), stat_file)) {
                    char *comm_end = strrchr(line, ')');
                    if (comm_end) {
                        *comm_end = '\0';
                        char *comm_start = strchr(line, '(');
                        if(comm_start) {
                           strncpy(proc_times[count].comm, comm_start + 1, sizeof(proc_times[count].comm) - 1);
                           proc_times[count].comm[sizeof(proc_times[count].comm) - 1] = '\0';
                        }

                        unsigned long utime, stime;
                        int items = sscanf(comm_end + 2,
                                    "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu",
                                    &utime, &stime);

                        if (items == 2) {
                             proc_times[count].utime = utime;
                             proc_times[count].stime = stime;
                             count++;
                        }
                    }
                }
                fclose(stat_file);
            }
        }
    }
    closedir(proc_dir);
    return count;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <interval_seconds> <cpu_threshold_percent>\n", argv[0]);
        return 1;
    }

    int interval = atoi(argv[1]);
    double threshold = atof(argv[2]);

    if (interval <= 0 || threshold < 0) {
        fprintf(stderr, "Interval must be positive, and threshold must be non-negative.\n");
        return 1;
    }

    ProcessTime initial_times[MAX_PIDS];
    ProcessTime final_times[MAX_PIDS];
    long hertz = sysconf(_SC_CLK_TCK);

    while (true) {
        int initial_count = get_process_times(initial_times);
        double start_uptime = get_uptime();

        if (initial_count == 0 || start_uptime < 0) {
            fprintf(stderr, "Could not get initial process statistics. Retrying...\n");
            sleep(interval);
            continue;
        }

        sleep(interval);

        int final_count = get_process_times(final_times);
        double end_uptime = get_uptime();

        if (final_count == 0 || end_uptime < 0) {
            fprintf(stderr, "Could not get final process statistics. Retrying...\n");
            continue;
        }

        double uptime_delta = end_uptime - start_uptime;
        if (uptime_delta <= 0) continue;

        printf("\n--- CPU Usage in last %d seconds (Threshold: %.2f%%) ---\n", interval, threshold);
        printf("%-10s %-20s %-10s\n", "PID", "COMMAND", "CPU%");

        for (int i = 0; i < final_count; i++) {
            for (int j = 0; j < initial_count; j++) {
                if (final_times[i].pid == initial_times[j].pid) {
                    unsigned long total_time_initial = initial_times[j].utime + initial_times[j].stime;
                    unsigned long total_time_final = final_times[i].utime + final_times[i].stime;
                    unsigned long time_delta = total_time_final - total_time_initial;

                    double cpu_percent = 100.0 * ((double)time_delta / hertz) / uptime_delta;

                    if (cpu_percent > threshold) {
                         printf("%-10d %-20s %-10.2f\n", final_times[i].pid, final_times[i].comm, cpu_percent);
                    }
                    break;
                }
            }
        }
        fflush(stdout);
    }

    return 0;
}
