<template>
    <div v-if="jobStatus" class="status-grid">
        <article class="status-box">
            <span class="status-box__label">Job</span>
            <span class="status-box__value">{{ jobStatus.job.status }}</span>
        </article>
        <article class="status-box">
            <span class="status-box__label">Running</span>
            <span class="status-box__value">{{ jobStatus.step_status_counts.running }}</span>
        </article>
        <article class="status-box">
            <span class="status-box__label">Queued</span>
            <span class="status-box__value">{{ jobStatus.step_status_counts.queued }}</span>
        </article>
        <article class="status-box">
            <span class="status-box__label">Failed</span>
            <span class="status-box__value">{{ jobStatus.step_status_counts.failed }}</span>
        </article>
    </div>

    <div v-if="jobStatus?.outbox_messages?.length" class="outbox-table">
        <div class="outbox-table__head">
            <span>Topic</span>
            <span>Interface</span>
            <span>Status</span>
            <span>Payload</span>
        </div>
        <article class="outbox-row" v-for="message in jobStatus.outbox_messages" :key="message.id">
            <div class="outbox-cell outbox-cell--mono">{{ message.topic }}</div>
            <div class="outbox-cell">
                <span class="interface-badge" :class="interfaceBadgeClass(message.payload?.interface_type)">
                    {{ message.payload?.interface_type || 'topic' }}
                </span>
            </div>
            <div class="outbox-cell">{{ message.status }}</div>
            <div class="outbox-cell">
                <pre>{{ formatParameters(message.payload) }}</pre>
            </div>
        </article>
    </div>

    <div v-else class="empty-state">
        Create and dispatch a job to inspect outbox traffic and step state.
    </div>

    <div v-if="jobStatus?.step_executions?.length" class="reply-board">
        <div class="reply-board__head">
            <span>Step Runtime</span>
            <span>Interface</span>
            <span>Status</span>
            <span>Latest Reply</span>
        </div>
        <article class="reply-row" v-for="execution in jobStatus.step_executions" :key="execution.id">
            <div class="reply-cell">
                <span class="queue-step">{{ execution.command_payload?.step_no || execution.id }}</span>
                <span class="queue-name">{{ execution.command_payload?.name || execution.recipe_step }}</span>
            </div>
            <div class="reply-cell">
                <span class="interface-badge" :class="interfaceBadgeClass(execution.command_payload?.interface_type)">
                    {{ execution.command_payload?.interface_type || 'topic' }}
                </span>
            </div>
            <div class="reply-cell">{{ execution.status }}</div>
            <div class="reply-cell">
                <div class="reply-meta" v-if="execution.telemetry?.last_reply_message_type">
                    <span class="reply-type">{{ execution.telemetry.last_reply_message_type }}</span>
                    <span class="reply-status">{{ execution.telemetry.last_reply_status || 'n/a' }}</span>
                </div>
                <div class="reply-copy">{{ summariseExecutionReply(execution) }}</div>
            </div>
        </article>
    </div>
</template>

<script>
export default {
    name: 'JobStatusBoard',
    props: {
        jobStatus: {
            type: Object,
            default: null
        }
    },
    methods: {
        formatParameters(parameters) {
            return JSON.stringify(parameters || {}, null, 2)
        },
        interfaceBadgeClass(interfaceType) {
            return `interface-badge--${interfaceType || 'topic'}`
        },
        summariseExecutionReply(execution) {
            const reply = execution.telemetry?.last_device_reply
            if (!reply) {
                return execution.error_message || 'No device reply yet.'
            }
            if (reply.message_type === 'progress') {
                const percent = reply.progress?.percent
                const stage = reply.progress?.stage
                return percent != null ? `Progress ${percent}%${stage ? ` • ${stage}` : ''}` : (stage || 'Progress update received.')
            }
            if (reply.message_type === 'result') {
                return reply.result ? JSON.stringify(reply.result) : 'Execution completed successfully.'
            }
            if (reply.message_type === 'error') {
                return reply.error?.message || reply.message || execution.error_message || 'Execution failed.'
            }
            return reply.status || reply.message_type || 'Reply received.'
        }
    }
}
</script>

