#include "config.h"
#include "slurm/slurm.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/interfaces/gres.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern void validation_set_time(int64_t);
extern void validation_release_batch(void);

#ifdef VALIDATION_STATE_CACHE
struct cached_job {
    int initialized;
    uint32_t state;
    time_t start, end;
    char nodes[128], licenses[64];
};
static struct cached_job cached[4096];
#endif

int main(int argc, char **argv)
{
    slurm_init(argv[1]);
    char line[256], command[32], node[64];
    unsigned int value, tokens, minutes;
    while (fgets(line, sizeof(line), stdin)) {
        sscanf(line, "%31s", command);
        int rc = 0;
        if (!strcmp(command, "TIME")) {
            validation_set_time(strtoll(line + 5, NULL, 10));
            puts("{}");
        } else if (!strcmp(command, "RELEASE")) {
            validation_release_batch();
            puts("{}");
        } else if (!strcmp(command, "REGISTER")) {
            sscanf(line, "%*s %63s", node);
            acct_gather_energy_t energy = {0};
            buf_t *gres = init_buf(128);
            gres_node_config_pack(gres);
            slurm_node_registration_status_msg_t registration = {
                .arch = "aarch64", .os = "Linux", .version = "26.11.0-0rc2",
                .hostname = "localhost", .node_name = node,
                .cpus = 1, .boards = 1, .sockets = 1, .cores = 1, .threads = 1,
                .real_memory = 128, .free_mem = 128, .up_time = 1000,
                .timestamp = time(NULL), .slurmd_start_time = time(NULL), .energy = &energy,
                .gres_info = gres, .hash_val = NO_VAL,
            };
            slurm_msg_t msg;
            slurm_msg_t_init(&msg);
            msg.msg_type = MESSAGE_NODE_REGISTRATION_STATUS;
            msg.data = &registration;
            int remote_rc = 0;
            rc = slurm_send_recv_controller_rc_msg(&msg, &remote_rc, NULL);
            free_buf(gres);
            if (rc || remote_rc) { fprintf(stderr, "registration failed: %d %d\n", rc, remote_rc); return 1; }
            puts("{}");
        } else if (!strcmp(command, "SUBMIT")) {
            sscanf(line, "%*s %u %u %63s", &tokens, &minutes, node);
            job_desc_msg_t job;
            resource_allocation_response_msg_t *response = NULL;
            slurm_init_job_desc_msg(&job);
            job.alloc_node = "localhost";
            job.work_dir = "/tmp";
            job.name = node;
            job.req_nodes = node;
            job.min_nodes = job.max_nodes = 1;
            job.num_tasks = job.min_cpus = 1;
            job.time_limit = minutes;
            char licenses[64];
            snprintf(licenses, sizeof(licenses), "capacity:%u", tokens);
            job.licenses = licenses;
            rc = slurm_allocate_resources(&job, &response);
            if (rc || !response) { fprintf(stderr, "submit: %s\n", slurm_strerror(errno)); return 1; }
            printf("{\"id\":%u}\n", response->step_id.job_id);
            slurm_free_resource_allocation_response_msg(response);
        } else if (!strcmp(command, "COMPLETE")) {
            sscanf(line, "%*s %u %63s", &value, node);
            slurm_step_id_t step = SLURM_STEP_ID_INITIALIZER;
            step.job_id = value;
            rc = slurm_complete_job(&step, 0);
            if (rc) { fprintf(stderr, "complete: %s\n", slurm_strerror(errno)); return 1; }
            epilog_complete_msg_t epilog = {.step_id = step, .node_name = node};
            slurm_msg_t msg;
            slurm_msg_t_init(&msg);
            msg.msg_type = MESSAGE_EPILOG_COMPLETE;
            msg.data = &epilog;
            int remote_rc = 0;
            rc = slurm_send_recv_controller_rc_msg(&msg, &remote_rc, NULL);
            if (rc || remote_rc) { fprintf(stderr, "epilog completion failed\n"); return 1; }
            puts("{}");
        } else if (!strcmp(command, "DUMP")) {
#ifdef VALIDATION_STATE_CACHE
            job_state_response_msg_t *states = NULL;
            rc = slurm_load_job_state(0, NULL, &states);
            if (rc) return 1;
            printf("{\"last_backfill\":0,\"jobs\":[");
            for (unsigned int i = 0; i < states->jobs_count; i++) {
                uint32_t id = states->jobs[i].job_id;
                if (id >= 4096) return 1;
                struct cached_job *entry = &cached[id];
                if (!entry->initialized || entry->state != states->jobs[i].state) {
                    job_info_msg_t *details = NULL;
                    slurm_step_id_t step = SLURM_STEP_ID_INITIALIZER;
                    step.job_id = id;
                    if (slurm_load_job(&details, step, SHOW_ALL)) return 1;
                    slurm_job_info_t *job = &details->job_array[0];
                    entry->initialized = 1;
                    entry->state = job->job_state;
                    entry->start = job->start_time;
                    entry->end = job->end_time;
                    snprintf(entry->nodes, sizeof(entry->nodes), "%s", job->nodes ? job->nodes : "");
                    snprintf(entry->licenses, sizeof(entry->licenses), "%s", job->licenses ? job->licenses : "");
                    slurm_free_job_info_msg(details);
                }
                printf("%s{\"id\":%u,\"state\":%u,\"start\":%ld,\"end\":%ld,"
                       "\"assignment\":\"%s\",\"licenses\":\"%s\"}", i ? "," : "",
                       id, entry->state, (long)entry->start, (long)entry->end,
                       entry->nodes, entry->licenses);
            }
            slurm_free_job_state_response_msg(states);
#else
            job_info_msg_t *response = NULL;
            rc = slurm_load_jobs(0, &response, SHOW_ALL);
            if (rc) { fprintf(stderr, "dump: %s\n", slurm_strerror(errno)); return 1; }
            printf("{\"last_backfill\":%ld,\"jobs\":[", (long)response->last_backfill);
            for (unsigned int i = 0; i < response->record_count; i++) {
                slurm_job_info_t *job = &response->job_array[i];
                printf("%s{\"id\":%u,\"state\":%u,\"start\":%ld,\"end\":%ld,"
                       "\"assignment\":\"%s\",\"licenses\":\"%s\"}", i ? "," : "",
                       job->step_id.job_id, job->job_state, (long)job->start_time,
                       (long)job->end_time, job->nodes ? job->nodes : "",
                       job->licenses ? job->licenses : "");
            }
            slurm_free_job_info_msg(response);
#endif
            license_info_msg_t *licenses = NULL;
            rc = slurm_load_licenses(0, &licenses, SHOW_ALL);
            if (rc || licenses->num_lic != 1) return 1;
            printf("],\"license_total\":%u,\"license_used\":%u}\n",
                   licenses->lic_array[0].total, licenses->lic_array[0].in_use);
            slurm_free_license_info_msg(licenses);
        } else return 2;
        fflush(stdout);
    }
    return 0;
}
