/* Read-only observations after each simulator event/scheduling batch. */
static void validation_snapshot(void)
{
    static uint64_t previous_hash = 0;
    static FILE *output = NULL;
    const char *path = getenv("SLURM_VALIDATION_SNAPSHOTS");
    if (!path)
        return;
    static time_t previous_job_update = (time_t)-1;
    static time_t previous_node_update = (time_t)-1;
    if (previous_job_update == last_job_update && previous_node_update == last_node_update)
        return;
    previous_job_update = last_job_update;
    previous_node_update = last_node_update;
    uint64_t hash = 1469598103934665603ULL;
    int i;
    for (i = 0; i < node_record_count; ++i) {
        struct node_record *node = node_record_table_ptr + i;
        if (IS_NODE_ALLOCATED(node)) {
            hash = (hash ^ (uint64_t)(i + 1)) * 1099511628211ULL;
        }
    }
    ListIterator iterator = list_iterator_create(job_list);
    struct job_record *job;
    while ((job = list_next(iterator))) {
        if (!IS_JOB_RUNNING(job))
            continue;
        hash = (hash ^ job->job_id) * 1099511628211ULL;
        for (i = 0; i < node_record_count; ++i) {
            if (job->node_bitmap && bit_test(job->node_bitmap, i))
                hash = (hash ^ (uint64_t)(i + 1)) * 1099511628211ULL;
        }
    }
    list_iterator_destroy(iterator);
    if (hash == previous_hash)
        return;
    previous_hash = hash;
    if (!output) {
        output = fopen(path, "w");
        if (!output)
            fatal("Cannot open validation snapshot file");
    }
    fprintf(output, "{\"time\":%ld,\"allocated\":[", (long)time(NULL));
    int comma = 0;
    for (i = 0; i < node_record_count; ++i) {
        struct node_record *node = node_record_table_ptr + i;
        if (IS_NODE_ALLOCATED(node)) {
            fprintf(output, "%s%d", comma ? "," : "", i);
            comma = 1;
        }
    }
    fprintf(output, "],\"jobs\":[");
    comma = 0;
    iterator = list_iterator_create(job_list);
    while ((job = list_next(iterator))) {
        if (!IS_JOB_RUNNING(job))
            continue;
        fprintf(output, "%s{\"id\":%u,\"nodes\":[", comma ? "," : "", job->job_id);
        comma = 1;
        int node_comma = 0;
        for (i = 0; i < node_record_count; ++i) {
            if (job->node_bitmap && bit_test(job->node_bitmap, i)) {
                fprintf(output, "%s%d", node_comma ? "," : "", i);
                node_comma = 1;
            }
        }
        fprintf(output, "]}");
    }
    list_iterator_destroy(iterator);
    fprintf(output, "]}\n");
    fflush(output);
}