<style scoped>
.status-grid {
    display: grid;
    grid-template-columns: repeat(4, minmax(0, 1fr));
    gap: 0.85rem;
    margin-bottom: 1rem;
}

.status-box {
    display: flex;
    flex-direction: column;
    gap: 0.2rem;
    padding: 0.9rem 1rem;
    border-radius: 16px;
    background: #f6f9ff;
    border: 1px solid rgba(15, 23, 36, 0.08);
}

.status-box__label {
    color: #64748b;
    font-size: 0.74rem;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    font-weight: 700;
}

.status-box__value {
    color: #111827;
    font-weight: 700;
}

.outbox-table,
.reply-board {
    border-radius: 18px;
    overflow: hidden;
    border: 1px solid rgba(15, 23, 36, 0.08);
}

.reply-board {
    margin-top: 1rem;
}

.outbox-table__head,
.outbox-row {
    display: grid;
    grid-template-columns: 0.9fr 0.7fr 0.5fr 1.4fr;
    gap: 0.75rem;
    align-items: start;
    padding: 0.95rem 1rem;
}

.reply-board__head,
.reply-row {
    display: grid;
    grid-template-columns: 1fr 0.7fr 0.6fr 1.6fr;
    gap: 0.75rem;
    align-items: start;
    padding: 0.9rem 1rem;
}

.outbox-table__head,
.reply-board__head {
    background: #ecf2f8;
    color: #5f6d81;
    font-size: 0.74rem;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    font-weight: 700;
}

.outbox-row,
.reply-row {
    background: #ffffff;
    border-top: 1px solid rgba(15, 23, 36, 0.06);
}

.outbox-row:nth-child(even),
.reply-row:nth-child(even) {
    background: #fafcfe;
}

.outbox-cell,
.reply-cell {
    color: #1f2937;
    font-size: 0.92rem;
}

.outbox-cell pre {
    margin: 0;
    padding: 0.85rem;
    background: #0f172a;
    color: #e2e8f0;
    border-radius: 14px;
    font-size: 0.8rem;
    overflow: auto;
    white-space: pre-wrap;
    word-break: break-word;
}

.outbox-cell--mono {
    font-family: "SFMono-Regular", Consolas, "Liberation Mono", Menlo, monospace;
    font-size: 0.84rem;
}

.queue-step {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: 34px;
    height: 34px;
    border-radius: 10px;
    background: #e5edf6;
    color: #274c7d;
    font-size: 0.8rem;
    font-weight: 700;
    margin-right: 0.65rem;
}

.queue-name {
    font-weight: 700;
    color: #111827;
}

.interface-badge {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    min-width: 84px;
    padding: 0.28rem 0.55rem;
    border-radius: 999px;
    font-size: 0.72rem;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: 0.08em;
}

.interface-badge--topic {
    background: #edf6ff;
    color: #1f5f95;
}

.interface-badge--service {
    background: #f1f5e8;
    color: #4f6f1f;
}

.interface-badge--action {
    background: #fff3e8;
    color: #9a5711;
}

.reply-meta {
    display: flex;
    gap: 0.5rem;
    margin-bottom: 0.25rem;
    flex-wrap: wrap;
}

.reply-type,
.reply-status {
    font-size: 0.74rem;
    font-weight: 700;
    text-transform: uppercase;
    color: #5b6575;
}

.reply-copy {
    color: #475569;
    line-height: 1.5;
}

.empty-state {
    padding: 1.6rem;
    border-radius: 18px;
    border: 1px dashed rgba(100, 116, 139, 0.28);
    color: #64748b;
    background: #fbfcfd;
}

@media screen and (max-width: 960px) {
    .status-grid,
    .outbox-table__head,
    .outbox-row,
    .reply-board__head,
    .reply-row {
        grid-template-columns: 1fr;
    }
}
</style>
