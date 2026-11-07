// SPDX-License-Identifier: LGPL-3.0
// Validation-only Fluxion synchronization; no placement or policy changes.
#include <vector>

class quiescence_service_t {
    qmanager_ctx_t *ctx;
    flux_msg_handler_t **handlers = nullptr;
    flux_watcher_t *prepare = nullptr;
    flux_watcher_t *check = nullptr;
    flux_watcher_t *idle = nullptr;
    flux_future_t *query = nullptr;
    std::vector<const flux_msg_t *> requests;
    bool queried = false;
    int manager_queued = 0;
    int manager_pending = 0;

    bool busy () const
    {
        for (const auto &[name, queue] : ctx->queues)
            if (queue->is_sched_loop_active () || queue->is_schedulable ()
                || queue->is_scheduled () || queue->is_annotation_dirty ())
                return true;
        return false;
    }

    void fail (int errnum, const char *reason)
    {
        for (auto *msg : requests) {
            if (flux_respond_error (ctx->h, msg, errnum, reason) < 0)
                flux_log_error (ctx->h, "sched.quiescent: respond");
            flux_msg_decref (msg);
        }
        requests.clear ();
        queried = false;
    }

    static void request_cb (flux_t *h, flux_msg_handler_t *,
                            const flux_msg_t *msg, void *arg)
    {
        auto *self = static_cast<quiescence_service_t *> (arg);
        try {
            self->requests.push_back (msg);
            flux_msg_incref (msg);
        } catch (const std::bad_alloc &) {
            flux_respond_error (h, msg, ENOMEM, nullptr);
        }
    }

    static void query_cb (flux_future_t *f, void *arg)
    {
        auto *self = static_cast<quiescence_service_t *> (arg);
        if (flux_rpc_get_unpack (f, "{s:i s:i}",
                                 "queue_length", &self->manager_queued,
                                 "alloc_pending", &self->manager_pending) < 0)
            self->fail (errno, "job-manager.alloc-query failed");
        else
            self->queried = true;
        flux_future_destroy (f);
        self->query = nullptr;
    }

    static void prepare_cb (flux_reactor_t *, flux_watcher_t *, int, void *arg)
    {
        auto *self = static_cast<quiescence_service_t *> (arg);
        // Let outstanding matching/query RPCs wake the reactor; do not spin.
        if (!self->requests.empty () && !self->query && !self->busy ())
            flux_watcher_start (self->idle);
    }

    void settle ()
    {
        if (requests.empty () || query || busy ())
            return;
        if (queried) {
            size_t pending = 0;
            size_t running = 0;
            for (const auto &[name, queue] : ctx->queues) {
                pending += queue->quiescence_pending_count ();
                running += queue->quiescence_running_count ();
            }
            queried = false;
            // The job manager sends one alloc per reactor turn. Wait for its
            // entire unheld batch and for all allocation replies to settle.
            // The round trip also fences preceding sched.free messages.
            if (manager_queued == 0 && manager_pending >= 0
                && pending == static_cast<size_t> (manager_pending)) {
                for (auto *msg : requests) {
                    if (flux_respond_pack (ctx->h, msg, "{s:i s:I}", "status", 0,
                                           "alloc_current", static_cast<json_int_t> (running)) < 0)
                        flux_log_error (ctx->h, "sched.quiescent: respond");
                    flux_msg_decref (msg);
                }
                requests.clear ();
                return;
            }
        }
        query = flux_rpc (ctx->h, "job-manager.alloc-query", nullptr, 0, 0);
        if (!query || flux_future_then (query, -1., query_cb, this) < 0) {
            int saved_errno = errno;
            flux_future_destroy (query);
            query = nullptr;
            fail (saved_errno, "could not query job manager");
        }
    }

    static void check_cb (flux_reactor_t *, flux_watcher_t *, int, void *arg)
    {
        auto *self = static_cast<quiescence_service_t *> (arg);
        flux_watcher_stop (self->idle);
        try {
            self->settle ();
        } catch (const std::exception &) {
            self->fail (EIO, "quiescence check failed");
        }
    }

   public:
    explicit quiescence_service_t (qmanager_ctx_t *context) : ctx (context) {}

    int start ()
    {
        static const flux_msg_handler_spec table[] = {
            {FLUX_MSGTYPE_REQUEST, "sched.quiescent", request_cb, FLUX_ROLE_USER},
            FLUX_MSGHANDLER_TABLE_END,
        };
        auto *r = flux_get_reactor (ctx->h);
        prepare = flux_prepare_watcher_create (r, prepare_cb, this);
        check = flux_check_watcher_create (r, check_cb, this);
        idle = flux_idle_watcher_create (r, nullptr, nullptr);
        if (!prepare || !check || !idle
            || flux_msg_handler_addvec (ctx->h, table, this, &handlers) < 0)
            return -1;
        flux_watcher_start (prepare);
        flux_watcher_start (check);
        return 0;
    }

    ~quiescence_service_t ()
    {
        fail (ENOSYS, "scheduler unloading");
        flux_future_destroy (query);
        flux_msg_handler_delvec (handlers);
        flux_watcher_destroy (prepare);
        flux_watcher_destroy (check);
        flux_watcher_destroy (idle);
    }
};
